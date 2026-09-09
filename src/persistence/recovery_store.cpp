#include "persistence/recovery_store.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

constexpr mode_t kDirectoryMode = 0700;
constexpr mode_t kFileMode = 0600;
constexpr auto kSuffix = ".json";
/// The whole document: contents plus a little JSON around it.
constexpr std::size_t kRecoveryMaxDocumentBytes = kRecoveryMaxContentBytes + std::size_t{16} * 1024;

using Failure = std::unexpected<RecoveryError>;

Failure fail(RecoveryErrorCode code, QString location, QString message) {
    return Failure(RecoveryError{code, std::move(location), std::move(message)});
}

Failure invalid(const QString& location, QString message) {
    return fail(RecoveryErrorCode::InvalidField, location, std::move(message));
}

QString text(const std::filesystem::path& path) { return QString::fromStdString(path.string()); }

Failure storeFailure(const std::filesystem::path& where, QString message) {
    return fail(RecoveryErrorCode::StoreFailed, text(where), std::move(message));
}

Failure storeFailure(const std::filesystem::path& where, const char* action, int errorNumber) {
    return storeFailure(
        where, QStringLiteral("%1: %2").arg(QString::fromLatin1(action),
                                            QString::fromLocal8Bit(std::strerror(errorNumber))));
}

bool isSymlink(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, error)) && !error;
}

std::expected<void, RecoveryError> ensureDirectory(const std::filesystem::path& directory) {
    if (isSymlink(directory)) {
        return storeFailure(directory, QStringLiteral("Recovery directory is a symlink; refusing"));
    }
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (::mkdir(directory.c_str(), kDirectoryMode) != 0 && errno != EEXIST) {
            return storeFailure(directory, "Could not create the recovery directory", errno);
        }
    }
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0) {
        return storeFailure(directory, "Could not inspect the recovery directory", errno);
    }
    if (!S_ISDIR(status.st_mode)) {
        return storeFailure(directory, QStringLiteral("Recovery path is not a directory"));
    }
    if ((status.st_mode & 07777) != kDirectoryMode &&
        ::chmod(directory.c_str(), kDirectoryMode) != 0) {
        return storeFailure(directory, "Could not make the recovery directory owner-only", errno);
    }
    return {};
}

/// Same rules as snapshot paths: relative, normal, no dot components.
std::expected<std::filesystem::path, RecoveryError> relativePath(const QByteArray& encoded,
                                                                 const QString& location) {
    if (encoded.isEmpty() || static_cast<std::size_t>(encoded.size()) > kSessionMaxPathBytes ||
        encoded.contains('\0')) {
        return invalid(location, QStringLiteral("Expected a path of 1 to %1 bytes without NUL")
                                     .arg(kSessionMaxPathBytes));
    }
    std::filesystem::path path(encoded.toStdString());
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return invalid(location, QStringLiteral("Path must be relative to the workspace"));
    }
    if (path.lexically_normal() != path || path.filename().empty() ||
        std::ranges::any_of(path, [](const auto& part) { return part == ".." || part == "."; })) {
        return invalid(location, QStringLiteral("Path must be in normal form without . or .."));
    }
    return path;
}

std::optional<RecoveryId> idFromFileName(const std::filesystem::path& file) {
    if (file.extension() != kSuffix) {
        return std::nullopt;
    }
    const auto id = QUuid::fromString(QString::fromStdString(file.stem().string()));
    if (id.isNull() || file.stem().string() != id.toString(QUuid::WithoutBraces).toStdString()) {
        return std::nullopt;
    }
    return id;
}

bool lexicallyInside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();
    while (rootPart != root.end() && candidatePart != candidate.end() &&
           *rootPart == *candidatePart) {
        ++rootPart;
        ++candidatePart;
    }
    return rootPart == root.end() && candidatePart != candidate.end();
}

} // namespace

QString RecoveryError::describe() const {
    return location.isEmpty() ? message : QStringLiteral("%1: %2").arg(location, message);
}

