#ifndef OMANOTES_APP_LAUNCH_REQUEST_HPP
#define OMANOTES_APP_LAUNCH_REQUEST_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace omanotes {

enum class LaunchErrorCode : std::uint8_t { InvalidArguments, InvalidWorkspace, InvalidFile };

struct LaunchError {
    LaunchErrorCode code;
    std::string message;
};

struct LaunchRequest {
    std::filesystem::path root;
    std::optional<std::filesystem::path> requestedFile;
    bool bypassRestore{false};
};

[[nodiscard]] std::expected<LaunchRequest, LaunchError>
resolveLaunchRequest(std::span<const std::string_view> arguments,
                     const std::filesystem::path& currentDirectory);

/// A one-line caution when the workspace is the filesystem root or the home
/// directory: every Markdown file beneath it is then in scope for the tree,
/// search, and note links. Not a refusal (`omanotes ~` is a legitimate
/// choice, and Vim opens `/` without comment); just said out loud once.
/// `home` is injected so the rule can be tested; production passes the
/// user's home. Empty when the root is anything narrower.
[[nodiscard]] std::optional<std::string> wideRootNotice(const std::filesystem::path& canonicalRoot,
                                                        const std::filesystem::path& home);

} // namespace omanotes

#endif // OMANOTES_APP_LAUNCH_REQUEST_HPP
