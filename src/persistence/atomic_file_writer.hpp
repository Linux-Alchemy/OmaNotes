#ifndef OMANOTES_PERSISTENCE_ATOMIC_FILE_WRITER_HPP
#define OMANOTES_PERSISTENCE_ATOMIC_FILE_WRITER_HPP

#include <QByteArrayView>

#include <sys/types.h>

#include <cerrno>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace omanotes {

class WorkspaceRoot;

enum class SaveErrorCode : std::uint8_t {
    OutsideRoot,
    InvalidTarget,
    TemporaryFailed,
    WriteFailed,
    SyncFailed,
    ReplaceFailed
};

struct SaveError {
    SaveErrorCode code;
    std::string message;
};

/// Test-only fault injection for the atomic replace. Every field is inert by
/// default; production code never sets one. They exist so the failure paths
/// (short write, full disk, failed flush, failed rename) can be exercised
/// deterministically instead of being trusted.
struct AtomicWriteFaults {
    /// Fail the write with `writeErrno` once this many bytes have landed.
    std::optional<std::size_t> failWriteAfterBytes;
    int writeErrno{ENOSPC};
    bool failSync{false};
    bool failRename{false};
};

/// Replace `destination` with `contents` so that a reader never sees a
/// partial document: temporary file in the same directory, created with
/// `mode`, flushed to storage, renamed over the destination, directory
/// flushed. A failure at any point leaves the destination exactly as it was
/// and removes the temporary. Performs no containment check: callers decide
/// where a file may go.
[[nodiscard]] std::expected<void, SaveError>
replaceFileAtomically(const std::filesystem::path& destination, QByteArrayView contents,
                      mode_t mode, const AtomicWriteFaults* faults = nullptr);

/// The prefix every temporary created by replaceFileAtomically carries, in the
/// destination's directory, followed by the destination's file name.
[[nodiscard]] std::string atomicTemporaryPrefix();

/// Writes a workspace file so that a reader never sees a partial document.
///
/// The write goes to a temporary file in the target's own directory, is flushed
/// to storage, and is then renamed over the target. A failure at any point
/// leaves the original file exactly as it was and removes the temporary.
class AtomicFileWriter final {
  public:
    /// Write `contents` to `target`, which must resolve inside `root`.
    ///
    /// A symlink target is written *through*: the link itself is preserved and
    /// the bytes land on the file it points at, which must also be inside the
    /// root. Returns the path actually written.
    [[nodiscard]] std::expected<std::filesystem::path, SaveError>
    write(const std::filesystem::path& target, QByteArrayView contents,
          const WorkspaceRoot& root) const;
};

} // namespace omanotes

#endif // OMANOTES_PERSISTENCE_ATOMIC_FILE_WRITER_HPP
