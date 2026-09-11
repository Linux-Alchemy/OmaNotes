#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include "app/keymap.hpp"
#include "app/launch_request.hpp"
#include "core/buffer.hpp"
#include "core/buffer_registry.hpp"
#include "core/command.hpp"
#include "core/command_registry.hpp"
#include "core/view_mode.hpp"
#include "persistence/conflict_detector.hpp"
#include "persistence/recovery_store.hpp"
#include "session/session_restorer.hpp"
#include "session/session_snapshot.hpp"
#include "ui/theme_adapter.hpp"

#include <QByteArrayView>
#include <QMainWindow>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QCloseEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMessageBox;
class QSplitter;
class QStackedWidget;

namespace omanotes {

class BufferStrip;
class EditorAdapter;
class FileWatcher;
class MarkdownView;
class PrefixRouter;
class SaveCommand;
class Sidebar;
class SearchPalette;
class HelpOverlay;

class MainWindow final : public QMainWindow, public SessionHost {
    Q_OBJECT

  public:
    explicit MainWindow(LaunchRequest launchRequest, QWidget* parent = nullptr);
    /// As above, but reading the theme from `themeSources` instead of the
    /// desktop's — how tests dress the window in a fixture theme.
    MainWindow(LaunchRequest launchRequest, ThemeSources themeSources, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// The application's command table: every keyboard, mouse, and leader
    /// route runs one of these, and nothing else.
    [[nodiscard]] const CommandRegistry& commands() const noexcept;
    /// Commands nothing can reach, or that repeat a label. Empty means every
    /// action has exactly one implementation and at least one route.
    [[nodiscard]] std::vector<AuditFinding> auditCommands() const;

    [[nodiscard]] const BufferRegistry& buffers() const noexcept;
    void showStatus(const QString& message);
    [[nodiscard]] QString statusText() const;

    // --- Session (Phase 7): what the controller reads and the restorer drives.

    /// The desk as it stands: structural state only, recovery ids left for
    /// the controller to fill in.
    [[nodiscard]] SessionSnapshot captureSnapshot() const;
    /// A dirty buffer's text and provenance for a checkpoint; absent when the
    /// buffer is unknown or clean.
    [[nodiscard]] std::optional<BufferRecovery> dirtyRecord(BufferId id) const;
    /// The scratch buffer the constructor opened when nothing was requested,
    /// if it is still the only buffer and untouched.
    [[nodiscard]] std::optional<BufferId> untouchedInitialBuffer() const;
    /// Close `id` quietly if it is an empty, unmodified scratch buffer.
    void closeIfUntouched(BufferId id);
    /// Open, or bring to the front, the file named on the command line.
    void focusRequestedFile(const std::filesystem::path& path);

    // SessionHost
    void applyWindow(const WindowSnapshot& window) override;
    void applySidebar(const SidebarSnapshot& sidebar,
                      const std::optional<std::filesystem::path>& selected) override;
    std::optional<BufferId> openNote(const std::filesystem::path& absolute) override;
    BufferId openScratch() override;
    std::optional<BufferId> openRecovered(const BufferRecovery& record,
                                          const RecoveryPlan& plan) override;
    void applyBufferView(BufferId id, ViewMode mode, CursorSnapshot cursor,
                         int scrollLine) override;
    void activateBuffer(BufferId id) override;

  signals:
    /// The desk changed in a way a snapshot would record.
    void sessionStateChanged();
    /// A buffer was saved, reloaded from disk, or closed: its unsaved text,
    /// if any, is accounted for and its recovery record may go.
    void bufferResolved(omanotes::BufferId id);
    void windowDeactivated();
    void aboutToClose();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

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
    /// Flip the active buffer between writing and the read-only projection.
    void toggleReadingView();
    [[nodiscard]] ViewMode viewModeFor(BufferId id) const;
    void renderReadingView(BufferId id);
    void openScratchBuffer();
    void saveActiveBuffer();
    /// Insert the system clipboard at the cursor, via the editor's own
    /// paste action so undo and encoding behave exactly as the editor's.
    void pasteFromClipboard();
    /// Copy the editor's current selection to the system clipboard via the
    /// editor's own copy action.
    void copySelectionToClipboard();
    /// Hand the editor a synthetic Ctrl+key that the event filter would
    /// otherwise claim, so Vi sees it as its own.
    void forwardControlKeyToVi(Qt::Key key);
    /// Vi enters visual block on a synthetic Ctrl+V: the real key now means
    /// paste, and KTextEditor's Vi mode hardcodes Ctrl+V.
    void enterVisualBlock();
    /// Ctrl+D / Ctrl+U: Vi's own half page in the editor; the reading view,
    /// which has no Vi, scrolls half its height.
    void scrollHalfPage(int direction);
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
    /// Save every modified buffer that has a path, activating each in turn.
    /// Returns the first failure's message, or empty. An unnamed modified
    /// buffer fails: it needs `:w path.md` first.
    QString saveAllModified(bool force);
    /// `:q` and friends. With `discardChanges`, modified buffers are closed
    /// without saving and the window closes; otherwise unsaved work brings
    /// the quit prompt (Save / Discard / Cancel). SUPER+w never comes here:
    /// the compositor's kill is handled by checkpoint and restore instead.
    void quitApplication(bool discardChanges);
    /// Close every modified buffer without saving, then the window.
    void discardAllAndClose();
    /// Paint every region with the semantic palette: window chrome via one
    /// stylesheet, tree selection via palette groups (so the inactive
    /// selection dims), the reading view's colours, and each editor.
    void applyTheme(const ThemePalette& palette);
    /// Keep the accent bar over whichever pane owns focus.
    void markActivePane();

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
    std::unique_ptr<ThemeAdapter> theme_;
    /// Watches the theme sources, separately from the note watcher: a theme
    /// switch regenerates whole directories, and these paths are not buffers.
    std::unique_ptr<FileWatcher> themeWatcher_;
    QWidget* writingArea_ = nullptr;
    QSplitter* splitter_ = nullptr;
    Sidebar* sidebar_ = nullptr;
    QLineEdit* namePrompt_ = nullptr;
    QStackedWidget* editorStack_ = nullptr;
    BufferStrip* bufferStrip_ = nullptr;
    QLabel* statusArea_ = nullptr;
    MarkdownView* readingView_ = nullptr;
    std::map<BufferId, ViewMode> viewModes_;
    QMessageBox* closePrompt_ = nullptr;
    /// True while forwardControlKeyToVi's synthetic key is in flight, so the
    /// event filter lets it through to Vi instead of re-intercepting it.
    bool forwardingKeyToVi_ = false;
    /// The application-wide focus hook; disconnected before children are
    /// destroyed, or a focus change during teardown reaches dead panes.
    QMetaObject::Connection focusConnection_;
    std::optional<BufferId> closePromptTarget_;
    QMessageBox* quitPrompt_ = nullptr;
    std::optional<BufferId> closeAfterNaming_;
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
