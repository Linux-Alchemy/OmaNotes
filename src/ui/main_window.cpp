#include "ui/main_window.hpp"

#include "app/prefix_router.hpp"
#include "editor/editor_adapter.hpp"
#include "editor/ktext_editor_adapter.hpp"
#include "ui/buffer_strip.hpp"
#include "ui/sidebar.hpp"
#include "workspace/workspace_root.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QVBoxLayout>
#include <QWidget>

#include <fstream>
#include <iterator>
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

QWidget* buildWritingArea(QWidget* parent, QStackedWidget*& editors, BufferStrip*& buffers,
                          QLabel*& status) {
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

    layout->addWidget(buffers);
    layout->addWidget(editors, 1);
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
    splitter->addWidget(buildWritingArea(splitter, editorStack_, bufferStrip_, statusArea_));
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
    const auto insideEditor =
        watchedWidget != nullptr && editorStack_ != nullptr &&
        (watchedWidget == editorStack_ || editorStack_->isAncestorOf(watchedWidget));

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

    std::ifstream input(*resolved, std::ios::binary);
    if (!input) {
        statusArea_->setText(QStringLiteral("Could not read %1").arg(displayName(*resolved)));
        return;
    }
    const std::string bytes(std::istreambuf_iterator<char>(input), {});
    if (bytes.find('\0') != std::string::npos) {
        statusArea_->setText(
            QStringLiteral("Refusing binary-looking file: %1").arg(displayName(*resolved)));
        return;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const auto encoded = QByteArray::fromStdString(bytes);
    const QString text = decoder(encoded);
    if (decoder.hasError()) {
        statusArea_->setText(
            QStringLiteral("File is not valid UTF-8: %1").arg(displayName(*resolved)));
        return;
    }

    const auto opened = buffers_.open(*resolved);
    if (!opened.has_value()) {
        statusArea_->setText(QString::fromStdString(opened.error().message));
        return;
    }

    createEditorFor(*opened).loadText(text);
    showBuffer(*opened);
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
    statusArea_->setText(
        QStringLiteral("%1    %2%3").arg(editor->modeName().toUpper(), name, modifiedMarker));
}

} // namespace omanotes
