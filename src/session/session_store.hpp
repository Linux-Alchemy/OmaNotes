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

namespace omanotes {

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

  private:
    [[nodiscard]] std::expected<void, SessionError>
    prepareDirectory(const std::filesystem::path& directory) const;

    std::filesystem::path sessions_;
    const AtomicWriteFaults* faults_;
};

} // namespace omanotes

#endif // OMANOTES_SESSION_SESSION_STORE_HPP
