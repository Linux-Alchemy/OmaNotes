#ifndef OMANOTES_SESSION_SESSION_SNAPSHOT_HPP
#define OMANOTES_SESSION_SESSION_SNAPSHOT_HPP

#include "core/buffer.hpp"
#include "core/view_mode.hpp"
#include "workspace/workspace_root.hpp"

#include <QByteArray>
#include <QString>
#include <QUuid>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <vector>

namespace omanotes {

/// The format this build writes. Documents carrying a newer version are
/// refused; documents older than kOldestSessionFormatVersion are refused too.
/// Anything in between is read, with documented omissions defaulted.
inline constexpr std::uint32_t kSessionFormatVersion = 1;
inline constexpr std::uint32_t kOldestSessionFormatVersion = 1;

/// Hard ceilings on what a session document may contain. They bound parsing
/// time and memory against a corrupt or hostile state file, and the writer
/// must respect them too so what it writes can always be read back.
inline constexpr std::size_t kSessionMaxBytes = std::size_t{256} * 1024;
inline constexpr std::size_t kSessionMaxBuffers = 256;
inline constexpr std::size_t kSessionMaxPathBytes = 4096;
inline constexpr int kSessionMaxDimension = 32767;
inline constexpr int kSessionMaxPosition = 100'000'000;

/// Identity of one dirty-buffer recovery record, owned and stored separately
/// by Task 7.3's RecoveryStore. The structural snapshot only ever references a
/// record; it never contains buffer text.
using RecoveryId = QUuid;

struct WindowSnapshot {
    int width{0};
    int height{0};
    bool maximized{false};

    [[nodiscard]] bool operator==(const WindowSnapshot&) const = default;
};

struct SidebarSnapshot {
    bool visible{false};
    int width{0};
    /// The tree selection, relative to the workspace root; absent when
    /// nothing was selected.
    std::optional<std::filesystem::path> selectedPath;

    [[nodiscard]] bool operator==(const SidebarSnapshot&) const = default;
};

struct CursorSnapshot {
    int line{0};
    int column{0};

    [[nodiscard]] bool operator==(const CursorSnapshot&) const = default;
};

struct BufferSnapshot {
    BufferId id;
    /// Relative to the workspace root. Absent for a scratch buffer.
    std::optional<std::filesystem::path> path;
    ViewMode viewMode{ViewMode::Writing};
    CursorSnapshot cursor;
    int scrollLine{0};
    bool modified{false};
    /// Present exactly when modified: the record holding the unsaved text.
    std::optional<RecoveryId> recovery;

    [[nodiscard]] bool operator==(const BufferSnapshot&) const = default;
};

/// Structural state of one workspace's desk. Note contents are never here.
struct SessionSnapshot {
    std::uint32_t version{kSessionFormatVersion};
    std::filesystem::path workspaceRoot;
    WindowSnapshot window;
    SidebarSnapshot sidebar;
    std::vector<BufferSnapshot> buffers;
    std::optional<BufferId> activeBuffer;

    /// How many buffers hold unsaved work. Answerable from this document
    /// alone, without opening a recovery record.
    [[nodiscard]] std::size_t dirtyBufferCount() const noexcept;

    [[nodiscard]] bool operator==(const SessionSnapshot&) const = default;
};

enum class SessionErrorCode : std::uint8_t {
    Unreadable,
    Oversized,
    Malformed,
    UnsupportedVersion,
    FutureVersion,
    InvalidField,
    RootMismatch,
    StoreFailed
};

/// One parse or validation failure, phrased for the status line: which
/// document, which field, and what was wrong with it.
struct SessionError {
    SessionErrorCode code;
    QString location;
    QString message;

    [[nodiscard]] QString describe() const;
};

/// Serialize to the documented JSON form. Output is deterministic: the same
/// snapshot yields the same bytes, keys in sorted order, one trailing newline.
[[nodiscard]] QByteArray serializeSessionSnapshot(const SessionSnapshot& snapshot);

/// Parse and validate a document. Every field is checked against the limits
/// above; unknown fields, out-of-range values, non-relative or non-normal
/// paths, a version outside the supported range, and internal inconsistencies
/// (a dirty buffer without a recovery record, an active buffer that is not
/// listed) are all refused with a located diagnostic.
[[nodiscard]] std::expected<SessionSnapshot, SessionError>
parseSessionSnapshot(const QByteArray& bytes);

/// Read a document from disk, bounded by kSessionMaxBytes, and parse it. Never
/// creates, truncates, or rewrites anything: a bad file stays exactly as it was
/// found so it can be inspected. A missing file is reported as Unreadable.
[[nodiscard]] std::expected<SessionSnapshot, SessionError>
readSessionSnapshot(const std::filesystem::path& file);

/// Confirm the document belongs to this resolved root. A snapshot written for
/// another root is never restored here (ADR 0010).
[[nodiscard]] std::expected<void, SessionError> checkSessionRoot(const SessionSnapshot& snapshot,
                                                                 const WorkspaceRoot& root);

/// Resolve one stored relative path against the root through the same policy
/// that guards the sidebar and saves. Missing files come back as Missing so a
/// restorer can skip them; a symlink or mount that leaves the root comes back
/// as OutsideRoot and must not be opened.
[[nodiscard]] std::expected<std::filesystem::path, WorkspaceError>
resolveSessionPath(const WorkspaceRoot& root, const std::filesystem::path& relative);

} // namespace omanotes

#endif // OMANOTES_SESSION_SESSION_SNAPSHOT_HPP
