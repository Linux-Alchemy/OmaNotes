#ifndef OMANOTES_PERSISTENCE_RECOVERY_STORE_HPP
#define OMANOTES_PERSISTENCE_RECOVERY_STORE_HPP

#include "persistence/atomic_file_writer.hpp"
#include "persistence/conflict_detector.hpp"
#include "session/session_snapshot.hpp"
#include "workspace/workspace_root.hpp"

#include <QString>
#include <QUuid>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace omanotes {

inline constexpr std::uint32_t kRecoveryFormatVersion = 1;
/// The largest buffer that is checkpointed. Larger ones are refused with a
/// diagnostic so the user knows that buffer is unprotected until saved.
inline constexpr std::size_t kRecoveryMaxContentBytes = std::size_t{16} * 1024 * 1024;

/// One dirty buffer's unsaved text, plus what is needed to put it back
/// safely (docs/session-format.md, "Recovery records").
struct BufferRecovery {
    /// The buffer's note, relative to the workspace root; absent for scratch.
    std::optional<std::filesystem::path> path;
    QString contents;
    /// The note's on-disk bytes when this checkpoint was taken; absent when
    /// the note did not exist. Lets a restore see whether the disk moved on.
    std::optional<SavedRevision> baseRevision;

    [[nodiscard]] bool operator==(const BufferRecovery&) const = default;
};

enum class RecoveryErrorCode : std::uint8_t {
    InvalidId,
    Oversized,
    Malformed,
    UnsupportedVersion,
    FutureVersion,
    InvalidField,
    StoreFailed
};

struct RecoveryError {
    RecoveryErrorCode code;
    QString location;
    QString message;

    [[nodiscard]] QString describe() const;
};

/// Keeps recovery records for one workspace in its own `recovery/`
/// directory, owner-only, replaced atomically through the same writer as
/// notes. Records are addressed by id inside that directory and nowhere
/// else: an id becomes a file name only after it has been validated as a
/// UUID, so no record can be reached by path and no snapshot can point a
/// restore at another workspace's text (ADR 0010).
class RecoveryStore final {
  public:
    /// `directory` is the workspace's `recovery/` directory, created on the
    /// first checkpoint. `faults` is a test-only seam, null in production.
    explicit RecoveryStore(std::filesystem::path directory,
                           const AtomicWriteFaults* faults = nullptr);

    /// The recovery directory beside a workspace's session file.
    [[nodiscard]] static std::filesystem::path
    directoryBeside(const std::filesystem::path& sessionDirectory);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

    /// Write a record. Pass the buffer's existing id to replace its record
    /// in place; omit it the first time a buffer becomes dirty and keep the
    /// returned id for the snapshot and for later checkpoints.
    [[nodiscard]] std::expected<RecoveryId, RecoveryError>
    checkpoint(const BufferRecovery& state,
               std::optional<RecoveryId> existing = std::nullopt) const;

    /// Read a record. No record means an empty optional, not an error; a
    /// record that is present but unreadable, malformed or oversized is an
    /// error and is left exactly as found.
    [[nodiscard]] std::expected<std::optional<BufferRecovery>, RecoveryError>
    load(RecoveryId id) const;

    /// Remove a record. Removing one that does not exist succeeds.
    [[nodiscard]] std::expected<void, RecoveryError> remove(RecoveryId id) const;

    /// Every record id present in the directory, in no particular order.
    [[nodiscard]] std::expected<std::vector<RecoveryId>, RecoveryError> list() const;

    /// Remove every record not in `live`. Returns how many were removed. For
    /// use after a successful restore, when `live` is what the snapshot
    /// still references.
    [[nodiscard]] std::expected<std::size_t, RecoveryError>
    removeAllExcept(std::span<const RecoveryId> live) const;

  private:
    [[nodiscard]] std::expected<std::filesystem::path, RecoveryError> fileFor(RecoveryId id) const;

    std::filesystem::path directory_;
    const AtomicWriteFaults* faults_;
};

/// Serialize and parse a record document; exposed for fixtures and for the
/// size check callers may want before checkpointing.
[[nodiscard]] QByteArray serializeBufferRecovery(const BufferRecovery& state);
[[nodiscard]] std::expected<BufferRecovery, RecoveryError>
parseBufferRecovery(const QByteArray& bytes);

/// How a restored buffer should come back. Every case is restored dirty;
/// the difference is what saving it may do (ADR 0006).
enum class RecoveryTarget : std::uint8_t {
    /// No note: `Untitled`, saving needs an explicit name.
    Scratch,
    /// The note is as the checkpoint left it: `:w` writes as usual.
    File,
    /// Someone changed the note since: in conflict, `:w` refuses, `:w!` overwrites.
    FileChangedOnDisk,
    /// The note is gone: `:w` recreates it.
    FileMissing
};

struct RecoveryPlan {
    RecoveryTarget target;
    /// The absolute note path inside the root; absent for Scratch.
    std::optional<std::filesystem::path> resolved;

    [[nodiscard]] bool operator==(const RecoveryPlan&) const = default;
};

/// Decide how a record is restored against the live root. Reads the disk,
/// never writes it. A path that now escapes the root, or is not a regular
/// readable file, is an error: the record is reported, not restored.
[[nodiscard]] std::expected<RecoveryPlan, WorkspaceError> planRecovery(const BufferRecovery& state,
                                                                       const WorkspaceRoot& root);

} // namespace omanotes

#endif // OMANOTES_PERSISTENCE_RECOVERY_STORE_HPP
