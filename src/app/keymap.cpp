#include "app/keymap.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <set>

namespace omanotes {
namespace {
bool takesArguments(QStringView id) {
    return id == QStringLiteral("file.open") || id == QStringLiteral("buffer.show");
}

bool validLeaderSequence(const QString& sequence) {
    const auto keys = sequence.split(QLatin1Char(' '));
    if (keys.empty() || keys.size() > 4) {
        return false;
    }
    return std::ranges::all_of(keys, [](const QString& key) {
        return key == QStringLiteral("Space") ||
               (key.size() == 1 && key.front().unicode() >= 33 && key.front().unicode() <= 126);
    });
}

bool safeShortcut(const QKeySequence& sequence, QStringView command) {
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown) {
        return false;
    }
    const auto key = sequence[0].key();
    const auto modifiers = sequence[0].keyboardModifiers();
    // These existing application routes have deliberately documented Vim
    // meanings. Their exceptions must not become permission to steal other
    // canonical editor keys.
    if (modifiers == Qt::ShiftModifier && (key == Qt::Key_H || key == Qt::Key_L)) {
        return command == QStringLiteral("buffer.next") ||
               command == QStringLiteral("buffer.previous");
    }
    if (modifiers == Qt::ControlModifier && key == Qt::Key_H) {
        return command == QStringLiteral("pane.sidebar");
    }
    if (modifiers == Qt::ControlModifier && key == Qt::Key_L) {
        return command == QStringLiteral("pane.editor");
    }
    if ((modifiers == Qt::ControlModifier ||
         modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) &&
        key == Qt::Key_S) {
        return command == QStringLiteral("file.save");
    }
    // No plain typing keys, Vim Ctrl commands, desktop Super bindings, or
    // dialog accelerators. Real editor QAction conflicts are checked too.
    return (modifiers == (Qt::ControlModifier | Qt::AltModifier) ||
            modifiers == (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) &&
           ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9));
}

std::unexpected<KeymapError> failure(const QString& location, const QString& message) {
    return std::unexpected(KeymapError{location, message});
}
} // namespace

Keymap Keymap::defaults(const CommandRegistry& commands) {
    Keymap result;
    result.leaderBindings_ = commands.bindings();
    result.shortcuts_ = {
        {QStringLiteral("file.save"), QKeySequence(QStringLiteral("Ctrl+S"))},
        {QStringLiteral("edit.paste"), QKeySequence(QStringLiteral("Ctrl+V"))},
        {QStringLiteral("edit.copy"), QKeySequence(QStringLiteral("Ctrl+C"))},
        {QStringLiteral("editor.visual-block"), QKeySequence(QStringLiteral("Ctrl+Q"))},
        {QStringLiteral("view.half-page-down"), QKeySequence(QStringLiteral("Ctrl+D"))},
        {QStringLiteral("view.half-page-up"), QKeySequence(QStringLiteral("Ctrl+U"))},
        {QStringLiteral("pane.sidebar"), QKeySequence(QStringLiteral("Ctrl+H"))},
        {QStringLiteral("pane.editor"), QKeySequence(QStringLiteral("Ctrl+L"))},
        {QStringLiteral("buffer.next"), QKeySequence(QStringLiteral("Shift+L"))},
        {QStringLiteral("buffer.previous"), QKeySequence(QStringLiteral("Shift+H"))}};
    return result;
}

