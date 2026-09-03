#include "persistence/atomic_file_writer.hpp"

#include "workspace/workspace_root.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

std::expected<std::filesystem::path, SaveError>
AtomicFileWriter::write(const std::filesystem::path& target, QByteArrayView contents,
                        const WorkspaceRoot& root) const {
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

    const auto directory = destination->parent_path();
    const auto pattern =
        (directory / (".omanotes-" + destination->filename().string() + "-XXXXXX")).string();
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

    const char* bytes = contents.data();
    auto remaining = static_cast<std::size_t>(contents.size());
    while (remaining > 0) {
        const auto written = ::write(descriptor, bytes, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return abandon(SaveErrorCode::WriteFailed, describe("Could not write the file", errno));
        }
        bytes += written;
        remaining -= static_cast<std::size_t>(written);
    }

    if (::fchmod(descriptor, modeFor(*destination)) != 0) {
        return abandon(SaveErrorCode::WriteFailed,
                       describe("Could not set file permissions", errno));
    }
    // Durability before visibility: the bytes reach storage before the rename
    // makes them the file every other reader sees.
    if (::fsync(descriptor) != 0) {
        return abandon(SaveErrorCode::SyncFailed, describe("Could not flush the file", errno));
    }
    if (::close(descriptor) != 0) {
        descriptor = -1;
        return abandon(SaveErrorCode::WriteFailed, describe("Could not close the file", errno));
    }
    descriptor = -1;

    std::filesystem::rename(temporary, *destination, error);
    if (error) {
        return abandon(SaveErrorCode::ReplaceFailed,
                       "Could not replace the file: " + error.message());
    }

    if (auto flushed = flushDirectory(directory); !flushed) {
        return std::unexpected(flushed.error());
    }

    return *destination;
}

} // namespace omanotes
