#ifndef OMANOTES_PERSISTENCE_NOTE_READER_HPP
#define OMANOTES_PERSISTENCE_NOTE_READER_HPP

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <expected>
#include <filesystem>

namespace omanotes {

/// The largest note the editor reads. It matches the recovery ceiling on
/// purpose: a note the reader refuses is one the recovery store could not
/// have protected either, so the two limits should move together.
inline constexpr qint64 kNoteMaxBytes = qint64{16} * 1024 * 1024;

enum class NoteReadErrorCode : std::uint8_t { Missing, NotRegular, Oversized, Unreadable };

struct NoteReadError {
    NoteReadErrorCode code;
    /// Phrased for the status line; names the file, never its contents.
    QString message;
};

/// Read a note's bytes the safe way, for every path that reaches disk after
/// validation: the open, the explicit reload, the watcher's reload, and the
/// conflict hash. The final path component is opened without following a
/// symlink, so a note swapped for a link after it was validated is refused
/// rather than read; the open descriptor is checked to be a regular file, so
/// a directory, pipe or device is refused before a byte is read; and reading
/// stops at `limit`, so a file that grew is refused rather than swallowed.
///
/// A path that is a symlink *inside* the root still opens, because the caller
/// resolves it canonically first and hands over the target. Only the last
/// step is guarded here; the directory-swap race between validation and open
/// is recorded in docs/threat-model.md (T-S4) and is not closed by this.
[[nodiscard]] std::expected<QByteArray, NoteReadError>
readNoteFile(const std::filesystem::path& path, qint64 limit = kNoteMaxBytes);

} // namespace omanotes

#endif // OMANOTES_PERSISTENCE_NOTE_READER_HPP
