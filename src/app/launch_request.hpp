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

} // namespace omanotes

#endif // OMANOTES_APP_LAUNCH_REQUEST_HPP
