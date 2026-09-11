#include "ui/main_window.hpp"

#include "app/prefix_router.hpp"
#include "editor/editor_adapter.hpp"
#include "editor/ktext_editor_adapter.hpp"
#include "editor/save_command.hpp"
#include "persistence/document_store.hpp"
#include "ui/buffer_strip.hpp"
#include "ui/help_overlay.hpp"
#include "ui/markdown_view.hpp"
#include "ui/search_palette.hpp"
#include "ui/sidebar.hpp"

#include "workspace/file_watcher.hpp"
#include "workspace/workspace_root.hpp"
#include <KActionCollection>
#include <KTextEditor/View>
#include <QAction>

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QStringView>
#include <QStyle>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <expected>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

constexpr auto kWindowTitle = "OmaNotes";

QString displayName(const std::filesystem::path& path) {
    return QFile::decodeName(QByteArray::fromStdString(path.filename().native()));
}

bool isMarkdown(const std::filesystem::path& path) {
    return displayName(path.extension()).compare(QStringLiteral(".md"), Qt::CaseInsensitive) == 0;
}

/// The raw bytes of a note, refused when they cannot be text.
std::expected<QByteArray, QString> readNoteBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(QStringLiteral("Could not read %1").arg(displayName(path)));
    }
    const std::string bytes(std::istreambuf_iterator<char>(input), {});
    if (bytes.find('\0') != std::string::npos) {
        return std::unexpected(
            QStringLiteral("Refusing binary-looking file: %1").arg(displayName(path)));
    }
    return QByteArray::fromStdString(bytes);
}

std::expected<QString, QString> decodeNote(const QByteArray& bytes,
                                           const std::filesystem::path& path) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    if (decoder.hasError()) {
        return std::unexpected(
            QStringLiteral("File is not valid UTF-8: %1").arg(displayName(path)));
    }
    return text;
}

constexpr auto kAddBangToOverride = " (add ! to override)";

QWidget* buildWritingArea(QWidget* parent, QStackedWidget*& editors, BufferStrip*& buffers,
                          QToolButton*& sidebarToggle, QToolButton*& newBuffer, QLabel*& status,
                          QLineEdit*& namePrompt) {
    auto* writingArea = new QWidget(parent);
    writingArea->setObjectName(QStringLiteral("writingArea"));

    auto* layout = new QVBoxLayout(writingArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buffers = new BufferStrip(writingArea);

    // The mouse route to the sidebar: a flat glyph at the seam where the tree
    // meets the tabs, running the same toggle command as Space e.
    sidebarToggle = new QToolButton(writingArea);
    sidebarToggle->setText(QStringLiteral("\u00bb"));
    sidebarToggle->setObjectName(QStringLiteral("sidebarToggleButton"));
    sidebarToggle->setAccessibleName(QStringLiteral("Show or hide sidebar"));
    sidebarToggle->setToolTip(QStringLiteral("Show or hide sidebar (Space e)"));
    sidebarToggle->setAutoRaise(true);
    sidebarToggle->setFocusPolicy(Qt::NoFocus);

    newBuffer = new QToolButton(writingArea);
    newBuffer->setText(QStringLiteral("+"));
    newBuffer->setObjectName(QStringLiteral("newBufferButton"));
    newBuffer->setAccessibleName(QStringLiteral("New buffer"));
    newBuffer->setToolTip(QStringLiteral("New buffer"));
    newBuffer->setAutoRaise(true);
    newBuffer->setFocusPolicy(Qt::NoFocus);

    editors = new QStackedWidget(writingArea);
    editors->setObjectName(QStringLiteral("editorStack"));
    editors->setAccessibleName(QStringLiteral("Open documents"));

    status = new QLabel(writingArea);
    status->setObjectName(QStringLiteral("statusArea"));
    status->setAccessibleName(QStringLiteral("Editor status"));
    status->setContentsMargins(10, 6, 10, 6);
    // A long message must wrap here, not raise this pane's minimum width —
    // that would make the splitter steal space from the sidebar.
    status->setWordWrap(true);

    namePrompt = new QLineEdit(writingArea);
    namePrompt->setObjectName(QStringLiteral("namePrompt"));
    namePrompt->setAccessibleName(QStringLiteral("Save as path"));
    namePrompt->setPlaceholderText(QStringLiteral("path/inside/workspace.md"));
    namePrompt->setFrame(false);
    namePrompt->setContentsMargins(10, 6, 10, 6);
    namePrompt->hide();

    // The strip is one band in the pane's own colour (Matt's call,
    // 2026-09-10): tabs, the +, and the empty run after them all sit on the
    // ground the editor sits on; the active tab is told by its text alone.
    auto* bufferRow = new QWidget(writingArea);
    bufferRow->setObjectName(QStringLiteral("bufferRow"));
    bufferRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* stripRow = new QHBoxLayout(bufferRow);
    stripRow->setContentsMargins(0, 0, 0, 0);
    stripRow->setSpacing(0);
    stripRow->addWidget(sidebarToggle);
    stripRow->addWidget(buffers);
    stripRow->addWidget(newBuffer);
    stripRow->addStretch(1);
    layout->addWidget(bufferRow);
    layout->addWidget(editors, 1);
    layout->addWidget(namePrompt);
    layout->addWidget(status);
    return writingArea;
}

/// KTextEditor reports its mode as "VI: NORMAL", "VI: VISUAL LINE" and so on.
/// The status line drops the prefix and the shouting: "Normal", "Visual Line".
QString humanModeName(QString modeName) {
    static const auto viPrefix = QRegularExpression(QStringLiteral("^\\s*VI:\\s*"),
                                                    QRegularExpression::CaseInsensitiveOption);
    modeName.remove(viPrefix);
    auto words = modeName.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (auto& word : words) {
        word[0] = word[0].toUpper();
    }
    return words.join(QLatin1Char(' '));
}

} // namespace

MainWindow::MainWindow(LaunchRequest launchRequest, QWidget* parent)
    : MainWindow(std::move(launchRequest), ThemeAdapter::systemSources(), parent) {}

