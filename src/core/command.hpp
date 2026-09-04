#ifndef OMANOTES_CORE_COMMAND_HPP
#define OMANOTES_CORE_COMMAND_HPP

#include "core/buffer.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace omanotes {

/// Where keyboard focus sits when a command is asked for.
enum class FocusContext : std::uint8_t { Editor, Sidebar, Prompt, Other };

/// Everything a command may consult to decide whether it applies and what to
/// act on. Built fresh by the window for each invocation; commands never keep
/// a copy.
struct AppContext {
    FocusContext focus{FocusContext::Other};
    /// True when the active editor reports Vim Normal mode.
    bool normalMode{false};
    std::size_t bufferCount{0};
    bool activeBufferHasPath{false};
    bool activeBufferModified{false};
    /// A buffer named by a click or a tab, when the command acts on one
    /// buffer in particular rather than the active one.
    std::optional<BufferId> targetBuffer;
    /// A file named by a click or a tree activation.
    std::optional<std::filesystem::path> targetPath;
};

/// One named, user-visible action.
///
/// Every keyboard route, mouse route, and menu entry for an action runs the
/// same `execute`, so behaviour cannot drift between them. `enabled` is
/// consulted first and a disabled command is never run; `disabledHint`
/// explains why, in the words the status line will show.
struct CommandDescriptor {
    QString id;
    QString label;
    QString category;
    std::function<bool(const AppContext&)> enabled;
    std::function<void(AppContext&)> execute;
    QString disabledHint;
};

} // namespace omanotes

#endif // OMANOTES_CORE_COMMAND_HPP