QByteArray serializeBufferRecovery(const BufferRecovery& state) {
    QJsonObject object{{"version", static_cast<qint64>(kRecoveryFormatVersion)},
                       {"contents", state.contents}};
    if (state.path) {
        object.insert("path", QString::fromStdString(state.path->generic_string()));
    }
    if (state.baseRevision) {
        object.insert("baseDigest", QString::fromLatin1(state.baseRevision->contentHash.toHex()));
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

std::expected<BufferRecovery, RecoveryError> parseBufferRecovery(const QByteArray& bytes) {
    if (static_cast<std::size_t>(bytes.size()) > kRecoveryMaxDocumentBytes) {
        return fail(
            RecoveryErrorCode::Oversized, {},
            QStringLiteral("Recovery record exceeds %1 bytes").arg(kRecoveryMaxDocumentBytes));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(RecoveryErrorCode::Malformed, QStringLiteral("byte %1").arg(parseError.offset),
                    parseError.errorString());
    }
    if (!document.isObject()) {
        return fail(RecoveryErrorCode::Malformed, {}, QStringLiteral("Expected a JSON object"));
    }
    const auto object = document.object();

    const auto versionValue = object.value("version");
    const auto version = versionValue.toDouble(-1);
    if (!versionValue.isDouble() || version < 0 || version != std::floor(version) ||
        version > std::numeric_limits<int>::max()) {
        return invalid("version", QStringLiteral("Expected a whole number"));
    }
    if (version > kRecoveryFormatVersion) {
        return fail(
            RecoveryErrorCode::FutureVersion, "version",
            QStringLiteral("Written by a newer Omanotes (format %1; this build reads up to %2)")
                .arg(static_cast<int>(version))
                .arg(kRecoveryFormatVersion));
    }
    if (version < 1) {
        return fail(RecoveryErrorCode::UnsupportedVersion, "version",
                    QStringLiteral("Format %1 is not supported").arg(static_cast<int>(version)));
    }
    for (auto item = object.constBegin(); item != object.constEnd(); ++item) {
        if (item.key() != "version" && item.key() != "contents" && item.key() != "path" &&
            item.key() != "baseDigest") {
            return invalid(item.key(), QStringLiteral("Unknown field"));
        }
    }

    BufferRecovery state;
    const auto contents = object.value("contents");
    if (!contents.isString()) {
        return invalid("contents", QStringLiteral("Expected a string"));
    }
    state.contents = contents.toString();
    if (static_cast<std::size_t>(state.contents.toUtf8().size()) > kRecoveryMaxContentBytes) {
        return fail(RecoveryErrorCode::Oversized, "contents",
                    QStringLiteral("Contents exceed %1 bytes").arg(kRecoveryMaxContentBytes));
    }
    if (object.contains("path")) {
        const auto value = object.value("path");
        if (!value.isString()) {
            return invalid("path", QStringLiteral("Expected a string"));
        }
        auto path = relativePath(value.toString().toUtf8(), "path");
        if (!path) {
            return Failure(path.error());
        }
        state.path = std::move(*path);
    }
    if (object.contains("baseDigest")) {
        const auto value = object.value("baseDigest");
        const auto hex = value.toString().toLatin1();
        const auto digest = QByteArray::fromHex(hex);
        if (!value.isString() || hex.size() != 64 || digest.size() != 32 ||
            digest.toHex() != hex.toLower()) {
            return invalid("baseDigest", QStringLiteral("Expected 64 hex characters"));
        }
        state.baseRevision = SavedRevision{digest};
    }
    return state;
}

RecoveryStore::RecoveryStore(std::filesystem::path directory, const AtomicWriteFaults* faults)
    : directory_(std::move(directory)), faults_(faults) {}

std::filesystem::path
RecoveryStore::directoryBeside(const std::filesystem::path& sessionDirectory) {
    return sessionDirectory / "recovery";
}

const std::filesystem::path& RecoveryStore::directory() const noexcept { return directory_; }

std::expected<std::filesystem::path, RecoveryError> RecoveryStore::fileFor(RecoveryId id) const {
    // The id is the only thing that becomes a file name, and only in this
    // directory: a validated UUID cannot carry a separator or a dot-dot.
    if (id.isNull()) {
        return fail(RecoveryErrorCode::InvalidId, {}, QStringLiteral("Recovery id is null"));
    }
    return directory_ / (id.toString(QUuid::WithoutBraces).toStdString() + kSuffix);
}

std::expected<RecoveryId, RecoveryError>
RecoveryStore::checkpoint(const BufferRecovery& state, std::optional<RecoveryId> existing) const {
    if (static_cast<std::size_t>(state.contents.toUtf8().size()) > kRecoveryMaxContentBytes) {
        return fail(RecoveryErrorCode::Oversized, "contents",
                    QStringLiteral("Buffer is larger than %1 bytes and will not be checkpointed")
                        .arg(kRecoveryMaxContentBytes));
    }
    if (state.path) {
        if (auto checked =
                relativePath(QByteArray::fromStdString(state.path->generic_string()), "path");
            !checked) {
            return Failure(checked.error());
        }
    }
    const auto id = existing.value_or(QUuid::createUuid());
    auto file = fileFor(id);
    if (!file) {
        return Failure(file.error());
    }
    if (auto ensured = ensureDirectory(directory_); !ensured) {
        return Failure(ensured.error());
    }
    if (isSymlink(*file)) {
        return storeFailure(*file,
                            QStringLiteral("Recovery record is a symlink; refusing to write"));
    }
    const auto bytes = serializeBufferRecovery(state);
    if (auto replaced = replaceFileAtomically(*file, QByteArrayView(bytes), kFileMode, faults_);
        !replaced) {
        return storeFailure(*file, QString::fromStdString(replaced.error().message));
    }
    return id;
}

std::expected<std::optional<BufferRecovery>, RecoveryError>
RecoveryStore::load(RecoveryId id) const {
    const auto file = fileFor(id);
    if (!file) {
        return Failure(file.error());
    }
    if (isSymlink(*file)) {
        return storeFailure(*file,
                            QStringLiteral("Recovery record is a symlink; refusing to read"));
    }
    const auto name = text(*file);
    const QFileInfo info(name);
    if (!info.exists()) {
        return std::optional<BufferRecovery>{};
    }
    QFile handle(name);
    if (!info.isFile() || !handle.open(QIODevice::ReadOnly)) {
        return storeFailure(*file, QStringLiteral("Cannot read recovery record"));
    }
    const auto bytes = handle.read(static_cast<qint64>(kRecoveryMaxDocumentBytes) + 1);
    if (handle.error() != QFileDevice::NoError) {
        return storeFailure(*file, handle.errorString());
    }
    auto parsed = parseBufferRecovery(bytes);
    if (!parsed) {
        parsed.error().location = parsed.error().location.isEmpty()
                                      ? name
                                      : QStringLiteral("%1: %2").arg(name, parsed.error().location);
        return Failure(parsed.error());
    }
    return std::optional<BufferRecovery>{std::move(*parsed)};
}

std::expected<void, RecoveryError> RecoveryStore::remove(RecoveryId id) const {
    const auto file = fileFor(id);
    if (!file) {
        return Failure(file.error());
    }
    if (isSymlink(*file)) {
        // Removing the link itself is harmless, but a link here is a sign of
        // tampering worth reporting rather than tidying away.
        return storeFailure(*file, QStringLiteral("Recovery record is a symlink; refusing"));
    }
    std::error_code error;
    std::filesystem::remove(*file, error);
    if (error) {
        return storeFailure(*file, QStringLiteral("Could not remove recovery record: %1")
                                       .arg(QString::fromStdString(error.message())));
    }
    return {};
}

std::expected<std::vector<RecoveryId>, RecoveryError> RecoveryStore::list() const {
    std::vector<RecoveryId> ids;
    std::error_code error;
    if (!std::filesystem::exists(directory_, error)) {
        return ids;
    }
    if (isSymlink(directory_)) {
        return storeFailure(directory_,
                            QStringLiteral("Recovery directory is a symlink; refusing"));
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        std::error_code ignored;
        if (!entry.is_regular_file(ignored) || entry.is_symlink(ignored)) {
            continue;
        }
        if (const auto id = idFromFileName(entry.path()); id) {
            ids.push_back(*id);
        }
    }
    if (error) {
        return storeFailure(directory_, QStringLiteral("Could not list recovery records: %1")
                                            .arg(QString::fromStdString(error.message())));
    }
    return ids;
}

std::expected<std::size_t, RecoveryError>
RecoveryStore::removeAllExcept(std::span<const RecoveryId> live) const {
    const auto present = list();
    if (!present) {
        return Failure(present.error());
    }
    std::size_t removed = 0;
    for (const auto& id : *present) {
        if (std::ranges::find(live, id) != live.end()) {
            continue;
        }
        if (auto gone = remove(id); !gone) {
            return Failure(gone.error());
        }
        ++removed;
    }
    return removed;
}

std::expected<RecoveryPlan, WorkspaceError> planRecovery(const BufferRecovery& state,
                                                         const WorkspaceRoot& root) {
    if (!state.path) {
        return RecoveryPlan{RecoveryTarget::Scratch, std::nullopt};
    }
    auto resolved = resolveSessionPath(root, *state.path);
    if (!resolved) {
        if (resolved.error().code != WorkspaceErrorCode::Missing) {
            return std::unexpected(resolved.error());
        }
        // Gone, or never saved: the note's place inside the root is still
        // well defined, and saving will create it there.
        const auto joined = root.path() / *state.path;
        std::error_code error;
        // The file is absent, so canonical() cannot be used; weakly_canonical
        // resolves every existing prefix (including a symlinked directory)
        // and the rest is compared lexically against the root.
        const auto resolvedTarget = std::filesystem::weakly_canonical(joined, error);
        if (error || !lexicallyInside(root.path(), resolvedTarget)) {
            return std::unexpected(
                WorkspaceError{WorkspaceErrorCode::OutsideRoot,
                               "Recovery target is outside the workspace: " + joined.string()});
        }
        return RecoveryPlan{RecoveryTarget::FileMissing, resolvedTarget};
    }
    const auto disk = DiskRevision::read(*resolved);
    if (!state.baseRevision) {
        // The note did not exist at checkpoint time and does now.
        return RecoveryPlan{RecoveryTarget::FileChangedOnDisk, *resolved};
    }
    switch (classifyExternalChange(*state.baseRevision, disk, /*bufferModified=*/true)) {
    case ExternalChangeAction::Unchanged:
        return RecoveryPlan{RecoveryTarget::File, *resolved};
    case ExternalChangeAction::FileRemoved:
        return RecoveryPlan{RecoveryTarget::FileMissing, *resolved};
    case ExternalChangeAction::PromptConflict:
    case ExternalChangeAction::ReloadClean:
        return RecoveryPlan{RecoveryTarget::FileChangedOnDisk, *resolved};
    }
    return RecoveryPlan{RecoveryTarget::FileChangedOnDisk, *resolved};
}

} // namespace omanotes
