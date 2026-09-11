#include "persistence/atomic_file_writer.hpp"

#include "persistence/note_reader.hpp"
#include "workspace/workspace_root.hpp"

#include <QCryptographicHash>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

namespace omanotes {

namespace {

constexpr mode_t kDefaultMode = 0644;

std::string describe(const std::string& action, int errorNumber) {
    return action + ": " + std::strerror(errorNumber);
}

/// Containment test for a file that may not exist yet.
///
/// `WorkspaceRoot::contains` canonicalises its argument, which fails for a path
/// being created, so the existing parent directory is what gets checked. That
/// also resolves any `..` before the comparison.
bool insideRoot(const WorkspaceRoot& root, const std::filesystem::path& path) {
    std::error_code error;
    const auto parent = std::filesystem::canonical(path.parent_path(), error);
    if (error) {
        return false;
    }
    return root.contains(parent);
}

/// Follow a symlink to the file that will actually receive the bytes.
///
/// Writing through the link keeps a note that lives elsewhere linked where the
/// user put it, instead of quietly replacing the link with a regular file.
std::expected<std::filesystem::path, SaveError>
resolveWriteTarget(const std::filesystem::path& target) {
    std::error_code error;
    if (!std::filesystem::is_symlink(std::filesystem::symlink_status(target, error))) {
        return target;
    }

    auto resolved = std::filesystem::weakly_canonical(target, error);
    if (error) {
        return std::unexpected(SaveError{SaveErrorCode::InvalidTarget,
                                         "Could not resolve the link at " + target.string()});
    }
    return resolved;
}

/// Reuse an existing file's permissions so saving never re-permissions a note.
/// A new file follows the user's umask rather than a hard-coded mode.
mode_t modeFor(const std::filesystem::path& target) {
    struct stat status{};
    if (::stat(target.c_str(), &status) == 0) {
        return status.st_mode & 07777;
    }
    const mode_t mask = ::umask(0);
    ::umask(mask);
    return kDefaultMode & ~mask;
}

std::expected<void, SaveError> flushDirectory(const std::filesystem::path& directory) {
    const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        return std::unexpected(
            SaveError{SaveErrorCode::SyncFailed, describe("Could not open the directory", errno)});
    }
    const int synced = ::fsync(descriptor);
    const int syncError = errno;
    ::close(descriptor);
    if (synced != 0) {
        return std::unexpected(SaveError{SaveErrorCode::SyncFailed,
                                         describe("Could not flush the directory", syncError)});
    }
    return {};
}

} // namespace

std::string atomicTemporaryPrefix() { return ".omanotes-"; }

