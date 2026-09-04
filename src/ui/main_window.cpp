#include "ui/main_window.hpp"

#include "app/prefix_router.hpp"
#include "editor/editor_adapter.hpp"
#include "editor/ktext_editor_adapter.hpp"
#include "editor/save_command.hpp"
#include "persistence/document_store.hpp"
#include "ui/buffer_strip.hpp"
#include "ui/sidebar.hpp"
#include "workspace/file_watcher.hpp"
#include "workspace/workspace_root.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QVBoxLayout>
#include <QWidget>

#include <expected>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

constexpr auto kWindowTitle = "Omanotes";

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
                          QLabel*& status, QLineEdit*& namePrompt) {
    auto* writingArea = new QWidget(parent);
    writingArea->setObjectName(QStringLiteral("writingArea"));

    auto* layout = new QVBoxLayout(writingArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buffers = new BufferStrip(writingArea);

    editors = new QStackedWidget(writingArea);
    editors->setObjectName(QStringLiteral("editorStack"));
    editors->setAccessibleName(QStringLiteral("Open documents"));

    status = new QLabel(writingArea);
    status->setObjectName(QStringLiteral("statusArea"));
    status->setAccessibleName(QStringLiteral("Editor status"));
    status->setContentsMargins(10, 6, 10, 6);

    namePrompt = new QLineEdit(writingArea);
    namePrompt->setObjectName(QStringLiteral("namePrompt"));
    namePrompt->setAccessibleName(QStringLiteral("Save as path"));
    namePrompt->setPlaceholderText(QStringLiteral("path/inside/workspace.md"));
    namePrompt->setFrame(false);
    namePrompt->setContentsMargins(10, 6, 10, 6);
    namePrompt->hide();

    layout->addWidget(buffers);
    layout->addWidget(editors, 1);
    layout->addWidget(namePrompt);
    layout->addWidget(status);
    return writingArea;
}

} // namespace

