#ifndef OMANOTES_PERSISTENCE_CONFLICT_DETECTOR_HPP
#define OMANOTES_PERSISTENCE_CONFLICT_DETECTOR_HPP

#include <QByteArray>
#include <QByteArrayView>

#include <cstdint>
#include <filesystem>

namespace omanotes {

/// The bytes the editor last loaded from, or wrote to, a file.
///
/// Identity is a content hash rather than a timestamp: a note is small enough
/// to hash in microseconds, and timestamps lie in exactly the cases that
/// matter here (`touch`, a rename-replace save that preserves mtime, two writes
/// within one clock tick).
struct SavedRevision {
    QByteArray contentHash;

    [[nodiscard]] static SavedRevision of(QByteArrayView contents);

    [[nodiscard]] bool operator==(const SavedRevision& other) const noexcept = default;
};

/// What the disk holds right now.
struct DiskRevision {
    enum class State : std::uint8_t { Present, Missing, Unreadable };

    State state{State::Missing};
    /// Meaningful only when `state == Present`.
    QByteArray contentHash;

    [[nodiscard]] static DiskRevision read(const std::filesystem::path& path);
};

enum class ExternalChangeAction : std::uint8_t {
    /// The disk still matches what the editor knows; nothing to do.
    Unchanged,
    /// Someone else changed the file and the buffer has no edits: take theirs.
    ReloadClean,
    /// Someone else changed the file and the buffer has edits too: ask.
    PromptConflict,
    /// The file is gone; the buffer is now the only copy.
    FileRemoved
};

/// Decide what an external change means for a buffer. Never destructive on its
/// own: `ReloadClean` is the only outcome that replaces anything, and it only
/// replaces an unmodified buffer with the disk's newer content.
[[nodiscard]] ExternalChangeAction classifyExternalChange(const SavedRevision& known,
                                                          const DiskRevision& current,
                                                          bool bufferModified);

} // namespace omanotes

#endif // OMANOTES_PERSISTENCE_CONFLICT_DETECTOR_HPP
