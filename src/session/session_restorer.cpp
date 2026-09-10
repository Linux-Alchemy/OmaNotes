#include "session/session_restorer.hpp"

#include <QStringList>

#include <utility>

namespace omanotes {

namespace {

QString name(const std::optional<std::filesystem::path>& path) {
    return path ? QString::fromStdString(path->generic_string()) : scratchDisplayName();
}

/// Open one record through the host, or explain why not.
std::optional<BufferId> openRecord(const BufferRecovery& record, const WorkspaceRoot& root,
                                   SessionHost& host, RestoreReport& report) {
    const auto plan = planRecovery(record, root);
    if (!plan) {
        report.skipped.push_back(
            QStringLiteral("%1 (unsaved changes not restored: %2)")
                .arg(name(record.path), QString::fromStdString(plan.error().message)));
        return std::nullopt;
    }
    const auto id = host.openRecovered(record, *plan);
    if (!id) {
        report.skipped.push_back(
            QStringLiteral("%1 (unsaved changes could not be opened)").arg(name(record.path)));
        return std::nullopt;
    }
    ++report.recovered;
    return id;
}

} // namespace

QString RestoreReport::summary() const {
    QStringList parts;
    if (restored > 0) {
        parts << (restored == 1 ? QStringLiteral("Restored 1 buffer")
                                : QStringLiteral("Restored %1 buffers").arg(restored));
    }
    if (recovered > 0) {
        parts << (recovered == 1
                      ? QStringLiteral("recovered 1 with unsaved changes")
                      : QStringLiteral("recovered %1 with unsaved changes").arg(recovered));
    }
    if (!skipped.empty()) {
        QStringList items;
        for (const auto& item : skipped) {
            items << item;
        }
        parts << QStringLiteral("skipped %1").arg(items.join(QStringLiteral(", ")));
    }
    return parts.join(QStringLiteral("; "));
}

RestoreReport SessionRestorer::restore(const SessionSnapshot& snapshot, const WorkspaceRoot& root,
                                       const RecoveryStore& recovery, SessionHost& host) const {
    RestoreReport report;
    host.applyWindow(snapshot.window);

    std::optional<std::filesystem::path> selected;
    if (snapshot.sidebar.selectedPath) {
        if (auto resolved = resolveSessionPath(root, *snapshot.sidebar.selectedPath); resolved) {
            selected = std::move(*resolved);
        }
        // A vanished selection is not worth a report line; the tree simply
        // opens unselected.
    }
    host.applySidebar(snapshot.sidebar, selected);

    std::optional<BufferId> activate;
    for (const auto& buffer : snapshot.buffers) {
        std::optional<BufferId> opened;
        if (buffer.recovery) {
            const auto record = recovery.load(*buffer.recovery);
            if (!record) {
                report.skipped.push_back(QStringLiteral("%1 (unsaved changes unreadable: %2)")
                                             .arg(name(buffer.path), record.error().describe()));
            } else if (!record->has_value()) {
                report.skipped.push_back(
                    QStringLiteral("%1 (unsaved changes missing)").arg(name(buffer.path)));
            } else {
                opened = openRecord(**record, root, host, report);
                if (opened) {
                    report.recoveryIds.emplace(*opened, *buffer.recovery);
                }
            }
            // A dirty buffer whose record is gone is not reopened clean from
            // disk behind the user's back; the report says what happened.
        } else if (!buffer.path) {
            opened = host.openScratch();
            ++report.restored;
        } else {
            const auto resolved = resolveSessionPath(root, *buffer.path);
            if (!resolved) {
                const auto reason = resolved.error().code == WorkspaceErrorCode::Missing
                                        ? QStringLiteral("missing")
                                        : QString::fromStdString(resolved.error().message);
                report.skipped.push_back(QStringLiteral("%1 (%2)").arg(name(buffer.path), reason));
            } else {
                opened = host.openNote(*resolved);
                if (opened) {
                    ++report.restored;
                } else {
                    report.skipped.push_back(
                        QStringLiteral("%1 (could not be opened)").arg(name(buffer.path)));
                }
            }
        }
        if (!opened) {
            continue;
        }
        host.applyBufferView(*opened, buffer.viewMode, buffer.cursor, buffer.scrollLine);
        if (snapshot.activeBuffer == buffer.id) {
            activate = opened;
        }
    }
    if (activate) {
        host.activateBuffer(*activate);
    }
    return report;
}

RestoreReport SessionRestorer::restoreUnreferenced(std::span<const RecoveryId> orphans,
                                                   const WorkspaceRoot& root,
                                                   const RecoveryStore& recovery,
                                                   SessionHost& host) const {
    RestoreReport report;
    for (const auto& id : orphans) {
        const auto record = recovery.load(id);
        if (!record) {
            report.skipped.push_back(
                QStringLiteral("a recovery record (%1)").arg(record.error().describe()));
            continue;
        }
        if (!record->has_value()) {
            continue;
        }
        if (const auto opened = openRecord(**record, root, host, report); opened) {
            report.recoveryIds.emplace(*opened, id);
            host.applyBufferView(*opened, ViewMode::Writing, {}, 0);
        }
    }
    return report;
}

} // namespace omanotes
