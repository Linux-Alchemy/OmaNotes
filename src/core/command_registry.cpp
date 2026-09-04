#include "core/command_registry.hpp"

#include <QStringList>

#include <algorithm>
#include <ranges>
#include <set>
#include <utility>

namespace omanotes {

namespace {

QString normaliseSequence(QStringView sequence) {
    const auto keys = sequence.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return keys.join(QLatin1Char(' '));
}

bool isPrefixOf(const QString& shorter, const QString& longer) {
    return longer.size() > shorter.size() && longer.startsWith(shorter) &&
           longer.at(shorter.size()) == QLatin1Char(' ');
}

} // namespace

std::expected<void, CommandError> CommandRegistry::add(CommandDescriptor descriptor) {
    if (descriptor.id.isEmpty() || !descriptor.execute) {
        return std::unexpected(CommandError{CommandErrorCode::InvalidDescriptor,
                                            QStringLiteral("A command needs an id and an action")});
    }
    if (find(descriptor.id) != nullptr) {
        return std::unexpected(
            CommandError{CommandErrorCode::DuplicateCommand,
                         QStringLiteral("Command %1 is already registered").arg(descriptor.id)});
    }
    if (!descriptor.enabled) {
        descriptor.enabled = [](const AppContext&) { return true; };
    }
    commands_.push_back(std::move(descriptor));
    return {};
}

const CommandDescriptor* CommandRegistry::find(QStringView id) const {
    const auto found = std::ranges::find_if(
        commands_, [id](const CommandDescriptor& command) { return command.id == id; });
    return found == commands_.end() ? nullptr : &*found;
}

std::vector<const CommandDescriptor*> CommandRegistry::commands() const {
    std::vector<const CommandDescriptor*> result;
    result.reserve(commands_.size());
    for (const auto& command : commands_) {
        result.push_back(&command);
    }
    return result;
}

std::vector<const CommandDescriptor*> CommandRegistry::available(const AppContext& context) const {
    std::vector<const CommandDescriptor*> result;
    for (const auto& command : commands_) {
        if (command.enabled(context)) {
            result.push_back(&command);
        }
    }
    return result;
}

std::expected<void, CommandError> CommandRegistry::execute(QStringView id,
                                                           AppContext& context) const {
    const auto* command = find(id);
    if (command == nullptr) {
        return std::unexpected(
            CommandError{CommandErrorCode::UnknownCommand,
                         QStringLiteral("Unknown command: %1").arg(id.toString())});
    }
    if (!command->enabled(context)) {
        const auto hint = command->disabledHint.isEmpty()
                              ? QStringLiteral("%1 is not available here").arg(command->label)
                              : command->disabledHint;
        return std::unexpected(CommandError{CommandErrorCode::Disabled, hint});
    }
    command->execute(context);
    return {};
}

std::expected<void, CommandError> CommandRegistry::bind(const LeaderSequence& sequence,
                                                        QStringView commandId) {
    const auto keys = normaliseSequence(sequence.keys);
    if (keys.isEmpty()) {
        return std::unexpected(CommandError{CommandErrorCode::InvalidDescriptor,
                                            QStringLiteral("A binding needs at least one key")});
    }
    if (find(commandId) == nullptr) {
        return std::unexpected(CommandError{
            CommandErrorCode::UnknownCommand,
            QStringLiteral("Cannot bind %1: no such command").arg(commandId.toString())});
    }
    if (const auto existing = bindings_.find(keys); existing != bindings_.end()) {
        return std::unexpected(
            CommandError{CommandErrorCode::DuplicateSequence,
                         QStringLiteral("%1 is already bound to %2").arg(keys, existing->second)});
    }
    for (const auto& [bound, id] : bindings_) {
        if (isPrefixOf(keys, bound) || isPrefixOf(bound, keys)) {
            return std::unexpected(CommandError{
                CommandErrorCode::ShadowedSequence,
                QStringLiteral("%1 and %2 (%3) cannot both be typed").arg(keys, bound, id)});
        }
    }
    bindings_.emplace(keys, commandId.toString());
    return {};
}

SequenceLookup CommandRegistry::lookup(QStringView sequence) const {
    const auto keys = normaliseSequence(sequence);
    if (const auto exact = bindings_.find(keys); exact != bindings_.end()) {
        return {SequenceMatch::Exact, exact->second};
    }
    for (const auto& bound : std::views::keys(bindings_)) {
        if (isPrefixOf(keys, bound)) {
            return {SequenceMatch::Prefix, {}};
        }
    }
    return {SequenceMatch::None, {}};
}

std::vector<QString> CommandRegistry::sequencesFor(QStringView commandId) const {
    std::vector<QString> result;
    for (const auto& [sequence, id] : bindings_) {
        if (id == commandId) {
            result.push_back(sequence);
        }
    }
    return result;
}

const std::map<QString, QString>& CommandRegistry::bindings() const noexcept { return bindings_; }

std::vector<AuditFinding>
CommandRegistry::audit(const std::vector<QString>& reachableElsewhere) const {
    std::vector<AuditFinding> findings;
    std::set<QString> bound;
    for (const auto& id : std::views::values(bindings_)) {
        bound.insert(id);
    }
    for (const auto& id : reachableElsewhere) {
        if (find(id) == nullptr) {
            findings.push_back(
                {id, QStringLiteral("a route names a command that is not registered")});
        }
        bound.insert(id);
    }

    std::set<std::pair<QString, QString>> labels;
    for (const auto& command : commands_) {
        if (!bound.contains(command.id)) {
            findings.push_back(
                {command.id, QStringLiteral("no keyboard or mouse route reaches it")});
        }
        if (!labels.emplace(command.category, command.label).second) {
            findings.push_back({command.id, QStringLiteral("label \"%1\" repeats within %2")
                                                .arg(command.label, command.category)});
        }
    }
    return findings;
}

} // namespace omanotes