std::expected<Keymap, KeymapError>
Keymap::fromConfig(const QVariantMap& values, const CommandRegistry& commands,
                   const std::vector<QKeySequence>& editorShortcuts) {
    auto candidate = defaults(commands);
    for (auto field = values.cbegin(); field != values.cend(); ++field) {
        if (field.key() != QStringLiteral("leaderBindings") &&
            field.key() != QStringLiteral("shortcuts")) {
            return failure(field.key(), QStringLiteral("Unknown configuration field"));
        }
        if (field.value().metaType().id() != QMetaType::QVariantMap) {
            return failure(field.key(),
                           QStringLiteral("Expected an object mapping command ids to keys"));
        }
    }
    const auto leaders = values.value(QStringLiteral("leaderBindings")).toMap();
    // Remove every overridden command first, so valid swaps are independent
    // of JSON object order. The new complete map is validated below.
    for (auto item = leaders.cbegin(); item != leaders.cend(); ++item) {
        std::erase_if(candidate.leaderBindings_,
                      [&item](const auto& binding) { return binding.second == item.key(); });
    }
    for (auto item = leaders.cbegin(); item != leaders.cend(); ++item) {
        const auto location = QStringLiteral("leaderBindings.%1").arg(item.key());
        if (commands.find(item.key()) == nullptr || takesArguments(item.key())) {
            return failure(
                location,
                QStringLiteral("Unknown command or command requires a mouse-supplied target"));
        }
        if (item.value().metaType().id() != QMetaType::QVariantList ||
            item.value().toList().isEmpty() || item.value().toList().size() > 8) {
            return failure(location,
                           QStringLiteral("Expected an array of 1 to 8 leader sequences"));
        }
        const auto sequences = item.value().toList();
        for (qsizetype index = 0; index < sequences.size(); ++index) {
            const auto at = QStringLiteral("%1[%2]").arg(location).arg(index);
            if (sequences[index].metaType().id() != QMetaType::QString ||
                !validLeaderSequence(sequences[index].toString())) {
                return failure(at, QStringLiteral("Use 1 to 4 printable ASCII keys separated by "
                                                  "spaces; spell the space key Space"));
            }
            if (!candidate.leaderBindings_.emplace(sequences[index].toString(), item.key())
                     .second) {
                return failure(at, QStringLiteral("Leader sequence is already bound"));
            }
        }
    }
    const auto help = candidate.leaderBindings_.find(QStringLiteral("?"));
    if (help == candidate.leaderBindings_.end() || help->second != QStringLiteral("help.show")) {
        return failure(QStringLiteral("leaderBindings.help.show"),
                       QStringLiteral("Space ? must remain bound to help.show"));
    }
    const auto shortcuts = values.value(QStringLiteral("shortcuts")).toMap();
    for (auto item = shortcuts.cbegin(); item != shortcuts.cend(); ++item) {
        const auto location = QStringLiteral("shortcuts.%1").arg(item.key());
        if (commands.find(item.key()) == nullptr || takesArguments(item.key())) {
            return failure(
                location,
                QStringLiteral("Unknown command or command requires a mouse-supplied target"));
        }
        if (item.value().metaType().id() != QMetaType::QString) {
            return failure(location, QStringLiteral("Expected one direct key sequence"));
        }
        const auto sequence =
            QKeySequence::fromString(item.value().toString(), QKeySequence::PortableText);
        if (!safeShortcut(sequence, item.key())) {
            return failure(location, QStringLiteral("Use Ctrl+Alt+[Shift+]letter/digit, or an "
                                                    "existing documented application shortcut"));
        }
        if (std::ranges::any_of(editorShortcuts, [&sequence](const QKeySequence& bound) {
                return !bound.isEmpty() && (sequence.matches(bound) != QKeySequence::NoMatch ||
                                            bound.matches(sequence) != QKeySequence::NoMatch);
            })) {
            return failure(location, QStringLiteral("This sequence is already used by the editor"));
        }
        candidate.shortcuts_[item.key()] = sequence;
    }
    std::set<QKeySequence> seen;
    for (const auto& [id, sequence] : candidate.shortcuts_) {
        if (!seen.insert(sequence).second) {
            return failure(QStringLiteral("shortcuts.%1").arg(id),
                           QStringLiteral("Direct shortcut is already bound"));
        }
    }
    auto probe = commands;
    if (const auto valid = candidate.applyTo(probe); !valid) {
        return std::unexpected(valid.error());
    }
    return candidate;
}

std::expected<void, KeymapError> Keymap::applyTo(CommandRegistry& commands) const {
    CommandRegistry candidate;
    for (const auto* command : commands.commands()) {
        if (const auto added = candidate.add(*command); !added) {
            return failure(command->id, added.error().message);
        }
    }
    for (const auto& [keys, id] : leaderBindings_) {
        if (const auto bound = candidate.bind(LeaderSequence{keys}, id); !bound) {
            return failure(QStringLiteral("leaderBindings.%1").arg(id), bound.error().message);
        }
    }
    commands = std::move(candidate);
    return {};
}

QString Keymap::configurationPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/omanotes/keymap.json");
}

std::expected<Keymap, KeymapError> Keymap::load(const QString& path,
                                                const CommandRegistry& commands,
                                                const std::vector<QKeySequence>& editorShortcuts) {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return defaults(commands);
    }
    QFile file(path);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
        return failure(path, QStringLiteral("Cannot read keymap file"));
    }
    const auto bytes = file.read(65537);
    if (bytes.size() > 65536 || file.error() != QFileDevice::NoError) {
        return failure(path, QStringLiteral("Keymap must be readable and at most 64 KiB"));
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        return failure(QStringLiteral("%1: byte %2").arg(path).arg(error.offset),
                       error.errorString());
    }
    if (!document.isObject()) {
        return failure(path, QStringLiteral("Expected a JSON object"));
    }
    auto result = fromConfig(document.object().toVariantMap(), commands, editorShortcuts);
    if (!result) {
        result.error().location = path + QStringLiteral(": ") + result.error().location;
    }
    return result;
}

QKeySequence Keymap::sequenceFor(QStringView commandId) const {
    const auto found = shortcuts_.find(commandId.toString());
    return found == shortcuts_.end() ? QKeySequence{} : found->second;
}
QString Keymap::commandFor(const QKeySequence& sequence) const {
    for (const auto& [id, bound] : shortcuts_) {
        if (bound == sequence) {
            return id;
        }
    }
    return {};
}
std::map<QString, QString> Keymap::shortcutLabels() const {
    std::map<QString, QString> labels;
    for (const auto& [id, sequence] : shortcuts_) {
        auto label = sequence.toString(QKeySequence::NativeText);
        // Qt prints every letter as a capital, which reads as Shift being
        // part of the chord. Ctrl+s is the unshifted key; Ctrl+Shift+S and
        // Shift+L keep their capital because Shift really is pressed.
        if (!label.isEmpty() && sequence.count() == 1 &&
            !(sequence[0].keyboardModifiers() & Qt::ShiftModifier) && label.back().isLetter()) {
            label.back() = label.back().toLower();
        }
        if (id == QStringLiteral("pane.editor") &&
            sequence == QKeySequence(QStringLiteral("Ctrl+L"))) {
            label += QStringLiteral(" (sidebar)");
        }
        labels.emplace(id, label);
    }
    return labels;
}
} // namespace omanotes
