#include "persistence/conflict_detector.hpp"

#include "persistence/note_reader.hpp"

#include <QCryptographicHash>

namespace omanotes {

SavedRevision SavedRevision::of(QByteArrayView contents) {
    return {QCryptographicHash::hash(contents, QCryptographicHash::Sha256)};
}

DiskRevision DiskRevision::read(const std::filesystem::path& path) {
    // The same bounded reader as the editor: a note that is a symlink now,
    // not a regular file, or past the size limit is "unreadable", which the
    // classifier turns into a prompt rather than a reload.
    const auto bytes = readNoteFile(path);
    if (!bytes) {
        return {bytes.error().code == NoteReadErrorCode::Missing ? DiskRevision::State::Missing
                                                                 : DiskRevision::State::Unreadable,
                {}};
    }
    return {DiskRevision::State::Present,
            QCryptographicHash::hash(QByteArrayView(*bytes), QCryptographicHash::Sha256)};
}

ExternalChangeAction classifyExternalChange(const SavedRevision& known, const DiskRevision& current,
                                            bool bufferModified) {
    switch (current.state) {
    case DiskRevision::State::Missing:
        return ExternalChangeAction::FileRemoved;
    case DiskRevision::State::Unreadable:
        // The disk cannot be trusted either way, so nothing is replaced and
        // the user is told. A clean buffer that cannot verify its file is
        // still safer kept than swapped for content nobody could read.
        return ExternalChangeAction::PromptConflict;
    case DiskRevision::State::Present:
        break;
    }

    if (current.contentHash == known.contentHash) {
        return ExternalChangeAction::Unchanged;
    }
    return bufferModified ? ExternalChangeAction::PromptConflict
                          : ExternalChangeAction::ReloadClean;
}

} // namespace omanotes
