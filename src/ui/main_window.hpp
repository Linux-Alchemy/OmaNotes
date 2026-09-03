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
class QLineEdit;
class QStackedWidget;

namespace omanotes {

class BufferStrip;
class EditorAdapter;
class PrefixRouter;
class SaveCommand;
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
    void saveActiveBuffer();
    void beginNaming();
    void cancelNaming();
    void commitNaming();
    /// Save the active buffer to `requested`, or to its own path when empty.
    /// Returns an empty string on success, or the message to show the user.
    QString saveTo(const std::filesystem::path& requested);
    [[nodiscard]] EditorAdapter* activeEditor() const;
    EditorAdapter& createEditorFor(BufferId id);
    [[nodiscard]] bool handleBufferSwitch(const QKeyEvent& event);
    /// Intercept the editor's own write commands before its Vi mode can run
    /// them, so nothing reaches the disk except through the atomic writer.
    [[nodiscard]] bool interceptEditorWrite(QObject* watched, const QKeyEvent& event);
    QString saveActiveBufferOrReport();

    LaunchRequest launchRequest_;
    BufferRegistry buffers_;
    std::map<BufferId, std::unique_ptr<EditorAdapter>> editors_;
    std::unique_ptr<PrefixRouter> prefixRouter_;
    std::unique_ptr<SaveCommand> saveCommand_;
    Sidebar* sidebar_ = nullptr;
    QLineEdit* namePrompt_ = nullptr;
    QStackedWidget* editorStack_ = nullptr;
    BufferStrip* bufferStrip_ = nullptr;
    QLabel* statusArea_ = nullptr;
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
