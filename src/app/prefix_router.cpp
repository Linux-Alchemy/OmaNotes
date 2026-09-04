#include "app/prefix_router.hpp"

#include <QKeyEvent>

#include <utility>

namespace omanotes {

namespace {

bool isModifierKey(const QKeyEvent& event) noexcept {
    switch (event.key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
        return true;
    default:
        return false;
    }
}

} // namespace

PrefixRouter::PrefixRouter(LeaderKey leader, QObject* parent) : QObject(parent), leader_(leader) {}

void PrefixRouter::setResolver(Resolver resolver) { resolver_ = std::move(resolver); }

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
        keys_.clear();
        emit feedbackChanged(QStringLiteral("%1 …").arg(leaderName()));
        return true;
    }

    if (event.key() == Qt::Key_Escape) {
        cancel(QStringLiteral("Application prefix cancelled"));
        return true;
    }

    if (isModifierKey(event)) {
        return false;
    }

    // A second press of the space bar is a key in its own right (LazyVim's
    // `<leader><space>`); it is spelt out because a bare space would vanish
    // into the separator between keys.
    const auto key = event.key() == Qt::Key_Space ? QStringLiteral("Space") : event.text();
    if (key.isEmpty() || key.trimmed().isEmpty()) {
        cancel(QStringLiteral("Unknown application command: %1+<key>").arg(displaySequence()));
        return true;
    }
    keys_.push_back(key);

    const auto lookup = resolver_ ? resolver_(keys_.join(QLatin1Char(' ')))
                                  : SequenceLookup{SequenceMatch::None, {}};
    switch (lookup.match) {
    case SequenceMatch::Exact: {
        const auto sequence = displaySequence();
        pending_ = false;
        keys_.clear();
        emit sequenceAccepted(lookup.commandId, sequence);
        return true;
    }
    case SequenceMatch::Prefix:
        emit feedbackChanged(QStringLiteral("%1 …").arg(displaySequence()));
        return true;
    case SequenceMatch::None:
        break;
    }

    cancel(QStringLiteral("Unknown application command: %1").arg(displaySequence()));
    return true;
}

bool PrefixRouter::isPending() const noexcept { return pending_; }

void PrefixRouter::cancelPending() {
    if (pending_) {
        cancel(QStringLiteral("Application prefix cancelled"));
    }
}

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

QString PrefixRouter::displaySequence() const {
    auto sequence = leaderName();
    for (const auto& key : keys_) {
        sequence += QLatin1Char('+') + key;
    }
    return sequence;
}

void PrefixRouter::cancel(const QString& feedback) {
    pending_ = false;
    keys_.clear();
    emit feedbackChanged(feedback);
}

} // namespace omanotes