MainWindow::MainWindow(LaunchRequest launchRequest, ThemeSources themeSources, QWidget* parent)
    : QMainWindow(parent), launchRequest_(std::move(launchRequest)) {
    setObjectName(QStringLiteral("mainWindow"));
    const auto rootName =
        QFile::decodeName(QByteArray::fromStdString(launchRequest_.root.native()));
    setWindowTitle(QStringLiteral("%1 — %2").arg(QString::fromLatin1(kWindowTitle), rootName));
    setMinimumSize(720, 480);
    resize(1100, 720);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter_ = splitter;
    splitter->setObjectName(QStringLiteral("workspaceSplitter"));
    splitter->setAccessibleName(QStringLiteral("Workspace and editor panes"));
    splitter->setChildrenCollapsible(false);
    sidebar_ = new Sidebar(launchRequest_.root, splitter);
    splitter->addWidget(sidebar_);
    // Hidden at launch, at Matt's call (ADR 0007): the text is the point, and
    // `Space e` or `Ctrl+H` brings the tree when it is wanted. Phase 7's
    // session restore will remember whichever way it was left.
    sidebar_->hide();
    prefixRouter_ = std::make_unique<PrefixRouter>(LeaderKey::Space);
    watcher_ = std::make_unique<FileWatcher>();
    connect(watcher_.get(), &FileWatcher::fileChanged, this,
            [this](const std::filesystem::path& path) { handleExternalChange(path); });
    QToolButton* sidebarToggleButton = nullptr;
    QToolButton* newBufferButton = nullptr;
    writingArea_ = buildWritingArea(splitter, editorStack_, bufferStrip_, sidebarToggleButton,
                                    newBufferButton, statusArea_, namePrompt_);
    splitter->addWidget(writingArea_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 860});

    setCentralWidget(splitter);
    connect(splitter, &QSplitter::splitterMoved, this, [this] { emit sessionStateChanged(); });

    readingView_ = new MarkdownView(editorStack_);
    editorStack_->addWidget(readingView_);
    connect(readingView_, &MarkdownView::noteLinkActivated, this,
            [this](const std::filesystem::path& path) {
                auto context = currentContext();
                context.targetPath = path;
                runCommand(QStringLiteral("file.open"), std::move(context));
            });
    connect(readingView_, &MarkdownView::externalLinkOpened, this, [this](const QString& url) {
        statusArea_->setText(QStringLiteral("Opened %1 in the browser").arg(url));
    });
    connect(readingView_, &MarkdownView::linkRefused, this,
            [this](const QString& reason) { statusArea_->setText(reason); });
    connect(readingView_, &MarkdownView::linkTargetChanged, this, [this](const QString& target) {
        if (target.isEmpty()) {
            refreshEditorStatus();
        } else {
            statusArea_->setText(target);
        }
    });

    searchPalette_ = new SearchPalette(launchRequest_.root, this);
    connect(searchPalette_, &SearchPalette::matchChosen, this,
            [this](const std::filesystem::path& path, int line, int column) {
                auto context = currentContext();
                context.targetPath = path;
                runCommand(QStringLiteral("file.open"), std::move(context));
                const auto opened = buffers_.findByPath(path);
                if (line >= 0 && opened.has_value() && opened == buffers_.activeId()) {
                    if (auto* view = qobject_cast<KTextEditor::View*>(activeEditor()->widget())) {
                        view->setCursorPosition(KTextEditor::Cursor(line, column));
                    }
                }
            });
    helpOverlay_ = new HelpOverlay(this);
    connect(helpOverlay_, &HelpOverlay::commandChosen, this,
            [this](const QString& id) { runCommand(id, helpContext_); });
    auto* helpButton = new QToolButton(statusArea_->parentWidget());
    helpButton->setText(QStringLiteral("?"));
    helpButton->setObjectName(QStringLiteral("helpButton"));
    helpButton->setAccessibleName(QStringLiteral("Show commands"));
    helpButton->setToolTip(QStringLiteral("Show commands (Space ?)"));
    // Same for the status row: the mode, the ?, and the space between are
    // one band in the pane's colour, and the ? sits flat on it.
    auto* pane = statusArea_->parentWidget();
    auto* paneLayout = qobject_cast<QVBoxLayout*>(pane->layout());
    auto* statusRow = new QWidget(pane);
    statusRow->setObjectName(QStringLiteral("statusRow"));
    statusRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);
    paneLayout->removeWidget(statusArea_);
    statusLayout->addWidget(statusArea_, 1);
    statusLayout->addWidget(helpButton);
    paneLayout->addWidget(statusRow);
    connect(helpButton, &QToolButton::clicked, this,
            [this] { runCommand(QStringLiteral("help.show")); });
    registerCommands();
    keymap_ = Keymap::defaults(commands_);
    prefixRouter_->setResolver(
        [this](const QString& sequence) { return commands_.lookup(sequence); });
    connect(prefixRouter_.get(), &PrefixRouter::feedbackChanged, statusArea_,
            [this](const QString& message) { statusArea_->setText(message); });
    connect(prefixRouter_.get(), &PrefixRouter::sequenceAccepted, this,
            [this](const QString& commandId, const QString&) {
                // The "Space …" feedback has served its purpose; the mode
                // comes back, and the command may then say its own piece.
                refreshEditorStatus();
                runCommand(commandId);
            });
    connect(bufferStrip_, &BufferStrip::bufferSelected, this, [this](BufferId id) {
        auto context = currentContext();
        context.targetBuffer = id;
        runCommand(QStringLiteral("buffer.show"), std::move(context));
    });
    connect(bufferStrip_, &BufferStrip::bufferCloseRequested, this,
            [this](BufferId id) { confirmCloseBuffer(id); });
    connect(newBufferButton, &QToolButton::clicked, this,
            [this] { runCommand(QStringLiteral("buffer.new")); });
    connect(sidebarToggleButton, &QToolButton::clicked, this,
            [this] { runCommand(QStringLiteral("pane.sidebar.toggle")); });
    connect(sidebar_, &Sidebar::fileActivated, this, [this](const std::filesystem::path& path) {
        auto context = currentContext();
        context.targetPath = path;
        runCommand(QStringLiteral("file.open"), std::move(context));
    });
    connect(sidebar_, &Sidebar::editorFocusRequested, this,
            [this] { runCommand(QStringLiteral("pane.editor")); });
    connect(namePrompt_, &QLineEdit::returnPressed, this, [this] { commitNaming(); });
    saveCommand_ = std::make_unique<SaveCommand>([this](const QString& argument) {
        if (argument.isEmpty()) {
            const auto active = buffers_.activeId();
            const auto* state = active.has_value() ? buffers_.find(*active) : nullptr;
            if (state == nullptr || !state->path.has_value()) {
                return QStringLiteral("No file name; try :w path.md");
            }
            return saveTo({});
        }
        return saveTo(std::filesystem::path(argument.toStdString()));
    });

    if (auto* application = QApplication::instance(); application != nullptr) {
        application->installEventFilter(this);
    }

    if (launchRequest_.requestedFile.has_value()) {
        loadMarkdownFile(*launchRequest_.requestedFile);
    }
    if (buffers_.count() == 0) {
        // Either nothing was requested or the request was refused; the window
        // still opens on somewhere to type, exactly as the launch contract says.
        // A refusal message must survive the scratch buffer that replaces it,
        // or the user is told nothing about why their file did not open.
        const auto refusal = statusArea_->text();
        openScratchBuffer();
        if (!refusal.isEmpty()) {
            statusArea_->setText(refusal);
        }
    }
    loadKeymap();

    // A theme switch rewrites theme.name and regenerates the theme directory;
    // an edit to colors.toml or shell.toml changes one file in place. Watching
    // all three catches every route — theme.name's parent directory is the
    // stable one, so it survives the regeneration — and the watcher's quiet
    // period coalesces the burst into a single refresh.
    themeWatcher_ = std::make_unique<FileWatcher>();
    themeWatcher_->watch(themeSources.stateDir / "theme.name");
    themeWatcher_->watch(themeSources.stateDir / "theme" / "colors.toml");
    themeWatcher_->watch(themeSources.configDir / "shell.toml");
    theme_ = std::make_unique<ThemeAdapter>(std::move(themeSources));
    applyTheme(theme_->currentPalette());
    connect(theme_.get(), &ThemeAdapter::paletteChanged, this,
            [this](const ThemePalette& palette) { applyTheme(palette); });
    connect(themeWatcher_.get(), &FileWatcher::fileChanged, this,
            [this](const std::filesystem::path&) { theme_->refresh(); });
    if (auto* application = qobject_cast<QApplication*>(QApplication::instance());
        application != nullptr) {
        focusConnection_ = connect(application, &QApplication::focusChanged, this,
                                   [this](QWidget*, QWidget*) { markActivePane(); });
    }
    markActivePane();
}

MainWindow::~MainWindow() {
    disconnect(focusConnection_);
    if (auto* application = QApplication::instance(); application != nullptr) {
        application->removeEventFilter(this);
    }
}

const BufferRegistry& MainWindow::buffers() const noexcept { return buffers_; }

void MainWindow::showStatus(const QString& message) { statusArea_->setText(message); }

QString MainWindow::statusText() const { return statusArea_->text(); }

bool MainWindow::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate) {
        emit windowDeactivated();
    }
    return QMainWindow::event(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // No prompt for dirty buffers: their text is checkpointed on the way out
    // and comes back dirty next time (docs/session-format.md).
    emit aboutToClose();
    QMainWindow::closeEvent(event);
}

namespace {

std::optional<std::filesystem::path> relativeToRoot(const std::filesystem::path& root,
                                                    const std::filesystem::path& absolute) {
    auto relative = absolute.lexically_relative(root);
    if (relative.empty() || relative.begin()->string() == "..") {
        return std::nullopt;
    }
    return relative;
}

} // namespace

SessionSnapshot MainWindow::captureSnapshot() const {
    SessionSnapshot snapshot;
    snapshot.workspaceRoot = launchRequest_.root;
    snapshot.window = {std::max(size().width(), 1), std::max(size().height(), 1), isMaximized()};
    snapshot.sidebar.visible = sidebar_->isVisible();
    snapshot.sidebar.width = splitter_ != nullptr ? std::max(splitter_->sizes().value(0), 0) : 0;
    if (const auto selected = sidebar_->selectedPath(); selected) {
        snapshot.sidebar.selectedPath = relativeToRoot(launchRequest_.root, *selected);
    }
    for (const auto& state : buffers_.buffers()) {
        BufferSnapshot buffer;
        buffer.id = state.id;
        if (state.path) {
            buffer.path = relativeToRoot(launchRequest_.root, *state.path);
            if (!buffer.path) {
                continue;
            }
        }
        buffer.viewMode = viewModeFor(state.id);
        if (const auto editor = editors_.find(state.id); editor != editors_.end()) {
            const auto position = editor->second->cursorPosition();
            buffer.cursor = {std::max(position.line, 0), std::max(position.column, 0)};
            buffer.scrollLine = std::max(editor->second->firstVisibleLine(), 0);
            buffer.modified = editor->second->isModified();
        }
        snapshot.buffers.push_back(std::move(buffer));
    }
    if (const auto active = buffers_.activeId(); active) {
        const auto listed = std::ranges::any_of(
            snapshot.buffers, [&active](const auto& buffer) { return buffer.id == *active; });
        if (listed) {
            snapshot.activeBuffer = *active;
        }
    }
    return snapshot;
}

std::optional<BufferRecovery> MainWindow::dirtyRecord(BufferId id) const {
    const auto* state = buffers_.find(id);
    const auto editor = editors_.find(id);
    if (state == nullptr || editor == editors_.end() || !editor->second->isModified()) {
        return std::nullopt;
    }
    BufferRecovery record;
    record.contents = editor->second->text();
    if (state->path) {
        record.path = relativeToRoot(launchRequest_.root, *state->path);
        if (!record.path) {
            return std::nullopt;
        }
        if (const auto tracked = tracked_.find(id); tracked != tracked_.end()) {
            record.baseRevision = tracked->second.known;
        }
    }
    return record;
}

std::optional<BufferId> MainWindow::untouchedInitialBuffer() const {
    if (buffers_.count() != 1) {
        return std::nullopt;
    }
    const auto& only = buffers_.buffers().front();
    const auto editor = editors_.find(only.id);
    if (only.path || editor == editors_.end() || editor->second->isModified() ||
        !editor->second->text().isEmpty()) {
        return std::nullopt;
    }
    return only.id;
}

void MainWindow::closeIfUntouched(BufferId id) {
    const auto* state = buffers_.find(id);
    const auto editor = editors_.find(id);
    if (state == nullptr || state->path || editor == editors_.end() ||
        editor->second->isModified() || !editor->second->text().isEmpty() || buffers_.count() < 2) {
        return;
    }
    const auto status = statusArea_->text();
    closeBuffer(id, false);
    statusArea_->setText(status);
}

void MainWindow::focusRequestedFile(const std::filesystem::path& path) { loadMarkdownFile(path); }

void MainWindow::applyWindow(const WindowSnapshot& window) {
    resize(std::max(window.width, minimumWidth()), std::max(window.height, minimumHeight()));
    if (window.maximized) {
        setWindowState(windowState() | Qt::WindowMaximized);
    }
}

void MainWindow::applySidebar(const SidebarSnapshot& sidebar,
                              const std::optional<std::filesystem::path>& selected) {
    sidebar.visible ? sidebar_->show() : sidebar_->hide();
    if (sidebar.width > 0 && splitter_ != nullptr) {
        const auto total = std::max(splitter_->width(), sidebar.width + 1);
        splitter_->setSizes({sidebar.width, total - sidebar.width});
    }
    if (selected) {
        sidebar_->selectPath(*selected);
    }
}

