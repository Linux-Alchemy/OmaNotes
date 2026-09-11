#ifndef OMANOTES_SESSION_SESSION_RESTORER_HPP
#define OMANOTES_SESSION_SESSION_RESTORER_HPP

#include "core/buffer.hpp"
#include "core/view_mode.hpp"
#include "persistence/recovery_store.hpp"
#include "session/session_snapshot.hpp"
#include "workspace/workspace_root.hpp"

#include <QString>

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace omanotes {

/// What a restore needs from the window, and nothing more. The restorer
/// decides *what* comes back and in which order; the host decides how a
/// buffer is opened. Tests supply a fake host; the application supplies
/// MainWindow.
class SessionHost {
  public:
    virtual ~SessionHost() = default;

    virtual void applyWindow(const WindowSnapshot& window) = 0;
    /// `selected` is the absolute path already verified inside the root, or
    /// absent when nothing should be selected.
    virtual void applySidebar(const SidebarSnapshot& sidebar,
                              const std::optional<std::filesystem::path>& selected) = 0;
    /// Open a note that is on disk and unchanged. Absent when the host could
    /// not open it; the restorer reports it and carries on.
    virtual std::optional<BufferId> openNote(const std::filesystem::path& absolute) = 0;
    /// Open an empty unnamed buffer.
    virtual BufferId openScratch() = 0;
    /// Open a buffer from a recovery record, dirty, disposed as `plan` says.
    virtual std::optional<BufferId> openRecovered(const BufferRecovery& record,
                                                  const RecoveryPlan& plan) = 0;
    virtual void applyBufferView(BufferId id, ViewMode mode, CursorSnapshot cursor,
                                 int scrollLine) = 0;
    virtual void activateBuffer(BufferId id) = 0;
};

struct RestoreReport {
    int restored{0};
    int recovered{0};
    /// Buffers whose unsaved text belongs to another live instance (ADR
    /// 0012): reopened clean from disk when they have a note, otherwise not
    /// opened. Counted so the status line can say so.
    int heldElsewhere{0};
    /// One line per item that did not come back, phrased for the status line.
    std::vector<QString> skipped;
    /// Buffers that came back from a recovery record, keyed by the id the
    /// host gave them, so the controller keeps checkpointing into the same
    /// record and removes it on save or discard.
    std::map<BufferId, RecoveryId> recoveryIds;

    /// A one-line account for the status line, or empty when nothing happened.
    [[nodiscard]] QString summary() const;
};

/// Puts a snapshot back best-effort: window, sidebar, buffers in order, the
/// active buffer last. One missing or refused note never stops the rest.
/// Reads the disk to plan; never writes it.
class SessionRestorer final {
  public:
    /// `adoptRecords` is false for an instance that does not hold the
    /// workspace's lock: it must not read or take over recovery records, so
    /// dirty buffers come back clean from disk and scratch ones stay parked.
    [[nodiscard]] RestoreReport restore(const SessionSnapshot& snapshot, const WorkspaceRoot& root,
                                        const RecoveryStore& recovery, SessionHost& host,
                                        bool adoptRecords = true) const;

    /// Bring back records the snapshot no longer references, as dirty
    /// buffers. Unsaved text is never discarded for lack of bookkeeping.
    [[nodiscard]] RestoreReport restoreUnreferenced(std::span<const RecoveryId> orphans,
                                                    const WorkspaceRoot& root,
                                                    const RecoveryStore& recovery,
                                                    SessionHost& host) const;
};

} // namespace omanotes

#endif // OMANOTES_SESSION_SESSION_RESTORER_HPP
