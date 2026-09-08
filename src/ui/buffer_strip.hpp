#ifndef OMANOTES_UI_BUFFER_STRIP_HPP
#define OMANOTES_UI_BUFFER_STRIP_HPP

#include "core/buffer.hpp"

#include <QTabBar>

#include <optional>
#include <vector>

class QToolButton;

namespace omanotes {

/// Tab strip for the open buffers.
///
/// The strip is a view of the registry, never a second source of truth: it is
/// redrawn from registry state and reports user selections back by identity.
class BufferStrip final : public QTabBar {
    Q_OBJECT

  public:
    explicit BufferStrip(QWidget* parent = nullptr);

    void syncWith(const std::vector<BufferState>& buffers, std::optional<BufferId> activeId);

  signals:
    void bufferSelected(BufferId id);
    void bufferCloseRequested(BufferId id);

  private:
    [[nodiscard]] QToolButton* makeCloseButton();

    bool synchronising_ = false;
};

} // namespace omanotes

#endif // OMANOTES_UI_BUFFER_STRIP_HPP