/// Does the destination still satisfy the precondition? Reads through the
/// bounded reader: a destination that became a symlink, a pipe, or too large
/// to hash cannot be shown to match, so it does not.
std::optional<std::string> preconditionFailure(const std::filesystem::path& destination,
                                               const WritePrecondition& precondition) {
    if (precondition.kind == WritePrecondition::Kind::Any) {
        return std::nullopt;
    }
    const auto bytes = readNoteFile(destination);
    const auto missing = !bytes && bytes.error().code == NoteReadErrorCode::Missing;
    switch (precondition.kind) {
    case WritePrecondition::Kind::Any:
        return std::nullopt;
    case WritePrecondition::Kind::Absent:
        if (missing) {
            return std::nullopt;
        }
        return "The file appeared on disk while the save was in progress";
    case WritePrecondition::Kind::Matches:
        if (missing) {
            return "The file was removed while the save was in progress";
        }
        if (!bytes) {
            return "The file could not be re-read before replacing it: " +
                   bytes.error().message.toStdString();
        }
        if (QCryptographicHash::hash(QByteArrayView(*bytes), QCryptographicHash::Sha256) !=
            precondition.contentHash) {
            return "The file changed on disk while the save was in progress";
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::expected<void, SaveError> replaceFileAtomically(const std::filesystem::path& destination,
                                                     QByteArrayView contents, mode_t mode,
                                                     const AtomicWriteFaults* faults,
                                                     const WritePrecondition& precondition) {
    const auto directory = destination.parent_path();
    const auto pattern =
        (directory / (atomicTemporaryPrefix() + destination.filename().string() + "-XXXXXX"))
            .string();
    std::vector<char> temporaryPath(pattern.begin(), pattern.end());
    temporaryPath.push_back('\0');

    int descriptor = ::mkstemp(temporaryPath.data());
    if (descriptor < 0) {
        return std::unexpected(SaveError{SaveErrorCode::TemporaryFailed,
                                         describe("Could not create a temporary file", errno)});
    }
    const std::filesystem::path temporary(temporaryPath.data());

    const auto abandon = [&temporary, &descriptor](SaveErrorCode code, const std::string& message) {
        if (descriptor >= 0) {
            ::close(descriptor);
            descriptor = -1;
        }
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected(SaveError{code, message});
    };

    // mkstemp creates 0600; apply the requested mode before any bytes land so
    // the file is never observable with the wrong permissions.
    if (::fchmod(descriptor, mode) != 0) {
        return abandon(SaveErrorCode::WriteFailed,
                       describe("Could not set file permissions", errno));
    }

    const char* bytes = contents.data();
    auto remaining = static_cast<std::size_t>(contents.size());
    std::size_t landed = 0;
    while (remaining > 0) {
        auto chunk = remaining;
        if (faults != nullptr && faults->failWriteAfterBytes.has_value()) {
            if (landed >= *faults->failWriteAfterBytes) {
                return abandon(SaveErrorCode::WriteFailed,
                               describe("Could not write the file", faults->writeErrno));
            }
            chunk = std::min(chunk, *faults->failWriteAfterBytes - landed);
        }
        const auto written = ::write(descriptor, bytes, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return abandon(SaveErrorCode::WriteFailed, describe("Could not write the file", errno));
        }
        bytes += written;
        remaining -= static_cast<std::size_t>(written);
        landed += static_cast<std::size_t>(written);
    }

    // Durability before visibility: the bytes reach storage before the rename
    // makes them the file every other reader sees.
    if ((faults != nullptr && faults->failSync) || ::fsync(descriptor) != 0) {
        return abandon(SaveErrorCode::SyncFailed,
                       describe("Could not flush the file", faults != nullptr ? EIO : errno));
    }
    if (::close(descriptor) != 0) {
        descriptor = -1;
        return abandon(SaveErrorCode::WriteFailed, describe("Could not close the file", errno));
    }
    descriptor = -1;

    // The last look before the rename: the bytes are durable, so this is
    // as late as the comparison can be made without a rename-if-unchanged
    // primitive the filesystem does not offer.
    if (const auto changed = preconditionFailure(destination, precondition); changed) {
        return abandon(SaveErrorCode::ChangedSinceRead, *changed);
    }

    std::error_code error;
    if (faults != nullptr && faults->failRename) {
        error = std::make_error_code(std::errc::io_error);
    } else {
        std::filesystem::rename(temporary, destination, error);
    }
    if (error) {
        return abandon(SaveErrorCode::ReplaceFailed,
                       "Could not replace the file: " + error.message());
    }

    return flushDirectory(directory);
}

std::expected<std::filesystem::path, SaveError>
AtomicFileWriter::write(const std::filesystem::path& target, QByteArrayView contents,
                        const WorkspaceRoot& root, const WritePrecondition& precondition) const {
    if (target.empty() || !target.has_filename() || target.parent_path().empty()) {
        return std::unexpected(SaveError{SaveErrorCode::InvalidTarget, "No file was named"});
    }
    if (!insideRoot(root, target)) {
        return std::unexpected(
            SaveError{SaveErrorCode::OutsideRoot, "Refusing to write outside the workspace root"});
    }

    auto destination = resolveWriteTarget(target);
    if (!destination) {
        return std::unexpected(destination.error());
    }
    // A link may point anywhere; the file it names must still be in the root.
    if (!insideRoot(root, *destination)) {
        return std::unexpected(
            SaveError{SaveErrorCode::OutsideRoot,
                      "Refusing to write through a link that leaves the workspace root"});
    }

    std::error_code error;
    if (std::filesystem::is_directory(*destination, error)) {
        return std::unexpected(SaveError{SaveErrorCode::InvalidTarget, "That path is a directory"});
    }

    if (auto replaced = replaceFileAtomically(*destination, contents, modeFor(*destination),
                                              nullptr, precondition);
        !replaced) {
        return std::unexpected(replaced.error());
    }
    return *destination;
}

} // namespace omanotes
