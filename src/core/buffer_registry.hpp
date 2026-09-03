#ifndef OMANOTES_CORE_BUFFER_REGISTRY_HPP
#define OMANOTES_CORE_BUFFER_REGISTRY_HPP

#include "core/buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace omanotes {

enum class BufferErrorCode : std::uint8_t { NotFound, Modified, InvalidPath };

struct BufferError {
    BufferErrorCode code;
    std::string message;
};

/// Ordered registry of open buffers.
///
/// The registry owns buffer identity, order, and activation only. It never
/// stores document text, and it never writes to disk; it reads the filesystem
/// solely to canonicalise a path so that two routes to one file resolve to one
/// buffer.
class BufferRegistry final {
  public:
    /// Append and activate a new unnamed buffer.
    BufferId createScratch();

    /// Open `path`, or activate the existing buffer holding the same file.
    [[nodiscard]] std::expected<BufferId, BufferError> open(const std::filesystem::path& path);

    /// Activate an existing buffer. Returns false when `id` is unknown.
    bool activate(BufferId id);

    /// Close a buffer, refusing to discard unsaved work.
    [[nodiscard]] std::expected<void, BufferError> close(BufferId id);

    /// Close a buffer whose modifications the user has explicitly abandoned.
    [[nodiscard]] std::expected<void, BufferError> closeDiscardingChanges(BufferId id);

    [[nodiscard]] const std::vector<BufferState>& buffers() const noexcept;
    [[nodiscard]] std::optional<BufferId> activeId() const noexcept;
    [[nodiscard]] const BufferState* find(BufferId id) const noexcept;

    /// Identity of the buffer already holding `path`, if one is open. Unlike
    /// `open`, this never creates or activates a buffer.
    [[nodiscard]] std::optional<BufferId> findByPath(const std::filesystem::path& path) const;
    [[nodiscard]] std::optional<std::size_t> indexOf(BufferId id) const noexcept;
    [[nodiscard]] std::size_t count() const noexcept;

    /// Record the editor's modified state. Returns false when `id` is unknown.
    bool setModified(BufferId id, bool modified);

    /// Activate the next or previous buffer, wrapping at the ends as Vim does.
    std::optional<BufferId> activateNext();
    std::optional<BufferId> activatePrevious();

  private:
    [[nodiscard]] std::expected<void, BufferError> remove(BufferId id, bool discardChanges);
    std::optional<BufferId> activateOffset(std::ptrdiff_t offset);

    std::vector<BufferState> buffers_;
    std::optional<BufferId> activeId_;
};

} // namespace omanotes

#endif // OMANOTES_CORE_BUFFER_REGISTRY_HPP
