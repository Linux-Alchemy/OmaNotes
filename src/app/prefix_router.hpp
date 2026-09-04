#ifndef OMANOTES_APP_PREFIX_ROUTER_HPP
#define OMANOTES_APP_PREFIX_ROUTER_HPP

#include "core/command_registry.hpp"
#include "editor/editor_adapter.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

class QKeyEvent;

namespace omanotes {

enum class LeaderKey : std::uint8_t { Space, ControlB };

/// Turns a leader key and the keys typed after it into a command id.
///
/// The router owns no bindings. It asks a resolver, normally the command
/// registry, whether the keys so far are a complete sequence, the start of
/// one, or nothing at all, and reports each step on the status line so a
/// half-typed sequence is never a silent wait.
class PrefixRouter final : public QObject {
    Q_OBJECT

  public:
    using Resolver = std::function<SequenceLookup(const QString& sequence)>;

    explicit PrefixRouter(LeaderKey leader, QObject* parent = nullptr);

    void setResolver(Resolver resolver);
    [[nodiscard]] bool route(QKeyEvent& event, EditorMode mode);
    void cancelPending();
    [[nodiscard]] bool isPending() const noexcept;
    [[nodiscard]] LeaderKey leader() const noexcept;
    [[nodiscard]] QString leaderName() const;

  signals:
    void feedbackChanged(const QString& message);
    /// A complete sequence was typed. `sequence` is the display form
    /// (`Space+b+d`) for messages; `commandId` names what to run.
    void sequenceAccepted(const QString& commandId, const QString& sequence);

  private:
    [[nodiscard]] bool matchesLeader(const QKeyEvent& event) const noexcept;
    [[nodiscard]] QString displaySequence() const;
    void cancel(const QString& feedback);

    LeaderKey leader_;
    Resolver resolver_;
    bool pending_ = false;
    QStringList keys_;
};

} // namespace omanotes

#endif // OMANOTES_APP_PREFIX_ROUTER_HPP
