#ifndef OMANOTES_CORE_COMMAND_REGISTRY_HPP
#define OMANOTES_CORE_COMMAND_REGISTRY_HPP

#include "core/command.hpp"

#include <QString>
#include <QStringView>

#include <cstdint>
#include <expected>
#include <map>
#include <vector>

namespace omanotes {

enum class CommandErrorCode : std::uint8_t {
    UnknownCommand,
    Disabled,
    DuplicateCommand,
    DuplicateSequence,
    ShadowedSequence,
    InvalidDescriptor
};

struct CommandError {
    CommandErrorCode code;
    QString message;
};

/// The keys typed after the leader, space separated: `"b d"`. A distinct
/// type so a sequence and a command id can never be passed the wrong way
/// round.
struct LeaderSequence {
    QString keys;
};

/// How a typed leader sequence relates to the bound ones.
enum class SequenceMatch : std::uint8_t { None, Prefix, Exact };

struct SequenceLookup {
    SequenceMatch match{SequenceMatch::None};
    /// Set only for an exact match.
    QString commandId;
};

/// One thing the audit found worth a human's attention.
struct AuditFinding {
    QString commandId;
    QString detail;
};

/// The single table of application commands and the leader sequences that
/// reach them.
///
/// The registry runs nothing on its own: `execute` refuses unknown and
/// disabled commands and otherwise calls the descriptor. It never touches the
/// shell, configuration, or plugins; it only holds what the window registers.
class CommandRegistry final {
  public:
    /// Register a command. Refuses an empty id or execute, and an id already
    /// taken, so one action can never have two implementations.
    [[nodiscard]] std::expected<void, CommandError> add(CommandDescriptor descriptor);

    [[nodiscard]] const CommandDescriptor* find(QStringView id) const;
    [[nodiscard]] std::vector<const CommandDescriptor*> commands() const;
    /// Commands whose `enabled` accepts `context`, in registration order.
    [[nodiscard]] std::vector<const CommandDescriptor*> available(const AppContext& context) const;

    /// Run `id` against `context`. Unknown and disabled commands never run;
    /// the error message is fit to show the user.
    [[nodiscard]] std::expected<void, CommandError> execute(QStringView id,
                                                            AppContext& context) const;

    /// Bind the keys typed after the leader, space separated (`"b d"`), to a
    /// command. Refuses an unknown command, a sequence already bound, and a
    /// sequence that would be a prefix of another or be shadowed by one,
    /// because such a sequence could never be typed.
    [[nodiscard]] std::expected<void, CommandError> bind(const LeaderSequence& sequence,
                                                         QStringView commandId);
    [[nodiscard]] SequenceLookup lookup(QStringView sequence) const;
    /// The sequences bound to `commandId`, in binding order.
    [[nodiscard]] std::vector<QString> sequencesFor(QStringView commandId) const;
    [[nodiscard]] const std::map<QString, QString>& bindings() const noexcept;

    /// Report commands that cannot be reached by any leader sequence, and
    /// labels that repeat within a category. Empty means clean.
    [[nodiscard]] std::vector<AuditFinding>
    audit(const std::vector<QString>& reachableElsewhere) const;

  private:
    std::vector<CommandDescriptor> commands_;
    /// Leader sequence to command id.
    std::map<QString, QString> bindings_;
};

} // namespace omanotes

#endif // OMANOTES_CORE_COMMAND_REGISTRY_HPP
