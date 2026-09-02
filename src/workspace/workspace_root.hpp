#ifndef OMANOTES_WORKSPACE_WORKSPACE_ROOT_HPP
#define OMANOTES_WORKSPACE_WORKSPACE_ROOT_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace omanotes {

enum class WorkspaceErrorCode : std::uint8_t {
    Missing,
    NotDirectory,
    NotRegularFile,
    Unreadable,
    OutsideRoot
};

struct WorkspaceError {
    WorkspaceErrorCode code;
    std::string message;
};

class WorkspaceRoot final {
  public:
    [[nodiscard]] static std::expected<WorkspaceRoot, WorkspaceError>
    resolve(const std::filesystem::path& candidate);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] bool contains(const std::filesystem::path& candidate) const;
    [[nodiscard]] std::expected<std::filesystem::path, WorkspaceError>
    resolveFile(const std::filesystem::path& candidate) const;

  private:
    explicit WorkspaceRoot(std::filesystem::path root);

    std::filesystem::path root_;
};

} // namespace omanotes

#endif // OMANOTES_WORKSPACE_WORKSPACE_ROOT_HPP
