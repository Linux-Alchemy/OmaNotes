#include "session/session_snapshot.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <system_error>

namespace omanotes {

namespace {

using Failure = std::unexpected<SessionError>;

Failure fail(SessionErrorCode code, QString location, QString message) {
    return Failure(SessionError{code, std::move(location), std::move(message)});
}

Failure invalid(const QString& location, QString message) {
    return fail(SessionErrorCode::InvalidField, location, std::move(message));
}

QString join(const QString& parent, const QString& child) {
    return parent.isEmpty() ? child : parent + QLatin1Char('.') + child;
}

QString index(const QString& parent, qsizetype position) {
    return QStringLiteral("%1[%2]").arg(parent).arg(position);
}

/// Refuse any key the format does not define. A same-version document with
/// extra keys was not written by this program, and guessing at its intent is
/// how state files become an input channel.
std::expected<void, SessionError> onlyKeys(const QJsonObject& object, const QString& location,
                                           const std::set<QString>& allowed) {
    for (auto item = object.constBegin(); item != object.constEnd(); ++item) {
        if (!allowed.contains(item.key())) {
            return invalid(join(location, item.key()), QStringLiteral("Unknown field"));
        }
    }
    return {};
}

std::expected<QJsonObject, SessionError>
requireObject(const QJsonObject& parent, const QString& location, const QString& key) {
    const auto value = parent.value(key);
    if (!value.isObject()) {
        return invalid(join(location, key), QStringLiteral("Expected an object"));
    }
    return value.toObject();
}

std::expected<bool, SessionError> requireBool(const QJsonObject& parent, const QString& location,
                                              const QString& key) {
    const auto value = parent.value(key);
    if (!value.isBool()) {
        return invalid(join(location, key), QStringLiteral("Expected true or false"));
    }
    return value.toBool();
}

std::expected<bool, SessionError> optionalBool(const QJsonObject& parent, const QString& location,
                                               const QString& key, bool fallback) {
    if (!parent.contains(key)) {
        return fallback;
    }
    return requireBool(parent, location, key);
}

struct IntRange {
    int minimum;
    int maximum;
};

/// JSON numbers are doubles; a value only counts as an integer here when it
/// is whole and within the caller's inclusive range.
std::expected<int, SessionError> requireInt(const QJsonObject& parent, const QString& location,
                                            const QString& key, IntRange range) {
    const auto value = parent.value(key);
    const auto number = value.toDouble(std::nan(""));
    if (!value.isDouble() || std::isnan(number) || std::floor(number) != number ||
        number < static_cast<double>(range.minimum) ||
        number > static_cast<double>(range.maximum)) {
        return invalid(join(location, key), QStringLiteral("Expected a whole number from %1 to %2")
                                                .arg(range.minimum)
                                                .arg(range.maximum));
    }
    return static_cast<int>(number);
}

std::expected<int, SessionError> optionalInt(const QJsonObject& parent, const QString& location,
                                             const QString& key, IntRange range, int fallback) {
    if (!parent.contains(key)) {
        return fallback;
    }
    return requireInt(parent, location, key, range);
}

std::expected<QString, SessionError> requireString(const QJsonObject& parent,
                                                   const QString& location, const QString& key) {
    const auto value = parent.value(key);
    if (!value.isString()) {
        return invalid(join(location, key), QStringLiteral("Expected a string"));
    }
    return value.toString();
}

std::expected<QUuid, SessionError> requireUuid(const QJsonObject& parent, const QString& location,
                                               const QString& key) {
    const auto text = requireString(parent, location, key);
    if (!text) {
        return Failure(text.error());
    }
    const auto uuid = QUuid::fromString(*text);
    if (uuid.isNull()) {
        return invalid(join(location, key), QStringLiteral("Expected a UUID"));
    }
    return uuid;
}

/// A stored path must be relative, non-empty, lexically normal, and free of
/// parent references, so no document can even name a location outside its
/// root. Whether the file still exists, or is a symlink out, is decided
/// later against the live root by resolveSessionPath.
std::expected<std::filesystem::path, SessionError> relativePath(const QByteArray& encoded,
                                                                const QString& location) {
    if (encoded.isEmpty() || static_cast<std::size_t>(encoded.size()) > kSessionMaxPathBytes) {
        return invalid(
            location, QStringLiteral("Expected a path of 1 to %1 bytes").arg(kSessionMaxPathBytes));
    }
    if (encoded.contains('\0')) {
        return invalid(location, QStringLiteral("Path contains a NUL byte"));
    }
    std::filesystem::path path(encoded.toStdString());
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return invalid(location, QStringLiteral("Path must be relative to the workspace"));
    }
    if (path.lexically_normal() != path || path.filename().empty()) {
        return invalid(location, QStringLiteral("Path must be in normal form"));
    }
    if (std::ranges::any_of(path, [](const auto& part) { return part == ".." || part == "."; })) {
        return invalid(location, QStringLiteral("Path must not contain . or .. components"));
    }
    return path;
}

std::expected<std::filesystem::path, SessionError>
requireRelativePath(const QJsonObject& parent, const QString& location, const QString& key) {
    const auto text = requireString(parent, location, key);
    if (!text) {
        return Failure(text.error());
    }
    return relativePath(text->toUtf8(), join(location, key));
}

std::expected<WindowSnapshot, SessionError> parseWindow(const QJsonObject& object,
                                                        const QString& location) {
    if (const auto keys = onlyKeys(object, location, {"width", "height", "maximized"}); !keys) {
        return Failure(keys.error());
    }
    WindowSnapshot window;
    const auto width = requireInt(object, location, "width", {1, kSessionMaxDimension});
    if (!width) {
        return Failure(width.error());
    }
    const auto height = requireInt(object, location, "height", {1, kSessionMaxDimension});
    if (!height) {
        return Failure(height.error());
    }
    const auto maximized = optionalBool(object, location, "maximized", false);
    if (!maximized) {
        return Failure(maximized.error());
    }
    window.width = *width;
    window.height = *height;
    window.maximized = *maximized;
    return window;
}

std::expected<SidebarSnapshot, SessionError> parseSidebar(const QJsonObject& object,
                                                          const QString& location) {
    if (const auto keys = onlyKeys(object, location, {"visible", "width", "selectedPath"}); !keys) {
        return Failure(keys.error());
    }
    SidebarSnapshot sidebar;
    const auto visible = requireBool(object, location, "visible");
    if (!visible) {
        return Failure(visible.error());
    }
    const auto width = optionalInt(object, location, "width", {0, kSessionMaxDimension}, 0);
    if (!width) {
        return Failure(width.error());
    }
    sidebar.visible = *visible;
    sidebar.width = *width;
    if (object.contains("selectedPath")) {
        auto selected = requireRelativePath(object, location, "selectedPath");
        if (!selected) {
            return Failure(selected.error());
        }
        sidebar.selectedPath = std::move(*selected);
    }
    return sidebar;
}

std::expected<BufferSnapshot, SessionError> parseBuffer(const QJsonValue& value,
                                                        const QString& location) {
    if (!value.isObject()) {
        return invalid(location, QStringLiteral("Expected an object"));
    }
    const auto object = value.toObject();
    if (const auto keys =
            onlyKeys(object, location,
                     {"id", "path", "viewMode", "cursor", "scrollLine", "modified", "recovery"});
        !keys) {
        return Failure(keys.error());
    }
    BufferSnapshot buffer;
    const auto id = requireUuid(object, location, "id");
    if (!id) {
        return Failure(id.error());
    }
    buffer.id = *id;
    if (object.contains("path")) {
        auto path = requireRelativePath(object, location, "path");
        if (!path) {
            return Failure(path.error());
        }
        buffer.path = std::move(*path);
    }
    if (object.contains("viewMode")) {
        const auto mode = requireString(object, location, "viewMode");
        if (!mode) {
            return Failure(mode.error());
        }
        if (*mode == QStringLiteral("writing")) {
            buffer.viewMode = ViewMode::Writing;
        } else if (*mode == QStringLiteral("reading")) {
            buffer.viewMode = ViewMode::Reading;
        } else {
            return invalid(join(location, "viewMode"),
                           QStringLiteral("Expected \"writing\" or \"reading\""));
        }
    }
    if (object.contains("cursor")) {
        const auto cursorLocation = join(location, "cursor");
        const auto cursor = requireObject(object, location, "cursor");
        if (!cursor) {
            return Failure(cursor.error());
        }
        if (const auto keys = onlyKeys(*cursor, cursorLocation, {"line", "column"}); !keys) {
            return Failure(keys.error());
        }
        const auto line = requireInt(*cursor, cursorLocation, "line", {0, kSessionMaxPosition});
        if (!line) {
            return Failure(line.error());
        }
        const auto column = requireInt(*cursor, cursorLocation, "column", {0, kSessionMaxPosition});
        if (!column) {
            return Failure(column.error());
        }
        buffer.cursor = {*line, *column};
    }
    const auto scroll = optionalInt(object, location, "scrollLine", {0, kSessionMaxPosition}, 0);
    if (!scroll) {
        return Failure(scroll.error());
    }
    buffer.scrollLine = *scroll;
    const auto modified = optionalBool(object, location, "modified", false);
    if (!modified) {
        return Failure(modified.error());
    }
    buffer.modified = *modified;
    if (object.contains("recovery")) {
        const auto recovery = requireUuid(object, location, "recovery");
        if (!recovery) {
            return Failure(recovery.error());
        }
        buffer.recovery = *recovery;
    }
    if (buffer.modified != buffer.recovery.has_value()) {
        return invalid(
            join(location, "recovery"),
            QStringLiteral("A buffer is modified exactly when it has a recovery record"));
    }
    return buffer;
}

QJsonObject toJson(const WindowSnapshot& window) {
    return {{"width", window.width}, {"height", window.height}, {"maximized", window.maximized}};
}

QString pathText(const std::filesystem::path& path) {
    return QString::fromStdString(path.generic_string());
}

QJsonObject toJson(const SidebarSnapshot& sidebar) {
    QJsonObject object{{"visible", sidebar.visible}, {"width", sidebar.width}};
    if (sidebar.selectedPath) {
        object.insert("selectedPath", pathText(*sidebar.selectedPath));
    }
    return object;
}

QJsonObject toJson(const BufferSnapshot& buffer) {
    QJsonObject object{
        {"id", buffer.id.toString(QUuid::WithoutBraces)},
        {"viewMode", buffer.viewMode == ViewMode::Reading ? "reading" : "writing"},
        {"cursor", QJsonObject{{"line", buffer.cursor.line}, {"column", buffer.cursor.column}}},
        {"scrollLine", buffer.scrollLine},
        {"modified", buffer.modified}};
    if (buffer.path) {
        object.insert("path", pathText(*buffer.path));
    }
    if (buffer.recovery) {
        object.insert("recovery", buffer.recovery->toString(QUuid::WithoutBraces));
    }
    return object;
}

} // namespace

