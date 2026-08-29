#ifndef OMANOTES_APP_PREFIX_ROUTER_HPP
#define OMANOTES_APP_PREFIX_ROUTER_HPP

#include <QObject>
#include <QString>

class QKeyEvent;

namespace omanotes {

enum class EditorMode { Normal, Insert, Visual, Replace, Other };
enum class LeaderKey { Space, ControlB };

class PrefixRouter final : public QObject {
    Q_OBJECT

  public:
    explicit PrefixRouter(LeaderKey leader, QObject* parent = nullptr);

    [[nodiscard]] bool route(QKeyEvent& event, EditorMode mode);
    [[nodiscard]] bool isPending() const noexcept;
    [[nodiscard]] LeaderKey leader() const noexcept;

  signals:
    void feedbackChanged(const QString& message);
    void sequenceAccepted(const QString& sequence);

  private:
    [[nodiscard]] bool matchesLeader(const QKeyEvent& event) const noexcept;
    [[nodiscard]] QString leaderName() const;
    void cancel(const QString& feedback);

    LeaderKey leader_;
    bool pending_ = false;
};

} // namespace omanotes

#endif // OMANOTES_APP_PREFIX_ROUTER_HPP
