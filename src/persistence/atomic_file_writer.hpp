#ifndef OMANOTES_PERSISTENCE_ATOMIC_FILE_WRITER_HPP
#define OMANOTES_PERSISTENCE_ATOMIC_FILE_WRITER_HPP

#include <QByteArrayView>

#include <cstdint>
#include <expected>
#include <filesystem>
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

/// Writes a file so that a reader never sees a partial document.
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
