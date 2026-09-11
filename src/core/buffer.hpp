#ifndef OMANOTES_CORE_BUFFER_HPP
#define OMANOTES_CORE_BUFFER_HPP

#include <QString>
#include <QUuid>

#include <filesystem>
#include <optional>

namespace omanotes {

using BufferId = QUuid;

/// Identity and presentation state for one open buffer.
///
/// Document text is deliberately absent: it stays owned by the editor bound to
/// the buffer, so the registry can never hold a second, divergent copy.
struct BufferState {
    BufferId id;
    std::optional<std::filesystem::path> path;
    QString displayName;
    bool modified{false};
};

/// Label shown for a buffer that has no file on disk yet.
inline QString scratchDisplayName() { return QStringLiteral("Untitled"); }

} // namespace omanotes

#endif // OMANOTES_CORE_BUFFER_HPP
