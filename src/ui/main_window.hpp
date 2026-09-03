#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include "app/launch_request.hpp"
#include "core/buffer.hpp"
#include "core/buffer_registry.hpp"

#include <QMainWindow>

#include <filesystem>
#include <map>
#include <memory>

class QKeyEvent;
class QLabel;
class QStackedWidget;

namespace omanotes {

class BufferStrip;
class EditorAdapter;
class PrefixRouter;
class Sidebar;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(LaunchRequest launchRequest, QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void loadMarkdownFile(const std::filesystem::path& path);
    void refreshEditorStatus();
    void syncBufferStrip();
    void showBuffer(BufferId id);
    void openScratchBuffer();
    [[nodiscard]] EditorAdapter* activeEditor() const;
    EditorAdapter& createEditorFor(BufferId id);
    [[nodiscard]] bool handleBufferSwitch(const QKeyEvent& event);

    LaunchRequest launchRequest_;
    BufferRegistry buffers_;
    std::map<BufferId, std::unique_ptr<EditorAdapter>> editors_;
    std::unique_ptr<PrefixRouter> prefixRouter_;
    Sidebar* sidebar_ = nullptr;
    QStackedWidget* editorStack_ = nullptr;
    BufferStrip* bufferStrip_ = nullptr;
    QLabel* statusArea_ = nullptr;
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
