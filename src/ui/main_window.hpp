#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include "app/launch_request.hpp"
#include "core/buffer.hpp"
#include "core/buffer_registry.hpp"
#include "persistence/conflict_detector.hpp"

#include <QByteArrayView>
#include <QMainWindow>

#include <cstdint>
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
class FileWatcher;
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
    /// Refuses to overwrite a file that changed on disk since it was read
    /// unless `force` is set. Returns an empty string on success, or the
    /// message to show the user.
    QString saveTo(const std::filesystem::path& requested, bool force = false);
    /// Replace the active buffer with its file's current content, as `:e` does.
    /// Refuses when the buffer has unsaved edits unless `discardEdits` is set.
    QString reloadActiveBuffer(bool discardEdits);
    void handleExternalChange(const std::filesystem::path& path);
    void trackFile(BufferId id, const std::filesystem::path& path, QByteArrayView contents);
    [[nodiscard]] EditorAdapter* activeEditor() const;
    EditorAdapter& createEditorFor(BufferId id);
    [[nodiscard]] bool handleBufferSwitch(const QKeyEvent& event);
    /// Intercept the editor's own file commands (`:w`, `:e`, and friends)
    /// before its Vi mode can run them, so nothing reaches the disk except
    /// through the atomic writer and nothing replaces a buffer unchecked.
    [[nodiscard]] bool interceptEditorFileCommand(QObject* watched, const QKeyEvent& event);
    QString saveActiveBufferOrReport(bool force = false);

    /// What the editor believes about a buffer's file, beyond its bytes.
    enum class DiskNote : std::uint8_t { InSync, ChangedOnDisk, Removed };
    struct TrackedFile {
        SavedRevision known;
        DiskNote note{DiskNote::InSync};
    };

    LaunchRequest launchRequest_;
    BufferRegistry buffers_;
    std::map<BufferId, TrackedFile> tracked_;
    std::unique_ptr<FileWatcher> watcher_;
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
