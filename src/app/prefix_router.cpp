#include "app/prefix_router.hpp"

#include <QKeyEvent>

namespace omanotes {

PrefixRouter::PrefixRouter(LeaderKey leader, QObject* parent) : QObject(parent), leader_(leader) {}

bool PrefixRouter::route(QKeyEvent& event, EditorMode mode) {
    if (mode != EditorMode::Normal) {
        if (pending_) {
            cancel(QStringLiteral("Application prefix cancelled"));
        }
        return false;
    }

    if (!pending_) {
        if (!matchesLeader(event)) {
            return false;
        }

        pending_ = true;
        emit feedbackChanged(QStringLiteral("%1 …").arg(leaderName()));
        return true;
    }

    if (event.key() == Qt::Key_Escape) {
        cancel(QStringLiteral("Application prefix cancelled"));
        return true;
    }

    const auto key = event.text();
    if (key == QStringLiteral("?") || key == QStringLiteral("m")) {
        const auto sequence = QStringLiteral("%1+%2").arg(leaderName(), key);
        pending_ = false;
        emit sequenceAccepted(sequence);
        emit feedbackChanged(QStringLiteral("%1 command is not available yet").arg(sequence));
        return true;
    }

    const auto displayKey = key.isEmpty() ? QStringLiteral("<key>") : key;
    cancel(QStringLiteral("Unknown application command: %1+%2").arg(leaderName(), displayKey));
    return true;
}

bool PrefixRouter::isPending() const noexcept { return pending_; }

LeaderKey PrefixRouter::leader() const noexcept { return leader_; }

bool PrefixRouter::matchesLeader(const QKeyEvent& event) const noexcept {
    if (leader_ == LeaderKey::Space) {
        return event.key() == Qt::Key_Space && event.modifiers() == Qt::NoModifier;
    }

    return event.key() == Qt::Key_B && event.modifiers() == Qt::ControlModifier;
}

QString PrefixRouter::leaderName() const {
    return leader_ == LeaderKey::Space ? QStringLiteral("Space") : QStringLiteral("Ctrl+B");
}

void PrefixRouter::cancel(const QString& feedback) {
    pending_ = false;
    emit feedbackChanged(feedback);
}

} // namespace omanotes
