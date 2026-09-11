#include "app/launch_request.hpp"

#include "workspace/workspace_root.hpp"

#include <system_error>

namespace omanotes {

namespace {

LaunchError fromWorkspaceError(const WorkspaceError& error, LaunchErrorCode code) {
    return {code, error.message};
}

} // namespace

std::expected<LaunchRequest, LaunchError>
resolveLaunchRequest(std::span<const std::string_view> arguments,
                     const std::filesystem::path& currentDirectory) {
    bool bypassRestore = false;
    std::optional<std::filesystem::path> requestedPath;

    for (const auto argument : arguments) {
        if (argument == "--fresh") {
            bypassRestore = true;
            continue;
        }
        if (argument.starts_with('-')) {
            return std::unexpected(LaunchError{LaunchErrorCode::InvalidArguments,
                                               "Unknown option: " + std::string(argument)});
        }
        if (requestedPath.has_value()) {
            return std::unexpected(LaunchError{LaunchErrorCode::InvalidArguments,
                                               "Expected at most one workspace or file path"});
        }
        requestedPath = std::filesystem::path(argument);
    }

    auto currentRoot = WorkspaceRoot::resolve(currentDirectory);
    if (!currentRoot) {
        return std::unexpected(
            fromWorkspaceError(currentRoot.error(), LaunchErrorCode::InvalidWorkspace));
    }

    if (!requestedPath.has_value()) {
        return LaunchRequest{currentRoot->path(), std::nullopt, bypassRestore};
    }

    const auto candidate =
        requestedPath->is_absolute() ? *requestedPath : currentRoot->path() / *requestedPath;
    std::error_code error;
    const auto status = std::filesystem::status(candidate, error);
    if (error || !std::filesystem::exists(status)) {
        return std::unexpected(LaunchError{LaunchErrorCode::InvalidFile,
                                           "Launch path does not exist: " + candidate.string()});
    }

    if (std::filesystem::is_directory(status)) {
        auto root = WorkspaceRoot::resolve(candidate);
        if (!root) {
            return std::unexpected(
                fromWorkspaceError(root.error(), LaunchErrorCode::InvalidWorkspace));
        }
        return LaunchRequest{root->path(), std::nullopt, bypassRestore};
    }

    if (requestedPath->is_absolute()) {
        auto root = WorkspaceRoot::resolve(candidate.parent_path());
        if (!root) {
            return std::unexpected(
                fromWorkspaceError(root.error(), LaunchErrorCode::InvalidWorkspace));
        }
        auto file = root->resolveFile(candidate);
        if (!file) {
            return std::unexpected(fromWorkspaceError(file.error(), LaunchErrorCode::InvalidFile));
        }
        return LaunchRequest{root->path(), *file, bypassRestore};
    }

    auto file = currentRoot->resolveFile(*requestedPath);
    if (!file) {
        return std::unexpected(fromWorkspaceError(file.error(), LaunchErrorCode::InvalidFile));
    }
    return LaunchRequest{currentRoot->path(), *file, bypassRestore};
}

std::optional<std::string> wideRootNotice(const std::filesystem::path& canonicalRoot,
                                          const std::filesystem::path& home) {
    if (canonicalRoot == canonicalRoot.root_path()) {
        return "Workspace is the filesystem root; every Markdown file on the system is in scope";
    }
    std::error_code error;
    const auto canonicalHome = std::filesystem::weakly_canonical(home, error);
    if (!error && !home.empty() && canonicalRoot == canonicalHome) {
        return "Workspace is your home directory; every Markdown file beneath it is in scope";
    }
    return std::nullopt;
}

} // namespace omanotes