std::optional<BufferId> MainWindow::openNote(const std::filesystem::path& absolute) {
    const auto status = statusArea_->text();
    loadMarkdownFile(absolute);
    const auto opened = buffers_.findByPath(absolute);
    if (!opened) {
        // loadMarkdownFile said why; the restorer will report it too.
        return std::nullopt;
    }
    statusArea_->setText(status);
    return opened;
}

BufferId MainWindow::openScratch() {
    const auto id = buffers_.createScratch();
    createEditorFor(id);
    showBuffer(id);
    return id;
}

std::optional<BufferId> MainWindow::openRecovered(const BufferRecovery& record,
                                                  const RecoveryPlan& plan) {
    BufferId id;
    if (plan.target == RecoveryTarget::Scratch || !plan.resolved) {
        id = buffers_.createScratch();
    } else {
        const auto opened = buffers_.open(*plan.resolved);
        if (!opened) {
            return std::nullopt;
        }
        id = *opened;
    }
    auto editor = editors_.find(id);
    if (editor == editors_.end()) {
        createEditorFor(id);
        editor = editors_.find(id);
    }
    editor->second->setText(record.contents);
    editor->second->markModified();
    if (plan.resolved) {
        // What the buffer last knew of the disk is what the record knew:
        // if the note changed since, :w refuses exactly as after any
        // external change (ADR 0006), and :w! or :e! settles it.
        DiskNote note = DiskNote::InSync;
        switch (plan.target) {
        case RecoveryTarget::FileChangedOnDisk:
            note = DiskNote::ChangedOnDisk;
            break;
        case RecoveryTarget::FileMissing:
            note = DiskNote::Removed;
            break;
        case RecoveryTarget::Scratch:
        case RecoveryTarget::File:
            break;
        }
        tracked_[id] = TrackedFile{record.baseRevision.value_or(SavedRevision{}), note};
        std::error_code error;
        if (std::filesystem::exists(*plan.resolved, error)) {
            watcher_->watch(*plan.resolved);
        }
    }
    showBuffer(id);
    return id;
}

void MainWindow::applyBufferView(BufferId id, ViewMode mode, CursorSnapshot cursor,
                                 int scrollLine) {
    viewModes_[id] = mode;
    if (const auto editor = editors_.find(id); editor != editors_.end()) {
        editor->second->setCursorPosition({cursor.line, cursor.column});
        editor->second->scrollToLine(scrollLine);
    }
}