std::size_t SessionSnapshot::dirtyBufferCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        buffers, [](const BufferSnapshot& buffer) { return buffer.modified; }));
}

QString SessionError::describe() const {
    if (location.isEmpty()) {
        return message;
    }
    return QStringLiteral("%1: %2").arg(location, message);
}

QByteArray serializeSessionSnapshot(const SessionSnapshot& snapshot) {
    QJsonArray buffers;
    for (const auto& buffer : snapshot.buffers) {
        buffers.append(toJson(buffer));
    }
    QJsonObject object{{"version", static_cast<qint64>(snapshot.version)},
                       {"workspaceRoot", pathText(snapshot.workspaceRoot)},
                       {"window", toJson(snapshot.window)},
                       {"sidebar", toJson(snapshot.sidebar)},
                       {"buffers", buffers}};
    if (snapshot.activeBuffer) {
        object.insert("activeBuffer", snapshot.activeBuffer->toString(QUuid::WithoutBraces));
    }
    // QJsonObject keeps keys sorted, so the indented form is canonical.
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

std::expected<SessionSnapshot, SessionError> parseSessionSnapshot(const QByteArray& bytes) {
    if (static_cast<std::size_t>(bytes.size()) > kSessionMaxBytes) {
        return fail(SessionErrorCode::Oversized, {},
                    QStringLiteral("Session document exceeds %1 bytes").arg(kSessionMaxBytes));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(SessionErrorCode::Malformed, QStringLiteral("byte %1").arg(parseError.offset),
                    parseError.errorString());
    }
    if (!document.isObject()) {
        return fail(SessionErrorCode::Malformed, {}, QStringLiteral("Expected a JSON object"));
    }
    const auto object = document.object();

    // Version first, and alone: a future document may legitimately contain
    // fields this build has never heard of, and it deserves the version
    // diagnostic rather than an "unknown field" one.
    const auto version = requireInt(object, {}, "version", {0, std::numeric_limits<int>::max()});
    if (!version) {
        return Failure(version.error());
    }
    if (static_cast<std::uint32_t>(*version) > kSessionFormatVersion) {
        return fail(
            SessionErrorCode::FutureVersion, "version",
            QStringLiteral("Written by a newer Omanotes (format %1; this build reads up to %2)")
                .arg(*version)
                .arg(kSessionFormatVersion));
    }
    if (static_cast<std::uint32_t>(*version) < kOldestSessionFormatVersion) {
        return fail(SessionErrorCode::UnsupportedVersion, "version",
                    QStringLiteral("Format %1 is no longer supported (oldest readable is %2)")
                        .arg(*version)
                        .arg(kOldestSessionFormatVersion));
    }

    if (const auto keys =
            onlyKeys(object, {},
                     {"version", "workspaceRoot", "window", "sidebar", "buffers", "activeBuffer"});
        !keys) {
        return Failure(keys.error());
    }

    SessionSnapshot snapshot;
    snapshot.version = static_cast<std::uint32_t>(*version);

    const auto rootText = requireString(object, {}, "workspaceRoot");
    if (!rootText) {
        return Failure(rootText.error());
    }
    const auto rootBytes = rootText->toUtf8();
    if (rootBytes.isEmpty() || static_cast<std::size_t>(rootBytes.size()) > kSessionMaxPathBytes ||
        rootBytes.contains('\0')) {
        return invalid(
            "workspaceRoot",
            QStringLiteral("Expected a path of 1 to %1 bytes").arg(kSessionMaxPathBytes));
    }
    snapshot.workspaceRoot = std::filesystem::path(rootBytes.toStdString());
    if (!snapshot.workspaceRoot.is_absolute() ||
        snapshot.workspaceRoot.lexically_normal() != snapshot.workspaceRoot) {
        return invalid("workspaceRoot", QStringLiteral("Expected an absolute path in normal form"));
    }

    const auto window = requireObject(object, {}, "window");
    if (!window) {
        return Failure(window.error());
    }
    auto parsedWindow = parseWindow(*window, "window");
    if (!parsedWindow) {
        return Failure(parsedWindow.error());
    }
    snapshot.window = *parsedWindow;

    const auto sidebar = requireObject(object, {}, "sidebar");
    if (!sidebar) {
        return Failure(sidebar.error());
    }
    auto parsedSidebar = parseSidebar(*sidebar, "sidebar");
    if (!parsedSidebar) {
        return Failure(parsedSidebar.error());
    }
    snapshot.sidebar = std::move(*parsedSidebar);

    const auto buffersValue = object.value("buffers");
    if (!buffersValue.isArray()) {
        return invalid("buffers", QStringLiteral("Expected an array"));
    }
    const auto buffers = buffersValue.toArray();
    if (static_cast<std::size_t>(buffers.size()) > kSessionMaxBuffers) {
        return invalid("buffers",
                       QStringLiteral("At most %1 buffers are restored").arg(kSessionMaxBuffers));
    }
    std::set<QUuid> seenIds;
    std::set<QUuid> seenRecoveries;
    snapshot.buffers.reserve(static_cast<std::size_t>(buffers.size()));
    for (qsizetype position = 0; position < buffers.size(); ++position) {
        const auto location = index("buffers", position);
        auto buffer = parseBuffer(buffers.at(position), location);
        if (!buffer) {
            return Failure(buffer.error());
        }
        if (!seenIds.insert(buffer->id).second) {
            return invalid(join(location, "id"), QStringLiteral("Duplicate buffer id"));
        }
        if (buffer->recovery && !seenRecoveries.insert(*buffer->recovery).second) {
            return invalid(join(location, "recovery"),
                           QStringLiteral("Recovery record referenced twice"));
        }
        snapshot.buffers.push_back(std::move(*buffer));
    }

    if (object.contains("activeBuffer")) {
        const auto active = requireUuid(object, {}, "activeBuffer");
        if (!active) {
            return Failure(active.error());
        }
        if (!seenIds.contains(*active)) {
            return invalid("activeBuffer", QStringLiteral("Not one of the listed buffers"));
        }
        snapshot.activeBuffer = *active;
    }
    return snapshot;
}

std::expected<SessionSnapshot, SessionError>
readSessionSnapshot(const std::filesystem::path& file) {
    const auto name = QString::fromStdString(file.string());
    const QFileInfo info(name);
    if (!info.isFile()) {
        return fail(SessionErrorCode::Unreadable, name, QStringLiteral("No session file"));
    }
    QFile handle(name);
    if (!handle.open(QIODevice::ReadOnly)) {
        return fail(SessionErrorCode::Unreadable, name, handle.errorString());
    }
    // Read one byte past the ceiling so an oversized file is recognised
    // without being loaded whole.
    const auto bytes = handle.read(static_cast<qint64>(kSessionMaxBytes) + 1);
    if (handle.error() != QFileDevice::NoError) {
        return fail(SessionErrorCode::Unreadable, name, handle.errorString());
    }
    auto parsed = parseSessionSnapshot(bytes);
    if (!parsed) {
        parsed.error().location = parsed.error().location.isEmpty()
                                      ? name
                                      : QStringLiteral("%1: %2").arg(name, parsed.error().location);
    }
    return parsed;
}

std::expected<void, SessionError> checkSessionRoot(const SessionSnapshot& snapshot,
                                                   const WorkspaceRoot& root) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(snapshot.workspaceRoot, error);
    if (error || canonical != root.path()) {
        return fail(SessionErrorCode::RootMismatch, "workspaceRoot",
                    QStringLiteral("Session belongs to %1, not this workspace")
                        .arg(pathText(snapshot.workspaceRoot)));
    }
    return {};
}

std::expected<std::filesystem::path, WorkspaceError>
resolveSessionPath(const WorkspaceRoot& root, const std::filesystem::path& relative) {
    if (relative.is_absolute()) {
        return std::unexpected(
            WorkspaceError{WorkspaceErrorCode::OutsideRoot,
                           "Session path must be relative: " + relative.string()});
    }
    return root.resolveFile(relative);
}

} // namespace omanotes
