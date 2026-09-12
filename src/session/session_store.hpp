#ifndef OMANOTES_SESSION_SESSION_STORE_HPP
#define OMANOTES_SESSION_SESSION_STORE_HPP

#include "persistence/atomic_file_writer.hpp"
#include "session/session_snapshot.hpp"
#include "workspace/workspace_root.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omanotes {

/// One sibling workspace directory as a launch scan sees it: its snapshot's
/// metadata, whether the root it names is still there, and the last time the
/// program wrote anything into it. Built without opening a recovery record.
struct ParkedWorkspace {
    std::filesystem::path directory;
    std::filesystem::path root;
    std::size_t dirtyBuffers{0};
    bool rootExists{true};
    std::filesystem::file_time_type lastActivity;
};

/// A workspace directory the retention sweep removed, for the status line.
struct SweptWorkspace {
    std::filesystem::path root;
    std::size_t dirtyBuffers{0};
};

/// Keeps one structural snapshot per workspace beneath the XDG state
/// directory (docs/session-format.md, ADR 0011).
///
/// Layout: `<sessions>/<workspace-id>/session.json`, where the id is derived
/// from the canonical root path so a directory listing does not spell out
/// every workspace ever opened. Directories are owner-only (0700), the file
/// is owner-only (0600), and replacement is temporary-file-and-rename through
/// the same code that saves notes. A failed save leaves the previous valid
/// snapshot untouched. Symlinks anywhere in the store are refused, never
/// followed.
class SessionStore final {
  public:
    /// Temporaries older than this are presumed abandoned by a crashed writer
    /// and removed before the next save. Younger ones may belong to a
    /// concurrent instance and are left alone.
    static constexpr std::chrono::minutes kStaleTemporaryAge{15};
    /// State for a root that no longer exists is kept this long past the
    /// last write into it, then removed at launch (ADR 0015, finding F-22).
    /// A root that exists is never swept, whatever its age.
    static constexpr std::chrono::days kVanishedRootRetention{7};
    /// How many sibling directories a launch scan reads. The notice and the
    /// sweep are courtesies; they must not make launch slow on a machine
    /// that has opened a thousand workspaces.
    static constexpr int kSiblingScanLimit = 64;

    /// `sessionsDirectory` is the `sessions` directory itself; it and its
    /// parents are created on first save. `faults` is a test-only seam and is
    /// null in production.
    explicit SessionStore(std::filesystem::path sessionsDirectory,
                          const AtomicWriteFaults* faults = nullptr);

    /// `$XDG_STATE_HOME/omanotes/sessions`, resolved through Qt's XDG support.
    [[nodiscard]] static std::filesystem::path defaultDirectory();

    /// 32 lowercase hex characters: the first 128 bits of SHA-256 over the
    /// canonical root path's bytes. Deterministic, no index to keep, and the
    /// snapshot inside still names the root so a collision cannot go unseen.
    [[nodiscard]] static std::string workspaceId(const std::filesystem::path& canonicalRoot);

    [[nodiscard]] std::filesystem::path directoryFor(const WorkspaceRoot& root) const;
    [[nodiscard]] std::filesystem::path fileFor(const WorkspaceRoot& root) const;
    /// `<sessions>/<workspace-id>/instance.lock`, the per-root instance lock
    /// (ADR 0012). Lives beside the snapshot so it shares its permissions.
    [[nodiscard]] std::filesystem::path lockFileFor(const WorkspaceRoot& root) const;

    /// Create and tighten the workspace's directory chain now, as a save
    /// would, without writing a snapshot. What the instance lock needs
    /// before the first checkpoint.
    [[nodiscard]] std::expected<void, SessionError>
    ensureDirectoryFor(const WorkspaceRoot& root) const;

    /// Write the snapshot for `snapshot.workspaceRoot`, which must resolve as a
    /// workspace. Creates and tightens the directories, removes stale
    /// temporaries, and replaces the file atomically. On any failure the
    /// previous file, if there was one, is byte-for-byte unchanged.
    [[nodiscard]] std::expected<void, SessionError> save(const SessionSnapshot& snapshot) const;

    /// Read the snapshot for `root`. No file means no session: an empty
    /// optional, not an error. A file that is present but unreadable,
    /// malformed, or written for a different root is an error carrying the
    /// diagnostic; the file is never modified.
    [[nodiscard]] std::expected<std::optional<SessionSnapshot>, SessionError>
    load(const WorkspaceRoot& root) const;

    /// Every other workspace's directory that holds a readable snapshot,
    /// metadata only (ADR 0010). `ownId` is this launch's workspace id and is
    /// skipped. Symlinked entries are skipped, never followed.
    [[nodiscard]] std::vector<ParkedWorkspace> listSiblings(std::string_view ownId) const;

    /// Remove the directories of workspaces whose root no longer exists and
    /// whose last write is older than `now - kVanishedRootRetention`, unless
    /// another instance holds their lock (ADR 0012). Returns what went, so
    /// the caller can say so. `now` is a parameter so tests need not wait a
    /// week; production passes the file clock's now.
    [[nodiscard]] std::vector<SweptWorkspace>
    sweepVanishedRoots(std::string_view ownId, std::filesystem::file_time_type now) const;

  private:
    [[nodiscard]] std::expected<void, SessionError>
    prepareDirectory(const std::filesystem::path& directory) const;

    std::filesystem::path sessions_;
    const AtomicWriteFaults* faults_;
};

} // namespace omanotes

#endif // OMANOTES_SESSION_SESSION_STORE_HPP
