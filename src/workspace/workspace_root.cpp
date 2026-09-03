#include "workspace/workspace_root.hpp"

#include <fstream>
#include <system_error>

namespace omanotes {

namespace {

bool hasAny(std::filesystem::perms permissions, std::filesystem::perms requested) noexcept {
    return (permissions & requested) != std::filesystem::perms::none;
}

bool directoryPermissionsAllowReading(std::filesystem::perms permissions) noexcept {
    constexpr auto read = std::filesystem::perms::owner_read | std::filesystem::perms::group_read |
                          std::filesystem::perms::others_read;
    constexpr auto search = std::filesystem::perms::owner_exec |
                            std::filesystem::perms::group_exec |
                            std::filesystem::perms::others_exec;
    return hasAny(permissions, read) && hasAny(permissions, search);
}

bool filePermissionsAllowReading(std::filesystem::perms permissions) noexcept {
    constexpr auto read = std::filesystem::perms::owner_read | std::filesystem::perms::group_read |
                          std::filesystem::perms::others_read;
    return hasAny(permissions, read);
}

std::string pathMessage(std::string_view message, const std::filesystem::path& path) {
    return std::string(message) + ": " + path.string();
}

} // namespace

WorkspaceRoot::WorkspaceRoot(std::filesystem::path root) : root_(std::move(root)) {}

std::expected<WorkspaceRoot, WorkspaceError>
WorkspaceRoot::resolve(const std::filesystem::path& candidate) {
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) || error) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Missing,
                                              pathMessage("Workspace does not exist", candidate)});
    }
    if (!std::filesystem::is_directory(candidate, error) || error) {
        return std::unexpected(
            WorkspaceError{WorkspaceErrorCode::NotDirectory,
                           pathMessage("Workspace is not a directory", candidate)});
    }

    const auto status = std::filesystem::status(candidate, error);
    if (error || !directoryPermissionsAllowReading(status.permissions())) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Unreadable,
                                              pathMessage("Workspace is not readable", candidate)});
    }

    auto canonical = std::filesystem::canonical(candidate, error);
    if (error) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Unreadable,
                                              pathMessage("Cannot resolve workspace", candidate)});
    }

    std::filesystem::directory_iterator probe(canonical, error);
    if (error) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Unreadable,
                                              pathMessage("Cannot read workspace", canonical)});
    }

    return WorkspaceRoot(std::move(canonical));
}

const std::filesystem::path& WorkspaceRoot::path() const noexcept { return root_; }

bool WorkspaceRoot::contains(const std::filesystem::path& candidate) const {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(candidate, error);
    if (error) {
        return false;
    }

    auto rootPart = root_.begin();
    auto candidatePart = canonical.begin();
    while (rootPart != root_.end() && candidatePart != canonical.end() &&
           *rootPart == *candidatePart) {
        ++rootPart;
        ++candidatePart;
    }
    return rootPart == root_.end();
}

std::expected<std::filesystem::path, WorkspaceError>
WorkspaceRoot::resolveFile(const std::filesystem::path& candidate) const {
    const auto joined = candidate.is_absolute() ? candidate : root_ / candidate;
    std::error_code error;
    if (!std::filesystem::exists(joined, error) || error) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Missing,
                                              pathMessage("File does not exist", joined)});
    }

    auto canonical = std::filesystem::canonical(joined, error);
    if (error || !contains(canonical)) {
        return std::unexpected(WorkspaceError{
            WorkspaceErrorCode::OutsideRoot, pathMessage("File is outside the workspace", joined)});
    }
    if (!std::filesystem::is_regular_file(canonical, error) || error) {
        return std::unexpected(
            WorkspaceError{WorkspaceErrorCode::NotRegularFile,
                           pathMessage("Path is not a regular file", canonical)});
    }

    const auto status = std::filesystem::status(canonical, error);
    if (error || !filePermissionsAllowReading(status.permissions())) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Unreadable,
                                              pathMessage("File is not readable", canonical)});
    }

    std::ifstream probe(canonical, std::ios::binary);
    if (!probe) {
        return std::unexpected(WorkspaceError{WorkspaceErrorCode::Unreadable,
                                              pathMessage("Cannot read file", canonical)});
    }
    return canonical;
}

} // namespace omanotes
