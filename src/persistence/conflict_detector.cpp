#include "persistence/conflict_detector.hpp"

#include <QCryptographicHash>

#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace omanotes {

SavedRevision SavedRevision::of(QByteArrayView contents) {
    return {QCryptographicHash::hash(contents, QCryptographicHash::Sha256)};
}

DiskRevision DiskRevision::read(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {DiskRevision::State::Missing, {}};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {DiskRevision::State::Unreadable, {}};
    }
    const std::string bytes(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) {
        return {DiskRevision::State::Unreadable, {}};
    }
    return {DiskRevision::State::Present,
            QCryptographicHash::hash(QByteArrayView(bytes), QCryptographicHash::Sha256)};
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