MainWindow::MainWindow(LaunchRequest launchRequest, QWidget* parent)
    : QMainWindow(parent), launchRequest_(std::move(launchRequest)) {
    setObjectName(QStringLiteral("mainWindow"));
    const auto rootName =
        QFile::decodeName(QByteArray::fromStdString(launchRequest_.root.native()));
    setWindowTitle(QStringLiteral("%1 — %2").arg(QString::fromLatin1(kWindowTitle), rootName));
    setMinimumSize(720, 480);
    resize(1100, 720);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("workspaceSplitter"));
    splitter->setAccessibleName(QStringLiteral("Workspace and editor panes"));
    splitter->setChildrenCollapsible(false);
    sidebar_ = new Sidebar(launchRequest_.root, splitter);
    splitter->addWidget(sidebar_);
    prefixRouter_ = std::make_unique<PrefixRouter>(LeaderKey::Space);
    watcher_ = std::make_unique<FileWatcher>();
    connect(watcher_.get(), &FileWatcher::fileChanged, this,
            [this](const std::filesystem::path& path) { handleExternalChange(path); });
    splitter->addWidget(
        buildWritingArea(splitter, editorStack_, bufferStrip_, statusArea_, namePrompt_));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 860});

    setCentralWidget(splitter);

    connect(prefixRouter_.get(), &PrefixRouter::feedbackChanged, statusArea_,
            [this](const QString& message) { statusArea_->setText(message); });
    connect(bufferStrip_, &BufferStrip::bufferSelected, this, [this](BufferId id) {
        if (buffers_.activate(id)) {
            showBuffer(id);
        }
    });
    connect(sidebar_, &Sidebar::fileActivated, this,
            [this](const std::filesystem::path& path) { loadMarkdownFile(path); });
    connect(sidebar_, &Sidebar::editorFocusRequested, this, [this] {
        if (auto* editor = activeEditor(); editor != nullptr && editor->widget() != nullptr) {
            editor->widget()->setFocus(Qt::ShortcutFocusReason);
        }
    });
    connect(namePrompt_, &QLineEdit::returnPressed, this, [this] { commitNaming(); });
    auto* saveShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { saveActiveBuffer(); });
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

    auto* focusSidebar = new QShortcut(QKeySequence(QStringLiteral("Ctrl+H")), this);
    focusSidebar->setContext(Qt::WidgetWithChildrenShortcut);
    connect(focusSidebar, &QShortcut::activated, this, [this] {
        auto* editor = activeEditor();
        if (editor != nullptr && editor->mode() == EditorMode::Normal) {
            prefixRouter_->cancelPending();
            sidebar_->focusTree();
        }
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
}

MainWindow::~MainWindow() {
    if (auto* application = QApplication::instance(); application != nullptr) {
        application->removeEventFilter(this);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
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
    } else if (insideEditor && event->type() == QEvent::KeyPress) {
        auto& keyEvent = *static_cast<QKeyEvent*>(event);
        auto* editor = activeEditor();
        if (editor != nullptr && prefixRouter_->route(keyEvent, editor->mode())) {
            return true;
        }
        if (handleBufferSwitch(keyEvent)) {
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::handleBufferSwitch(const QKeyEvent& event) {
    const auto* editor = activeEditor();
    if (editor == nullptr || editor->mode() != EditorMode::Normal) {
        return false;
    }
    if (event.modifiers() != Qt::ShiftModifier) {
        return false;
    }
    if (buffers_.count() < 2) {
        // Nothing to switch to; leave the key to the editor rather than
        // swallowing it silently.
        return false;
    }

    std::optional<BufferId> target;
    if (event.key() == Qt::Key_L) {
        target = buffers_.activateNext();
    } else if (event.key() == Qt::Key_H) {
        target = buffers_.activatePrevious();
    }

    if (!target.has_value()) {
        return false;
    }
    showBuffer(*target);
    return true;
}

bool MainWindow::interceptEditorFileCommand(QObject* watched, const QKeyEvent& event) {
    if (event.key() != Qt::Key_Return && event.key() != Qt::Key_Enter) {
        return false;
    }
    auto* commandLine = qobject_cast<QLineEdit*>(watched);
    if (commandLine == nullptr || editorStack_ == nullptr ||
        !editorStack_->isAncestorOf(commandLine)) {
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
    // Reloading goes the same way: the editor's own `:e` would replace the
    // buffer without consulting the revision the application is tracking.
    static const QStringList kEditVerbs = {QStringLiteral("e"), QStringLiteral("e!"),
                                           QStringLiteral("edit"), QStringLiteral("edit!")};
    const auto isWrite = kWriteVerbs.contains(verb);
    const auto isEdit = kEditVerbs.contains(verb);
    if (!isWrite && !isEdit) {
        return false;
    }

    // Dismiss the editor's command bar the way Escape would, then answer in the
    // status area.
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
    } else {
        failure = QStringLiteral("%1 is not available yet; use :w [path]").arg(verb);
    }
    if (!failure.isEmpty()) {
        statusArea_->setText(failure);
    }
    return true;
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

    connect(&editor, &EditorAdapter::modeChanged, this, [this] { refreshEditorStatus(); });
    connect(&editor, &EditorAdapter::modifiedChanged, this, [this, id](bool modified) {
        buffers_.setModified(id, modified);
        syncBufferStrip();
        refreshEditorStatus();
    });
    return editor;
}

void MainWindow::showBuffer(BufferId id) {
    const auto editor = editors_.find(id);
    if (editor == editors_.end()) {
        return;
    }

    prefixRouter_->cancelPending();
    editorStack_->setCurrentWidget(editor->second->widget());
    syncBufferStrip();
    refreshEditorStatus();
    if (editor->second->widget() != nullptr) {
        editor->second->widget()->setFocus(Qt::OtherFocusReason);
    }
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
    statusArea_->setText(QStringLiteral("Wrote %1").arg(displayName(*written)));
    return {};
}

void MainWindow::refreshEditorStatus() {
    const auto* editor = activeEditor();
    if (editor == nullptr) {
        return;
    }

    const auto active = buffers_.activeId();
    const auto* state = active.has_value() ? buffers_.find(*active) : nullptr;
    const auto name = state != nullptr ? state->displayName : scratchDisplayName();
    const auto modifiedMarker = editor->isModified() ? QStringLiteral(" [+]") : QString{};
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
    statusArea_->setText(QStringLiteral("%1    %2%3%4")
                             .arg(editor->modeName().toUpper(), name, modifiedMarker, diskMarker));
}

} // namespace omanotes
