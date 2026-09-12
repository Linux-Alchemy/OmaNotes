#ifndef OMANOTES_APP_APPLICATION_CONTROLLER_HPP
#define OMANOTES_APP_APPLICATION_CONTROLLER_HPP

#include "app/launch_request.hpp"
#include "core/buffer.hpp"
#include "persistence/recovery_store.hpp"
#include "session/instance_lock.hpp"
#include "session/session_restorer.hpp"
#include "session/session_store.hpp"
#include "workspace/workspace_root.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>

namespace omanotes {

class MainWindow;

/// Owns the session on the window's behalf (docs/session-format.md).
///
/// On start it restores the last desk for this root unless `--fresh` was
/// given, brings back any unreferenced recovery records as dirty buffers,
/// then opens or focuses the file named on the command line last. Work
/// parked in other roots is never mentioned (ADR 0016); state for roots
/// that no longer exist is swept after seven days (ADR 0015). While the
/// window runs it checkpoints dirty buffers and rewrites the snapshot two
/// seconds after the desk last changed, when the window loses focus, and on
/// close. A record is removed the moment its buffer is saved or discarded.
class ApplicationController final : public QObject {
    Q_OBJECT

  public:
    static constexpr std::chrono::milliseconds kDebounce{2000};

    /// `sessionsDirectory` is the store root; production passes
    /// `SessionStore::defaultDirectory()`, tests a temporary directory.
    ApplicationController(MainWindow& window, LaunchRequest request,
                          const std::filesystem::path& sessionsDirectory,
                          QObject* parent = nullptr);
    ~ApplicationController() override;

    /// Restore and report. Call before the window is shown.
    void start();
    /// Checkpoint every dirty buffer and, unless `--fresh`, rewrite the
    /// snapshot now. What the timer and the close path both run.
    void checkpointNow();

    [[nodiscard]] const RestoreReport& lastReport() const noexcept;
    [[nodiscard]] const std::map<BufferId, RecoveryId>& recoveryIds() const noexcept;
    /// True when this instance holds the workspace's lock and so owns
    /// recovery for it (ADR 0012).
    [[nodiscard]] bool holdsInstanceLock() const noexcept;

  private:
    void scheduleCheckpoint();
    void releaseRecord(BufferId id);
    [[nodiscard]] std::optional<WorkspaceRoot> resolveRoot() const;

    MainWindow& window_;
    LaunchRequest request_;
    SessionStore sessions_;
    RecoveryStore recovery_;
    SessionRestorer restorer_;
    QTimer debounce_;
    std::map<BufferId, RecoveryId> recoveryIds_;
    RestoreReport lastReport_;
    std::optional<InstanceLock> lock_;
    bool started_{false};
};

} // namespace omanotes

#endif // OMANOTES_APP_APPLICATION_CONTROLLER_HPP