void MainWindow::activateBuffer(BufferId id) {
    if (buffers_.activate(id)) {
        showBuffer(id);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (forwardingKeyToVi_) {
        return false;
    }
    const auto* watchedWidget = qobject_cast<QWidget*>(watched);
    const auto insideWindow =
        watchedWidget != nullptr && (watchedWidget == this || isAncestorOf(watchedWidget));
    // KTextEditor puts its own `:` command line and `/` search bar inside the
    // view. Those are text inputs: routing application keys out of them would
    // eat the space in `:w my note.md` and turn Shift+H into a buffer switch
    // in the middle of a search. Application keys belong to the text area only.
    const auto typingElsewhere = qobject_cast<const QLineEdit*>(watchedWidget) != nullptr;
    const auto insideEditor =
        watchedWidget != nullptr && editorStack_ != nullptr && !typingElsewhere &&
        (watchedWidget == editorStack_ || editorStack_->isAncestorOf(watchedWidget));

    if (!prefixRouter_->isPending() &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto id = shortcutCommand(*static_cast<QKeyEvent*>(event), watchedWidget);
        if (!id.isEmpty()) {
            if (event->type() == QEvent::KeyPress) {
                prefixRouter_->cancelPending();
                runCommand(id);
            }
            event->accept();
            return true;
        }
    }

    if (insideWindow &&
        (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate)) {
        prefixRouter_->cancelPending();
    }

    if (watched == namePrompt_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        cancelNaming();
        return true;
    }

    if (event->type() == QEvent::KeyPress &&
        interceptEditorFileCommand(watched, *static_cast<QKeyEvent*>(event))) {
        return true;
    }

    if (insideWindow && event->type() == QEvent::MouseButtonPress) {
        prefixRouter_->cancelPending();
    } else if (insideWindow && sidebar_->isAncestorOf(watchedWidget) &&
               event->type() == QEvent::KeyPress) {
        if (prefixRouter_->route(*static_cast<QKeyEvent*>(event), EditorMode::Normal)) {
            return true;
        }
    } else if (insideEditor && event->type() == QEvent::KeyPress) {
        auto& keyEvent = *static_cast<QKeyEvent*>(event);
        auto* editor = activeEditor();
        if (editor != nullptr && prefixRouter_->route(keyEvent, editor->mode())) {
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::loadKeymap() {
    std::vector<QKeySequence> reserved;
    if (auto* editor = activeEditor(); editor != nullptr) {
        if (auto* view = qobject_cast<KTextEditor::View*>(editor->widget())) {
            for (const auto* action : view->actionCollection()->actions()) {
                for (const auto& sequence : action->shortcuts()) {
                    reserved.push_back(sequence);
                }
            }
        }
    }
    const auto configured = Keymap::load(Keymap::configurationPath(), commands_, reserved);
    if (!configured) {
        auto* warning = new QLabel(writingArea_);
        warning->setObjectName(QStringLiteral("keymapWarning"));
        warning->setAccessibleName(QStringLiteral("Keymap configuration error"));
        warning->setTextFormat(Qt::PlainText);
        warning->setWordWrap(true);
        warning->setText(QStringLiteral("Default keys are active. %1: %2")
                             .arg(configured.error().location, configured.error().message));
        qobject_cast<QVBoxLayout*>(writingArea_->layout())->addWidget(warning);
        qWarning("%s", qPrintable(warning->text()));
        return;
    }
    if (const auto applied = configured->applyTo(commands_); !applied) {
        qFatal("Validated keymap could not be applied");
    }
    keymap_ = *configured;
}

QString MainWindow::shortcutCommand(const QKeyEvent& event, const QWidget* target) const {
    if (target == nullptr || target->window() != this ||
        qobject_cast<const QLineEdit*>(target) != nullptr) {
        return {};
    }
    // Omarchy's universal clipboard chords are compositor-injected Ctrl
    // events delivered while the physical Super key is still held, so they
    // arrive as Ctrl+Meta combinations. Super belongs to the desktop, never
    // to an application shortcut: ignore it when matching.
    const auto combination = event.keyCombination();
    auto id = keymap_.commandFor(QKeySequence(
        QKeyCombination(combination.keyboardModifiers() & ~Qt::MetaModifier, combination.key())));
    if (id.isEmpty()) {
        return {};
    }
    if (event.key() == Qt::Key_L && event.modifiers() == Qt::ControlModifier &&
        !sidebar_->isAncestorOf(target)) {
        return {};
    }
    const auto* editor = activeEditor();
    const auto inEditor = editorStack_->isAncestorOf(target);
    const auto normal = editor != nullptr && editor->mode() == EditorMode::Normal;
    if (event.modifiers() == Qt::ShiftModifier && (!inEditor || !normal || buffers_.count() < 2)) {
        return {};
    }
    // Omarchy's Super+V arrives as a clean Ctrl+V — clipboard.lua's
    // send_key_state exists precisely to keep the held Super out of the
    // injected chord — so the two are indistinguishable here. Matt's rule
    // (2026-09-07): universal paste wins in every mode; Vi's visual block
    // moves to Ctrl+Q, gvim's classic answer to this exact collision.
    if (id == QStringLiteral("edit.paste") && (!inEditor || editor == nullptr)) {
        return {};
    }
    if (id == QStringLiteral("editor.visual-block") &&
        (!inEditor || !normal || editorStack_->currentWidget() == readingView_)) {
        return {};
    }
    // Copy runs only over a selection; without one, Ctrl+C stays Vi's abort.
    // The selection that counts is the visible pane's: the reading view's
    // when it is showing, the editor's otherwise.
    if (id == QStringLiteral("edit.copy")) {
        if (!inEditor) {
            return {};
        }
        if (editorStack_->currentWidget() == readingView_) {
            if (!readingView_->textCursor().hasSelection()) {
                return {};
            }
        } else {
            auto* adapter = activeEditor();
            const auto* view = adapter != nullptr && adapter->widget() != nullptr
                                   ? qobject_cast<const KTextEditor::View*>(adapter->widget())
                                   : nullptr;
            if (view == nullptr || !view->selection()) {
                return {};
            }
        }
    }
    // Only Save and the clipboard routes are application shortcuts while
    // editing Insert/Visual text. Plain typing, command bars, search fields
    // and dialog controls keep their keys.
    if (inEditor && !normal && id != QStringLiteral("file.save") &&
        id != QStringLiteral("edit.paste") && id != QStringLiteral("edit.copy")) {
        return {};
    }
    return id;
}

const CommandRegistry& MainWindow::commands() const noexcept { return commands_; }

std::vector<AuditFinding> MainWindow::auditCommands() const {
    return commands_.audit(routedOutsideLeader_);
}

void MainWindow::addCommand(CommandDescriptor descriptor,
                            std::initializer_list<QString> sequences) {
    const auto id = descriptor.id;
    if (const auto added = commands_.add(std::move(descriptor)); !added) {
        qFatal("Command registration failed: %s", qPrintable(added.error().message));
    }
    for (const auto& sequence : sequences) {
        if (const auto bound = commands_.bind(LeaderSequence{sequence}, id); !bound) {
            qFatal("Command binding failed: %s", qPrintable(bound.error().message));
        }
    }
}

void MainWindow::registerCommands() {
    // Leader sequences follow Matt's LazyVim vocabulary where one exists:
    // `e` toggles the explorer, `b d` / `b D` delete a buffer, `f f` and a
    // second Space find files, `f n` is a new file, `/` is text search. `?` and `m` come from the
    // plan. Reached by other routes too: Ctrl+S, Ctrl+H, Shift+H/L, tab, tab
    // close-button, + button and tree clicks; those routes name the command
    // ids in routedOutsideLeader_.
    const auto always = [](const AppContext&) { return true; };
    const auto severalBuffers = [](const AppContext& context) { return context.bufferCount > 1; };
    const auto inNormalMode = [](const AppContext& context) { return context.normalMode; };

    addCommand({QStringLiteral("file.save"),
                QStringLiteral("Save"),
                QStringLiteral("file"),
                always,
                [this](AppContext&) { saveActiveBuffer(); },
                {}});
    routedOutsideLeader_.push_back(QStringLiteral("file.save"));

    // Omarchy's Super+V arrives as a literal Ctrl+V (universal paste). It
    // pastes while typing; Normal and Visual keep Ctrl+V as Vi's visual
    // block, per Matt's call of 2026-09-07.
    addCommand({QStringLiteral("edit.paste"),
                QStringLiteral("Paste from clipboard"),
                QStringLiteral("edit"),
                always,
                [this](AppContext&) { pasteFromClipboard(); },
                {}});
    routedOutsideLeader_.push_back(QStringLiteral("edit.paste"));
    addCommand({QStringLiteral("edit.copy"),
                QStringLiteral("Copy selection"),
                QStringLiteral("edit"),
                always,
                [this](AppContext&) { copySelectionToClipboard(); },
                {}});
    routedOutsideLeader_.push_back(QStringLiteral("edit.copy"));
    addCommand({QStringLiteral("editor.visual-block"), QStringLiteral("Visual block (Vi)"),
                QStringLiteral("edit"), inNormalMode, [this](AppContext&) { enterVisualBlock(); },
                QStringLiteral("Visual block starts from Normal mode")});
    routedOutsideLeader_.push_back(QStringLiteral("editor.visual-block"));
    addCommand({QStringLiteral("view.half-page-down"), QStringLiteral("Half page down"),
                QStringLiteral("view"), inNormalMode, [this](AppContext&) { scrollHalfPage(+1); },
                QStringLiteral("Half-page scrolling starts from Normal mode")});
    routedOutsideLeader_.push_back(QStringLiteral("view.half-page-down"));
    addCommand({QStringLiteral("view.half-page-up"), QStringLiteral("Half page up"),
                QStringLiteral("view"), inNormalMode, [this](AppContext&) { scrollHalfPage(-1); },
                QStringLiteral("Half-page scrolling starts from Normal mode")});
    routedOutsideLeader_.push_back(QStringLiteral("view.half-page-up"));
    // Listed so the way out is discoverable, and worded as Matt asked
    // (2026-09-10). The route is `:q`; choosing it here runs the same quit,
    // prompt and all.
    addCommand({QStringLiteral("app.quit"),
                QStringLiteral("IYKYK"),
                QStringLiteral("app"),
                always,
                [this](AppContext&) { quitApplication(false); },
                {}});
    routedOutsideLeader_.push_back(QStringLiteral("app.quit"));

    addCommand({QStringLiteral("file.open"), QStringLiteral("Open file"), QStringLiteral("file"),
                [](const AppContext& context) { return context.targetPath.has_value(); },
                [this](AppContext& context) { loadMarkdownFile(*context.targetPath); },
                QStringLiteral("Choose a file in the sidebar to open it")});
    routedOutsideLeader_.push_back(QStringLiteral("file.open"));

    addCommand({QStringLiteral("buffer.new"),
                QStringLiteral("New buffer"),
                QStringLiteral("buffer"),
                always,
                [this](AppContext&) { openScratchBuffer(); },
                {}},
               {QStringLiteral("f n")});
    routedOutsideLeader_.push_back(QStringLiteral("buffer.new"));
    addCommand({QStringLiteral("buffer.close"),
                QStringLiteral("Close buffer"),
                QStringLiteral("buffer"),
                always,
                [this](AppContext& context) {
                    const auto id = context.targetBuffer.has_value() ? context.targetBuffer
                                                                     : buffers_.activeId();
                    if (id.has_value()) {
                        closeBuffer(*id, false);
                    }
                },
                {}},
               {QStringLiteral("b d")});
    routedOutsideLeader_.push_back(QStringLiteral("buffer.close"));
    addCommand({QStringLiteral("buffer.close.discard"),
                QStringLiteral("Close buffer, discard"),
                QStringLiteral("buffer"),
                always,
                [this](AppContext& context) {
                    const auto id = context.targetBuffer.has_value() ? context.targetBuffer
                                                                     : buffers_.activeId();
                    if (id.has_value()) {
                        closeBuffer(*id, true);
                    }
                },
                {}},
               {QStringLiteral("b D")});
    addCommand({QStringLiteral("buffer.next"), QStringLiteral("Next buffer"),
                QStringLiteral("buffer"), severalBuffers,
                [this](AppContext&) {
                    if (const auto target = buffers_.activateNext(); target.has_value()) {
                        showBuffer(*target);
                    }
                },
                QStringLiteral("Only one buffer is open")},
               {QStringLiteral("b n")});
    addCommand({QStringLiteral("buffer.previous"), QStringLiteral("Previous buffer"),
                QStringLiteral("buffer"), severalBuffers,
                [this](AppContext&) {
                    if (const auto target = buffers_.activatePrevious(); target.has_value()) {
                        showBuffer(*target);
                    }
                },
                QStringLiteral("Only one buffer is open")},
               {QStringLiteral("b p")});
    routedOutsideLeader_.push_back(QStringLiteral("buffer.next"));
    routedOutsideLeader_.push_back(QStringLiteral("buffer.previous"));
    addCommand({QStringLiteral("buffer.show"), QStringLiteral("Show buffer"),
                QStringLiteral("buffer"),
                [](const AppContext& context) { return context.targetBuffer.has_value(); },
                [this](AppContext& context) {
                    if (buffers_.activate(*context.targetBuffer)) {
                        showBuffer(*context.targetBuffer);
                    }
                },
                QStringLiteral("Choose a buffer tab to show it")});
    routedOutsideLeader_.push_back(QStringLiteral("buffer.show"));

    addCommand({QStringLiteral("pane.sidebar"), QStringLiteral("Focus sidebar"),
                QStringLiteral("pane"), inNormalMode,
                [this](AppContext&) {
                    prefixRouter_->cancelPending();
                    sidebar_->show();
                    sidebar_->focusTree();
                    emit sessionStateChanged();
                },
                QStringLiteral("Leave Insert mode to move to the sidebar")});
    routedOutsideLeader_.push_back(QStringLiteral("pane.sidebar"));
    addCommand({QStringLiteral("pane.sidebar.toggle"),
                QStringLiteral("Show or hide sidebar"),
                QStringLiteral("pane"),
                always,
                [this](AppContext& context) {
                    // LazyVim's explorer toggle: opening also moves focus
                    // there; closing hands focus back to the text.
                    if (sidebar_->isVisible()) {
                        sidebar_->hide();
                        if (context.focus == FocusContext::Sidebar) {
                            runCommand(QStringLiteral("pane.editor"));
                        }
                        emit sessionStateChanged();
                        return;
                    }
                    sidebar_->show();
                    sidebar_->focusTree();
                    emit sessionStateChanged();
                },
                {}},
               {QStringLiteral("e")});
    addCommand({QStringLiteral("pane.editor"),
                QStringLiteral("Focus editor"),
                QStringLiteral("pane"),
                always,
                [this](AppContext&) {
                    if (auto* editor = activeEditor();
                        editor != nullptr && editor->widget() != nullptr) {
                        editor->widget()->setFocus(Qt::ShortcutFocusReason);
                    }
                },
                {}});
    routedOutsideLeader_.push_back(QStringLiteral("pane.editor"));

    addCommand({QStringLiteral("search.files"),
                QStringLiteral("Find files"),
                QStringLiteral("search"),
                always,
                [this](AppContext&) { searchPalette_->begin(SearchKind::Files); },
                {}},
               {QStringLiteral("f f"), QStringLiteral("Space")});
    addCommand({QStringLiteral("search.text"),
                QStringLiteral("Search text"),
                QStringLiteral("search"),
                always,
                [this](AppContext&) { searchPalette_->begin(SearchKind::Text); },
                {}},
               {QStringLiteral("/")});
    addCommand({QStringLiteral("help.show"),
                QStringLiteral("Help"),
                QStringLiteral("help"),
                always,
                [this](AppContext& context) {
                    helpContext_ = context;
                    // `:q` is a Vi command line verb, not a key sequence, so
                    // the keymap has no label for it; the help shows it anyway.
                    auto routes = keymap_.shortcutLabels();
                    routes.emplace(QStringLiteral("app.quit"), QStringLiteral(":q"));
                    helpOverlay_->showCommands(commands_, context, routes);
                },
                {}},
               {QStringLiteral("?")});
    addCommand({QStringLiteral("view.reading"),
                QStringLiteral("Reading view"),
                QStringLiteral("view"),
                always,
                [this](AppContext&) { toggleReadingView(); },
                {}},
               {QStringLiteral("m")});
}

AppContext MainWindow::currentContext() const {
    AppContext context;
    const auto* focus = QApplication::focusWidget();
    const auto* editor = activeEditor();
    if (focus != nullptr && focus == namePrompt_) {
        context.focus = FocusContext::Prompt;
    } else if (focus != nullptr && sidebar_ != nullptr &&
               (focus == sidebar_ || sidebar_->isAncestorOf(focus))) {
        context.focus = FocusContext::Sidebar;
    } else if (focus != nullptr && editorStack_ != nullptr &&
               (focus == editorStack_ || editorStack_->isAncestorOf(focus))) {
        context.focus = FocusContext::Editor;
    }
    context.normalMode = editor != nullptr && editor->mode() == EditorMode::Normal;
    context.bufferCount = buffers_.count();
    const auto active = buffers_.activeId();
    const auto* state = active.has_value() ? buffers_.find(*active) : nullptr;
    context.activeBufferHasPath = state != nullptr && state->path.has_value();
    context.activeBufferModified = state != nullptr && state->modified;
    return context;
}

void MainWindow::runCommand(QStringView id) { runCommand(id, currentContext()); }

void MainWindow::runCommand(QStringView id, AppContext context) {
    if (const auto result = commands_.execute(id, context); !result) {
        statusArea_->setText(result.error().message);
    }
}

void MainWindow::confirmCloseBuffer(BufferId id) {
    const auto* state = buffers_.find(id);
    if (state == nullptr) {
        return;
    }
    if (!state->modified) {
        auto context = currentContext();
        context.targetBuffer = id;
        runCommand(QStringLiteral("buffer.close"), std::move(context));
        return;
    }

    // Bring the buffer being judged to the front before asking about it.
    if (buffers_.activate(id)) {
        showBuffer(id);
    }
    if (closePrompt_ == nullptr) {
        closePrompt_ =
            new QMessageBox(QMessageBox::Question, QString{}, QString{},
                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        closePrompt_->setObjectName(QStringLiteral("closeBufferPrompt"));
        closePrompt_->setDefaultButton(QMessageBox::Save);
        // Plain text, no platform-theme stock icons on the buttons.
        for (auto* button : closePrompt_->buttons()) {
            button->setIcon(QIcon());
        }
        connect(closePrompt_, &QMessageBox::finished, this, [this](int result) {
            const auto target = closePromptTarget_;
            closePromptTarget_.reset();
            if (!target.has_value() || buffers_.find(*target) == nullptr) {
                return;
            }
            auto context = currentContext();
            context.targetBuffer = *target;
            if (result == QMessageBox::Discard) {
                runCommand(QStringLiteral("buffer.close.discard"), std::move(context));
                return;
            }
            if (result != QMessageBox::Save) {
                return;
            }
            if (const auto* chosen = buffers_.find(*target); !chosen->path.has_value()) {
                closeAfterNaming_ = *target;
                beginNaming();
            } else if (const auto failure = saveTo({}); failure.isEmpty()) {
                runCommand(QStringLiteral("buffer.close"), std::move(context));
            } else {
                statusArea_->setText(failure);
            }
        });
    }
    closePromptTarget_ = id;
    closePrompt_->setWindowTitle(QStringLiteral("Unsaved changes"));
    closePrompt_->setText(QStringLiteral("%1 has unsaved changes.").arg(state->displayName));
    closePrompt_->setInformativeText(
        QStringLiteral("Save writes it inside the workspace; Discard closes without saving."));
    closePrompt_->open();
}

void MainWindow::closeBuffer(BufferId id, bool discardChanges) {
    const auto* state = buffers_.find(id);
    if (state == nullptr) {
        statusArea_->setText(QStringLiteral("That buffer is not open"));
        return;
    }
    const auto name = state->displayName;
    const auto path = state->path;
    const auto closed = discardChanges ? buffers_.closeDiscardingChanges(id) : buffers_.close(id);
    if (!closed) {
        if (closed.error().code == BufferErrorCode::Modified) {
            statusArea_->setText(
                QStringLiteral(
                    "No write since last change for %1; :w saves it, Space+b+D discards it")
                    .arg(name));
        } else {
            statusArea_->setText(QString::fromStdString(closed.error().message));
        }
        return;
    }

    if (const auto editor = editors_.find(id); editor != editors_.end()) {
        if (auto* widget = editor->second->widget(); widget != nullptr) {
            editorStack_->removeWidget(widget);
        }
        editors_.erase(editor);
    }
    tracked_.erase(id);
    viewModes_.erase(id);
    if (path.has_value()) {
        watcher_->unwatch(*path);
    }

    // The registry keeps one unnamed buffer alive when the last one closes,
    // exactly as Vim does; it needs an editor like any other.
    for (const auto& buffer : buffers_.buffers()) {
        if (!editors_.contains(buffer.id)) {
            createEditorFor(buffer.id);
        }
    }
    if (const auto active = buffers_.activeId(); active.has_value()) {
        showBuffer(*active);
    }
    emit bufferResolved(id);
    statusArea_->setText(discardChanges ? QStringLiteral("Closed %1, discarding changes").arg(name)
                                        : QStringLiteral("Closed %1").arg(name));
}

bool MainWindow::interceptEditorFileCommand(QObject* watched, const QKeyEvent& event) {
    if (event.key() != Qt::Key_Return && event.key() != Qt::Key_Enter) {
        return false;
    }
    if (editorStack_ == nullptr) {
        return false;
    }
    // A bare `:w` leaves the command bar's completion popup open, and the
    // Return then lands on that popup rather than on the line edit. The popup
    // is a separate top-level window whose focus proxy is the line edit; on
    // older toolkits without the proxy, the only visible line edit inside the
    // editor is the command bar.
    auto* commandLine = qobject_cast<QLineEdit*>(watched);
    QWidget* popup = nullptr;
    if (commandLine == nullptr) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget == nullptr || widget->windowType() != Qt::Popup) {
            return false;
        }
        popup = widget;
        commandLine = qobject_cast<QLineEdit*>(widget->focusProxy());
        if (commandLine == nullptr) {
            for (auto* candidate : editorStack_->findChildren<QLineEdit*>()) {
                if (candidate->isVisible()) {
                    commandLine = candidate;
                    break;
                }
            }
        }
    }
    if (commandLine == nullptr || !editorStack_->isAncestorOf(commandLine)) {
        return false;
    }

    const auto typed = commandLine->text().trimmed();
    const auto verb = typed.section(QLatin1Char(' '), 0, 0);
    const auto argument = typed.section(QLatin1Char(' '), 1).trimmed();

    // KTextEditor's Vi mode implements these itself and calls its own writer,
    // which does not know about the workspace root and does not write
    // atomically. Every one of them has to be taken away from it.
    static const QStringList kWriteVerbs = {
        QStringLiteral("w"),      QStringLiteral("w!"),   QStringLiteral("write"),
        QStringLiteral("write!"), QStringLiteral("wq"),   QStringLiteral("wq!"),
        QStringLiteral("wa"),     QStringLiteral("wall"), QStringLiteral("wqa"),
        QStringLiteral("wqa!"),   QStringLiteral("x"),    QStringLiteral("x!"),
        QStringLiteral("xa"),     QStringLiteral("xall"), QStringLiteral("exit"),
        QStringLiteral("saveas"), QStringLiteral("sav"),  QStringLiteral("update"),
        QStringLiteral("up")};
    // Quitting too: Vi mode's own `:q` asks a host application this app never
    // registers, so it would do nothing at all. `:q` is the way out of
    // OmaNotes (Matt's call, 2026-09-10), with the save prompt when work is
    // unsaved; `:q!` discards. Buffers here are Vim buffers, not windows, so
    // `:q` and `:qa` mean the same thing.
    static const QStringList kQuitVerbs = {QStringLiteral("q"),    QStringLiteral("q!"),
                                           QStringLiteral("quit"), QStringLiteral("quit!"),
                                           QStringLiteral("qa"),   QStringLiteral("qa!"),
                                           QStringLiteral("qall"), QStringLiteral("qall!")};
    // Reloading goes the same way: the editor's own `:e` would replace the
    // buffer without consulting the revision the application is tracking.
    static const QStringList kEditVerbs = {QStringLiteral("e"), QStringLiteral("e!"),
                                           QStringLiteral("edit"), QStringLiteral("edit!")};
    const auto isWrite = kWriteVerbs.contains(verb);
    const auto isEdit = kEditVerbs.contains(verb);
    const auto isQuit = kQuitVerbs.contains(verb);
    if (!isWrite && !isEdit && !isQuit) {
        return false;
    }

    // Dismiss the editor's command bar the way Escape would, then answer in the
    // status area.
    if (popup != nullptr) {
        popup->hide();
    }
    QKeyEvent dismiss(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(commandLine, &dismiss);

    const auto force = verb.endsWith(QLatin1Char('!'));
    const auto bare = force ? verb.chopped(1) : verb;
    QString failure;
    if (isEdit) {
        if (argument.isEmpty()) {
            failure = reloadActiveBuffer(force);
        } else {
            loadMarkdownFile(std::filesystem::path(argument.toStdString()));
        }
    } else if (bare == QStringLiteral("w") || bare == QStringLiteral("write")) {
        failure = argument.isEmpty() ? saveActiveBufferOrReport(force)
                                     : saveTo(std::filesystem::path(argument.toStdString()), force);
    } else if (isQuit) {
        quitApplication(force);
    } else if (bare == QStringLiteral("wq") || bare == QStringLiteral("x") ||
               bare == QStringLiteral("exit")) {
        // Vim's :wq writes this buffer and quits; other unsaved buffers still
        // get their say through the prompt. :x is the same here: the only
        // difference in Vim is skipping an unneeded write, which the saver
        // already does.
        failure = argument.isEmpty() ? saveActiveBufferOrReport(force)
                                     : saveTo(std::filesystem::path(argument.toStdString()), force);
        if (failure.isEmpty()) {
            quitApplication(false);
        }
    } else if (bare == QStringLiteral("wqa") || bare == QStringLiteral("xa") ||
               bare == QStringLiteral("xall")) {
        failure = saveAllModified(force);
        if (failure.isEmpty()) {
            quitApplication(false);
        }
    } else {
        failure = QStringLiteral("%1 is not available yet; use :w [path]").arg(verb);
    }
    if (!failure.isEmpty()) {
        statusArea_->setText(failure);
    }
    return true;
}

QString MainWindow::saveAllModified(bool force) {
    for (const auto& buffer : buffers_.buffers()) {
        if (!buffer.modified) {
            continue;
        }
        if (buffers_.activate(buffer.id)) {
            showBuffer(buffer.id);
        }
        if (!buffer.path.has_value()) {
            return QStringLiteral("%1 has no file name; :w path.md names it")
                .arg(buffer.displayName);
        }
        if (const auto failure = saveTo({}, force); !failure.isEmpty()) {
            return failure;
        }
    }
    return {};
}

void MainWindow::discardAllAndClose() {
    std::vector<BufferId> modified;
    for (const auto& buffer : buffers_.buffers()) {
        if (buffer.modified) {
            modified.push_back(buffer.id);
        }
    }
    // Closing with discard drops each buffer's recovery record, so the
    // decision sticks (docs/session-format.md, "Buffer discarded").
    for (const auto id : modified) {
        closeBuffer(id, true);
    }
    close();
}

void MainWindow::quitApplication(bool discardChanges) {
    if (discardChanges) {
        discardAllAndClose();
        return;
    }
    QStringList unsaved;
    for (const auto& buffer : buffers_.buffers()) {
        if (buffer.modified) {
            unsaved.append(buffer.displayName);
        }
    }
    if (unsaved.isEmpty()) {
        close();
        return;
    }
    if (quitPrompt_ == nullptr) {
        quitPrompt_ =
            new QMessageBox(QMessageBox::Question, QString{}, QString{},
                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        quitPrompt_->setObjectName(QStringLiteral("quitPrompt"));
        quitPrompt_->setDefaultButton(QMessageBox::Save);
        for (auto* button : quitPrompt_->buttons()) {
            button->setIcon(QIcon());
        }
        connect(quitPrompt_, &QMessageBox::finished, this, [this](int result) {
            if (result == QMessageBox::Discard) {
                discardAllAndClose();
                return;
            }
            // Staying means back to the note, not to a dismissed dialog.
            const auto backToEditor = [this] {
                activateWindow();
                if (auto* editor = activeEditor();
                    editor != nullptr && editor->widget() != nullptr) {
                    editor->widget()->setFocus(Qt::OtherFocusReason);
                }
            };
            if (result != QMessageBox::Save) {
                backToEditor();
                return;
            }
            if (const auto failure = saveAllModified(false); !failure.isEmpty()) {
                statusArea_->setText(failure);
                backToEditor();
                return;
            }
            close();
        });
    }
    quitPrompt_->setWindowTitle(QStringLiteral("Unsaved changes"));
    quitPrompt_->setText(unsaved.size() == 1
                             ? QStringLiteral("%1 has unsaved changes.").arg(unsaved.first())
                             : QStringLiteral("%1 buffers have unsaved changes: %2")
                                   .arg(unsaved.size())
                                   .arg(unsaved.join(QStringLiteral(", "))));
    quitPrompt_->setInformativeText(
        QStringLiteral("Save writes them inside the workspace and quits; Discard quits without "
                       "saving; Cancel stays."));
    quitPrompt_->open();
}

QString MainWindow::saveActiveBufferOrReport(bool force) {
    const auto active = buffers_.activeId();
    const auto* state = active.has_value() ? buffers_.find(*active) : nullptr;
    if (state == nullptr || !state->path.has_value()) {
        return QStringLiteral("No file name; try :w path.md");
    }
    return saveTo({}, force);
}

QString MainWindow::reloadActiveBuffer(bool discardEdits) {
    auto* editor = activeEditor();
    const auto active = buffers_.activeId();
    if (editor == nullptr || !active.has_value()) {
        return QStringLiteral("There is nothing to reload");
    }
    const auto activeId = *active;
    const auto* state = buffers_.find(activeId);
    if (state == nullptr || !state->path.has_value()) {
        return QStringLiteral("No file name; try :e path.md");
    }
    if (editor->isModified() && !discardEdits) {
        return QStringLiteral("No write since last change") + QLatin1String(kAddBangToOverride);
    }

    const auto& path = *state->path;
    const auto bytes = readNoteBytes(path);
    if (!bytes) {
        return bytes.error();
    }
    const auto text = decodeNote(*bytes, path);
    if (!text) {
        return text.error();
    }

    editor->loadText(*text);
    trackFile(activeId, path, QByteArrayView(*bytes));
    buffers_.setModified(activeId, false);
    syncBufferStrip();
    emit bufferResolved(activeId);
    statusArea_->setText(QStringLiteral("Reloaded %1 from disk").arg(displayName(path)));
    return {};
}

void MainWindow::trackFile(BufferId id, const std::filesystem::path& path,
                           QByteArrayView contents) {
    tracked_[id] = TrackedFile{SavedRevision::of(contents), DiskNote::InSync};
    watcher_->watch(path);
}

void MainWindow::handleExternalChange(const std::filesystem::path& path) {
    const auto id = buffers_.findByPath(path);
    if (!id.has_value()) {
        return;
    }
    const auto tracked = tracked_.find(*id);
    const auto editor = editors_.find(*id);
    if (tracked == tracked_.end() || editor == editors_.end()) {
        return;
    }

    const auto name = displayName(path);
    const auto current = DiskRevision::read(path);
    switch (classifyExternalChange(tracked->second.known, current, editor->second->isModified())) {
    case ExternalChangeAction::Unchanged:
        // Our own save, or a rewrite of identical bytes: nothing to report,
        // and the "Wrote" confirmation must be left standing. A file that was
        // deleted and then restored is back in sync, though.
        if (tracked->second.note != DiskNote::InSync) {
            tracked->second.note = DiskNote::InSync;
            refreshEditorStatus();
        }
        return;
    case ExternalChangeAction::ReloadClean: {
        const auto bytes = readNoteBytes(path);
        const auto text = bytes ? decodeNote(*bytes, path) : std::unexpected(bytes.error());
        if (!text) {
            tracked->second.note = DiskNote::ChangedOnDisk;
            statusArea_->setText(text.error());
            return;
        }
        editor->second->loadText(*text);
        tracked->second = TrackedFile{SavedRevision::of(QByteArrayView(*bytes)), DiskNote::InSync};
        buffers_.setModified(*id, false);
        syncBufferStrip();
        // A reader must see the agent's edit too, not a stale projection.
        if (buffers_.activeId() == *id && viewModeFor(*id) == ViewMode::Reading) {
            renderReadingView(*id);
        }
        statusArea_->setText(QStringLiteral("Reloaded %1 from disk").arg(name));
        return;
    }
    case ExternalChangeAction::PromptConflict:
        tracked->second.note = DiskNote::ChangedOnDisk;
        refreshEditorStatus();
        statusArea_->setText(
            QStringLiteral("%1 changed on disk; your edits are kept. :w! overwrites, :e! reloads")
                .arg(name));
        return;
    case ExternalChangeAction::FileRemoved:
        tracked->second.note = DiskNote::Removed;
        refreshEditorStatus();
        statusArea_->setText(
            QStringLiteral("%1 was deleted on disk; :w writes it again").arg(name));
        return;
    }
}

void MainWindow::openScratchBuffer() {
    const auto id = buffers_.createScratch();
    createEditorFor(id);
    showBuffer(id);
}

EditorAdapter* MainWindow::activeEditor() const {
    const auto active = buffers_.activeId();
    if (!active.has_value()) {
        return nullptr;
    }
    const auto editor = editors_.find(*active);
    return editor == editors_.end() ? nullptr : editor->second.get();
}

EditorAdapter& MainWindow::createEditorFor(BufferId id) {
    auto adapter = std::make_unique<KTextEditorAdapter>(editorStack_);
    auto& editor = *adapter;
    editors_.emplace(id, std::move(adapter));
    editorStack_->addWidget(editor.widget());
    if (theme_ != nullptr) {
        static_cast<KTextEditorAdapter&>(editor).applyTheme(theme_->currentPalette());
    }

    connect(&editor, &EditorAdapter::modeChanged, this, [this] { refreshEditorStatus(); });
    connect(&editor, &EditorAdapter::modifiedChanged, this, [this, id](bool modified) {
        buffers_.setModified(id, modified);
        syncBufferStrip();
        refreshEditorStatus();
        emit sessionStateChanged();
    });
    return editor;
}

void MainWindow::showBuffer(BufferId id) {
    const auto editor = editors_.find(id);
    if (editor == editors_.end()) {
        return;
    }

    prefixRouter_->cancelPending();
    if (viewModeFor(id) == ViewMode::Reading) {
        renderReadingView(id);
        editorStack_->setCurrentWidget(readingView_);
        syncBufferStrip();
        refreshEditorStatus();
        readingView_->setFocus(Qt::OtherFocusReason);
        emit sessionStateChanged();
        return;
    }
    editorStack_->setCurrentWidget(editor->second->widget());
    syncBufferStrip();
    refreshEditorStatus();
    if (editor->second->widget() != nullptr) {
        editor->second->widget()->setFocus(Qt::OtherFocusReason);
    }
    emit sessionStateChanged();
}

void MainWindow::toggleReadingView() {
    const auto active = buffers_.activeId();
    if (!active.has_value()) {
        return;
    }
    auto* editor = activeEditor();
    const auto lastLine = editor != nullptr ? std::max(editor->lineCount() - 1, 1) : 1;
    auto* bar = readingView_->verticalScrollBar();
    // Obsidian's courtesy: reading opens near where the cursor was, and
    // writing comes back near where the reader stopped. Source lines and
    // rendered blocks do not map one to one, so this is proportional, not exact.
    if (viewModeFor(*active) == ViewMode::Writing) {
        viewModes_[*active] = ViewMode::Reading;
        showBuffer(*active);
        if (editor != nullptr) {
            (void)readingView_->document()->documentLayout()->documentSize();
            const auto fraction = static_cast<double>(editor->cursorPosition().line) / lastLine;
            bar->setValue(static_cast<int>(fraction * bar->maximum()));
        }
        return;
    }
    const auto fraction =
        bar->maximum() > 0 ? static_cast<double>(bar->value()) / bar->maximum() : 0.0;
    viewModes_[*active] = ViewMode::Writing;
    showBuffer(*active);
    if (editor != nullptr && fraction > 0.0) {
        editor->scrollToLine(static_cast<int>(fraction * lastLine));
    }
}

ViewMode MainWindow::viewModeFor(BufferId id) const {
    const auto found = viewModes_.find(id);
    return found == viewModes_.end() ? ViewMode::Writing : found->second;
}

void MainWindow::renderReadingView(BufferId id) {
    const auto editor = editors_.find(id);
    if (editor == editors_.end()) {
        return;
    }
    const auto* state = buffers_.find(id);
    ResourcePolicy policy;
    policy.root = launchRequest_.root;
    policy.noteDirectory = state != nullptr && state->path.has_value() ? state->path->parent_path()
                                                                       : launchRequest_.root;
    readingView_->render(editor->second->text(), policy);
}

void MainWindow::syncBufferStrip() {
    bufferStrip_->syncWith(buffers_.buffers(), buffers_.activeId());
}

void MainWindow::loadMarkdownFile(const std::filesystem::path& path) {
    auto workspace = WorkspaceRoot::resolve(launchRequest_.root);
    if (!workspace) {
        statusArea_->setText(QString::fromStdString(workspace.error().message));
        return;
    }
    auto resolved = workspace->resolveFile(path);
    if (!resolved) {
        statusArea_->setText(QString::fromStdString(resolved.error().message));
        return;
    }
    if (!isMarkdown(*resolved)) {
        statusArea_->setText(QStringLiteral("Only Markdown (.md) files can be opened"));
        return;
    }

    // An already-open buffer is activated, never reloaded: re-opening a file
    // must not discard edits the user has not saved. The lookup deliberately
    // does not create a buffer, so a failed read below leaves no empty tab.
    if (const auto existing = buffers_.findByPath(*resolved); existing.has_value()) {
        buffers_.activate(*existing);
        showBuffer(*existing);
        return;
    }

    const auto bytes = readNoteBytes(*resolved);
    if (!bytes) {
        statusArea_->setText(bytes.error());
        return;
    }
    const auto text = decodeNote(*bytes, *resolved);
    if (!text) {
        statusArea_->setText(text.error());
        return;
    }

    const auto opened = buffers_.open(*resolved);
    if (!opened.has_value()) {
        statusArea_->setText(QString::fromStdString(opened.error().message));
        return;
    }

    createEditorFor(*opened).loadText(*text);
    trackFile(*opened, *resolved, QByteArrayView(*bytes));
    showBuffer(*opened);
}

void MainWindow::pasteFromClipboard() {
    // The reading view is a projection, never an editor: pasting here would
    // silently mutate the hidden source buffer. Refuse and say why.
    if (editorStack_->currentWidget() == readingView_) {
        statusArea_->setText(QStringLiteral("Reading view is read-only — Space m to write"));
        return;
    }
    auto* editor = activeEditor();
    if (editor == nullptr || editor->widget() == nullptr) {
        return;
    }
    if (auto* view = qobject_cast<KTextEditor::View*>(editor->widget())) {
        if (auto* paste = view->actionCollection()->action(QStringLiteral("edit_paste"));
            paste != nullptr) {
            paste->trigger();
            return;
        }
    }
    statusArea_->setText(QStringLiteral("Nothing to paste into"));
}

void MainWindow::copySelectionToClipboard() {
    if (editorStack_->currentWidget() == readingView_) {
        if (readingView_->textCursor().hasSelection()) {
            readingView_->copy();
            return;
        }
        statusArea_->setText(QStringLiteral("Nothing is selected to copy"));
        return;
    }
    auto* editor = activeEditor();
    if (editor == nullptr || editor->widget() == nullptr) {
        return;
    }
    if (auto* view = qobject_cast<KTextEditor::View*>(editor->widget());
        view != nullptr && view->selection()) {
        if (auto* copy = view->actionCollection()->action(QStringLiteral("edit_copy"));
            copy != nullptr) {
            copy->trigger();
            return;
        }
    }
    statusArea_->setText(QStringLiteral("Nothing is selected to copy"));
}

void MainWindow::forwardControlKeyToVi(Qt::Key key) {
    auto* editor = activeEditor();
    if (editor == nullptr || editor->widget() == nullptr) {
        return;
    }
    auto* target = QApplication::focusWidget();
    if (target == nullptr || !editorStack_->isAncestorOf(target)) {
        target = editor->widget();
    }
    forwardingKeyToVi_ = true;
    QKeyEvent press(QEvent::KeyPress, key, Qt::ControlModifier);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::ControlModifier);
    QApplication::sendEvent(target, &release);
    forwardingKeyToVi_ = false;
}

void MainWindow::enterVisualBlock() { forwardControlKeyToVi(Qt::Key_V); }

void MainWindow::scrollHalfPage(int direction) {
    if (editorStack_->currentWidget() == readingView_) {
        auto* bar = readingView_->verticalScrollBar();
        bar->setValue(bar->value() + direction * readingView_->viewport()->height() / 2);
        return;
    }
    forwardControlKeyToVi(direction > 0 ? Qt::Key_D : Qt::Key_U);
}

void MainWindow::saveActiveBuffer() {
    const auto active = buffers_.activeId();
    const auto* state = active.has_value() ? buffers_.find(*active) : nullptr;
    if (state == nullptr) {
        return;
    }
    if (!state->path.has_value()) {
        beginNaming();
        return;
    }

    if (const auto failure = saveTo({}); !failure.isEmpty()) {
        statusArea_->setText(failure);
    }
}

void MainWindow::beginNaming() {
    namePrompt_->clear();
    namePrompt_->show();
    namePrompt_->setFocus(Qt::OtherFocusReason);
    statusArea_->setText(QStringLiteral("Save as (Esc to cancel)"));
}

void MainWindow::cancelNaming() {
    closeAfterNaming_.reset();
    namePrompt_->clear();
    namePrompt_->hide();
    if (auto* editor = activeEditor(); editor != nullptr && editor->widget() != nullptr) {
        editor->widget()->setFocus(Qt::OtherFocusReason);
    }
    refreshEditorStatus();
}

void MainWindow::commitNaming() {
    const auto typed = namePrompt_->text().trimmed();
    if (typed.isEmpty()) {
        cancelNaming();
        return;
    }

    const auto failure = saveTo(std::filesystem::path(typed.toStdString()));
    if (!failure.isEmpty()) {
        statusArea_->setText(failure);
        return;
    }

    namePrompt_->clear();
    namePrompt_->hide();
    if (auto* editor = activeEditor(); editor != nullptr && editor->widget() != nullptr) {
        editor->widget()->setFocus(Qt::OtherFocusReason);
    }

    // A save begun from the close prompt finishes the close it interrupted.
    if (closeAfterNaming_.has_value()) {
        auto context = currentContext();
        context.targetBuffer = *closeAfterNaming_;
        closeAfterNaming_.reset();
        runCommand(QStringLiteral("buffer.close"), std::move(context));
    }
}

QString MainWindow::saveTo(const std::filesystem::path& requested, bool force) {
    auto* editor = activeEditor();
    const auto active = buffers_.activeId();
    if (editor == nullptr || !active.has_value()) {
        return QStringLiteral("There is nothing to save");
    }

    const auto activeId = *active;
    const auto* state = buffers_.find(activeId);
    if (state == nullptr) {
        return QStringLiteral("There is nothing to save");
    }

    const auto target =
        requested.empty() ? state->path.value_or(std::filesystem::path{}) : requested;
    if (target.empty()) {
        return QStringLiteral("No file name; try :w path.md");
    }

    auto workspace = WorkspaceRoot::resolve(launchRequest_.root);
    if (!workspace) {
        return QString::fromStdString(workspace.error().message);
    }

    const DocumentStore store(*workspace);
    const auto destination = store.resolveTarget(target);
    if (!destination) {
        return QString::fromStdString(destination.error().message);
    }

    // The guarantee of this whole task lives here, not in the watcher: the
    // watcher is a courtesy that may lag, but a write compares against the
    // disk at the moment it happens.
    if (!force) {
        const auto current = DiskRevision::read(*destination);
        const auto name = displayName(*destination);
        const auto tracked = tracked_.find(activeId);
        // Buffers remember canonical paths, so a target typed through a
        // symlink still has to count as the buffer's own file.
        std::error_code error;
        auto canonicalDestination = std::filesystem::weakly_canonical(*destination, error);
        if (error) {
            canonicalDestination = *destination;
        }
        const auto ownFile = tracked != tracked_.end() && state->path == canonicalDestination;
        if (!ownFile) {
            if (current.state != DiskRevision::State::Missing) {
                return QStringLiteral("File exists: %1").arg(name) +
                       QLatin1String(kAddBangToOverride);
            }
        } else {
            switch (classifyExternalChange(tracked->second.known, current, editor->isModified())) {
            case ExternalChangeAction::Unchanged:
            case ExternalChangeAction::FileRemoved:
                break;
            case ExternalChangeAction::ReloadClean:
                return QStringLiteral("%1 changed on disk since it was read; :e reloads it, :w! "
                                      "overwrites it")
                    .arg(name);
            case ExternalChangeAction::PromptConflict:
                return QStringLiteral("%1 changed on disk since it was read; :w! overwrites it, "
                                      ":e! discards your edits")
                    .arg(name);
            }
        }
    }

    const auto text = editor->text();
    const auto written = store.save(target, text);
    if (!written) {
        return QString::fromStdString(written.error().message);
    }
    trackFile(activeId, *written, QByteArrayView(text.toUtf8()));

    if (state->path != *written) {
        if (const auto assigned = buffers_.assignPath(activeId, *written); !assigned) {
            return QString::fromStdString(assigned.error().message);
        }
    }

    // Clear the modified flag first: it emits, and the emission refreshes the
    // status line that the confirmation below is written into.
    editor->markSaved();
    buffers_.setModified(activeId, false);
    syncBufferStrip();
    // A note the user just wrote should be visible where they expect to find
    // it, without relaunching.
    sidebar_->noteFileCreated(*written);
    emit bufferResolved(activeId);
    statusArea_->setText(QStringLiteral("Wrote %1").arg(displayName(*written)));
    return {};
}

void MainWindow::refreshEditorStatus() {
    const auto* editor = activeEditor();
    if (editor == nullptr) {
        return;
    }

    const auto active = buffers_.activeId();
    const auto reading = active.has_value() && viewModeFor(*active) == ViewMode::Reading;
    const auto modeName = reading ? QStringLiteral("Reading") : humanModeName(editor->modeName());
    auto diskMarker = QString{};
    if (const auto tracked = active.has_value() ? tracked_.find(*active) : tracked_.end();
        tracked != tracked_.end()) {
        switch (tracked->second.note) {
        case DiskNote::InSync:
            break;
        case DiskNote::ChangedOnDisk:
            diskMarker = QStringLiteral(" [changed on disk]");
            break;
        case DiskNote::Removed:
            diskMarker = QStringLiteral(" [deleted on disk]");
            break;
        }
    }
    statusArea_->setText(QStringLiteral("%1%2").arg(modeName, diskMarker));
}

void MainWindow::applyTheme(const ThemePalette& palette) {
    const auto name = [](const QColor& colour) { return colour.name(QColor::HexRgb); };
    // Where you are is told by what is there, not by a mark on the frame
    // (Matt's call, 2026-09-10): the sidebar's selected row is painted only
    // while the sidebar has focus, and the editor has its cursor. The
    // paneActive property is the window's own per-pane focus flag; Qt's
    // :active pseudo-state follows the whole window, not the pane.
    setStyleSheet(QStringLiteral(R"(
* { font-family: monospace; font-size: %9pt; }
QScrollBar:vertical { width: 0px; }
QScrollBar:horizontal { height: 0px; }
QMainWindow#mainWindow { background-color: %1; }
QSplitter#workspaceSplitter::handle { background-color: %2; }
QFrame#sidebar { background-color: %3; border: none; }
QFrame#sidebar QTreeView { background-color: %3; color: %5; border: none; outline: none; }
QFrame#sidebar QTreeView::item:selected { background-color: %3; color: %5; }
QFrame#sidebar[paneActive="true"] QTreeView::item:selected { background-color: %6; color: %8; }
QLabel#sidebarHeading { color: %7; }
QTextBrowser#readingView { background-color: %1; border: none; font-size: %10pt; }
QWidget#writingArea { background-color: %1; }
QWidget#statusRow { background-color: %1; }
QLabel#statusArea { background-color: %1; color: %5; }
QLineEdit#namePrompt { background-color: %3; color: %5; selection-background-color: %6; selection-color: %8; }
QWidget#bufferRow { background-color: %1; }
QTabBar#bufferStrip { background-color: %1; }
QTabBar#bufferStrip::tab { background-color: %1; color: %7; padding: 5px 12px; border: none; }
QTabBar#bufferStrip::tab:selected { background-color: %1; color: %5; }
QToolButton#sidebarToggleButton { background-color: %1; color: %7; border: none; padding: 2px 8px; }
QToolButton#sidebarToggleButton:hover { color: %4; }
QToolButton#newBufferButton { background-color: %1; color: %7; border: none; padding: 2px 8px; }
QToolButton#tabCloseButton { background: transparent; color: %7; border: none; padding: 0px 2px; }
QToolButton#tabCloseButton:hover { color: %4; }
QToolButton#helpButton { background: transparent; color: %7; border: none; padding: 0px 6px; }
QToolButton#helpButton:hover { color: %4; }
QDialog#helpOverlay, QDialog#searchPalette, QMessageBox { background-color: %1; color: %5; }
QDialog#helpOverlay QLabel, QDialog#searchPalette QLabel, QMessageBox QLabel { color: %5; }
QTreeWidget#helpCommands, QListWidget#searchResults, QPlainTextEdit#searchPreview { background-color: %3; color: %5; border: none; }
QTreeWidget#helpCommands::item:selected, QListWidget#searchResults::item:selected { background-color: %6; color: %8; }
QTreeWidget#helpCommands QHeaderView { background-color: %3; border: none; }
QTreeWidget#helpCommands QHeaderView::section { background-color: %3; color: %5; border: none; padding: 4px 6px; }
QLineEdit#searchQuery { background-color: %3; color: %5; border: 1px solid %2; padding: 4px 6px; selection-background-color: %6; selection-color: %8; }
QDialog QPushButton { background-color: %3; color: %5; border: 1px solid %2; padding: 4px 14px; }
QDialog QPushButton:default { border: 1px solid %4; }
QDialog QPushButton:hover { background-color: %2; }
QLineEdit#commandtext { background-color: %1; color: %5; border: none; selection-background-color: %6; selection-color: %8; }
QLabel#bartypeindicator, QLabel#commandresponsemessage, QLabel#waitingforregisterindicator { background-color: %1; color: %7; }
)")
                      .arg(name(palette.background), name(palette.border), name(palette.surface),
                           name(palette.accent), name(palette.text), name(palette.selection),
                           name(palette.mutedText), name(palette.selectedText),
                           QString::number(palette.baseFontPointSize),
                           QString::number(palette.baseFontPointSize + 1.0)));
    writingArea_->setAttribute(Qt::WA_StyledBackground, true);
    // Vi's `:` command line is a child of the editor and takes the rules
    // above, but its completion drop-down is a QCompleter popup with no
    // parent at all, so no window stylesheet can reach it. Only an
    // application-wide rule does; it is kept to that one widget's class,
    // and the window's own id rules still win where they apply.
    qApp->setStyleSheet(
        QStringLiteral(
            "QListView { background-color: %1; color: %2; border: 1px solid %3; outline: none; "
            "font-family: monospace; font-size: %6pt; }\n"
            "QListView::item:selected { background-color: %4; color: %5; }")
            .arg(name(palette.surface), name(palette.text), name(palette.border),
                 name(palette.selection), name(palette.selectedText),
                 QString::number(palette.baseFontPointSize)));
    // No scrollbars anywhere (Matt's call, 2026-09-10): the wheel, the keys
    // and the trackpad still scroll; the bars are given zero size rather
    // than a policy because KTextEditor owns its own and offers no switch.

    // Grounds and the tree's selected row live in the stylesheet above: with
    // a stylesheet active, Qt ignores QPalette for widget backgrounds and
    // item selection (Matt's gate found both the sidebar row and the reading
    // pane stuck grey/blue), and the :active/:!active pseudo-states carry
    // the inactive dim instead. The palette below still matters — the
    // reading view's document rendering draws its text, links, and text
    // selection from it, not from the stylesheet.
    auto readingPalette = readingView_->palette();
    readingPalette.setColor(QPalette::Text, palette.text);
    readingPalette.setColor(QPalette::Link, palette.link);
    readingPalette.setColor(QPalette::Highlight, palette.selection);
    readingPalette.setColor(QPalette::HighlightedText, palette.selectedText);
    readingView_->setPalette(readingPalette);
    // Pure Omarchy (Matt's call, 2026-09-08): the system monospace
    // everywhere, at the shell's base size — via the stylesheet's `*` rule,
    // because QApplication::setFont is documented not to mix with style
    // sheets and the chrome duly ignored it. "monospace" resolves through
    // fontconfig, exactly the source of truth omarchy-font-current reads;
    // the GTK platform theme's sans (which Omarchy never chose) is retired.
    // The reading view still gets an explicit font: its QTextDocument reads
    // the widget font when rendering, not the stylesheet.
    QFont readingFont(QStringLiteral("monospace"));
    readingFont.setStyleHint(QFont::Monospace);
    // 6.1's restrained typography holds: the reading face sits one point up.
    readingFont.setPointSizeF(palette.baseFontPointSize + 1.0);
    readingView_->setFont(readingFont);

    for (const auto& [bufferId, editor] : editors_) {
        if (auto* hosted = qobject_cast<KTextEditorAdapter*>(editor.get()); hosted != nullptr) {
            hosted->applyTheme(palette);
        }
    }

    // A visible projection re-renders so its document picks up the new link
    // and text colours; hidden ones re-render on their next Space m anyway.
    if (const auto active = buffers_.activeId();
        active.has_value() && editorStack_->currentWidget() == readingView_) {
        renderReadingView(*active);
    }
}

void MainWindow::markActivePane() {
    auto* focus = QApplication::focusWidget();
    const auto mark = [focus](QWidget* pane) {
        const auto active = focus != nullptr && pane->isAncestorOf(focus);
        if (pane->property("paneActive").toBool() == active) {
            return;
        }
        pane->setProperty("paneActive", active);
        pane->style()->unpolish(pane);
        pane->style()->polish(pane);
        // Rules on descendants keyed to the pane's property are re-evaluated
        // only when those descendants are polished too.
        for (auto* view : pane->findChildren<QAbstractItemView*>()) {
            view->style()->unpolish(view);
            view->style()->polish(view);
            view->viewport()->update();
        }
    };
    mark(sidebar_);
    mark(writingArea_);
}

} // namespace omanotes
