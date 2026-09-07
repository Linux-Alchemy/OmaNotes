#ifndef OMANOTES_CORE_VIEW_MODE_HPP
#define OMANOTES_CORE_VIEW_MODE_HPP

#include <cstdint>

namespace omanotes {

/// How one buffer is presented: the KTextEditor pane, or the read-only
/// Markdown projection. Per buffer, never global.
enum class ViewMode : std::uint8_t { Writing, Reading };

[[nodiscard]] constexpr ViewMode toggled(ViewMode mode) noexcept {
    return mode == ViewMode::Writing ? ViewMode::Reading : ViewMode::Writing;
}

} // namespace omanotes

#endif // OMANOTES_CORE_VIEW_MODE_HPP
