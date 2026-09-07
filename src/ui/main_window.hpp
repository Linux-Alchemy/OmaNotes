#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include "app/keymap.hpp"
#include "app/launch_request.hpp"
#include "core/buffer.hpp"
#include "core/buffer_registry.hpp"
#include "core/command.hpp"
#include "core/command_registry.hpp"
#include "persistence/conflict_detector.hpp"

#include <QByteArrayView>
#include <QMainWindow>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QKeyEvent;
class QLabel;
class QLineEdit;
class QMessageBox;
class QStackedWidget;

namespace omanotes {

class BufferStrip;
class EditorAdapter;
class FileWatcher;
class PrefixRouter;
class SaveCommand;
class Sidebar;
class SearchPalette;
class HelpOverlay;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(LaunchRequest launchRequest, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// The application's command table: every keyboard, mouse, and leader
    /// route runs one of these, and nothing else.
    [[nodiscard]] const CommandRegistry& commands() const noexcept;
    /// Commands nothing can reach, or that repeat a label. Empty means every
    /// action has exactly one implementation and at least one route.
    [[nodiscard]] std::vector<AuditFinding> auditCommands() const;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void registerCommands();
    void loadKeymap();
    [[nodiscard]] QString shortcutCommand(const QKeyEvent& event, const QWidget* target) const;
    /// Register `descriptor` and, when `sequences` is given, the leader keys
    /// that reach it. A registration failure is a programming error, so it
    /// aborts loudly rather than leaving an action silently unreachable.
    void addCommand(CommandDescriptor descriptor, std::initializer_list<QString> sequences = {});
    /// Where focus sits and what the active buffer looks like, right now.
    [[nodiscard]] AppContext currentContext() const;
    /// Run a registered command; a refusal goes to the status line.
    void runCommand(QStringView id);
    void runCommand(QStringView id, AppContext context);
    void closeBuffer(BufferId id, bool discardChanges);
    /// The mouse close affordance: a clean buffer closes outright; a dirty
    /// one comes to the front and asks Save / Discard / Cancel. Saving a
    /// scratch buffer routes through the save-as prompt and closes once named.
    void confirmCloseBuffer(BufferId id);
    void loadMarkdownFile(const std::filesystem::path& path);
    void refreshEditorStatus();
    void syncBufferStrip();
    void showBuffer(BufferId id);
    void openScratchBuffer();
    void saveActiveBuffer();
    /// Insert the system clipboard at the cursor, via the editor's own
    /// paste action so undo and encoding behave exactly as the editor's.
    void pasteFromClipboard();
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
    CommandRegistry commands_;
    Keymap keymap_;
    /// Command ids reached by a shortcut, click, or editor verb rather than a
    /// leader sequence; the audit counts these as routed.
    std::vector<QString> routedOutsideLeader_;
    std::map<BufferId, TrackedFile> tracked_;
    std::unique_ptr<FileWatcher> watcher_;
    std::map<BufferId, std::unique_ptr<EditorAdapter>> editors_;
    std::unique_ptr<PrefixRouter> prefixRouter_;
    std::unique_ptr<SaveCommand> saveCommand_;
    HelpOverlay* helpOverlay_ = nullptr;
    AppContext helpContext_;
    SearchPalette* searchPalette_ = nullptr;
    Sidebar* sidebar_ = nullptr;
    QLineEdit* namePrompt_ = nullptr;
    QStackedWidget* editorStack_ = nullptr;
    BufferStrip* bufferStrip_ = nullptr;
    QLabel* statusArea_ = nullptr;
    QMessageBox* closePrompt_ = nullptr;
    std::optional<BufferId> closePromptTarget_;
    std::optional<BufferId> closeAfterNaming_;
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
