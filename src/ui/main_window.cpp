#include "ui/main_window.hpp"

#include "app/prefix_router.hpp"
#include "editor/editor_adapter.hpp"
#include "editor/ktext_editor_adapter.hpp"
#include "ui/sidebar.hpp"
#include "workspace/workspace_root.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QShortcut>
#include <QSplitter>
#include <QStringDecoder>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>

#include <fstream>
#include <iterator>

namespace omanotes {

namespace {

constexpr auto kWindowTitle = "Omanotes";
constexpr auto kNoNameLabel = "[No Name]";

QString displayName(const std::filesystem::path& path) {
    return QFile::decodeName(QByteArray::fromStdString(path.filename().native()));
}

QWidget* buildWritingArea(QWidget* parent, std::unique_ptr<EditorAdapter>& editor,
                          QTabBar*& buffers, QLabel*& status) {
    auto* writingArea = new QWidget(parent);
    writingArea->setObjectName(QStringLiteral("writingArea"));
    editor = std::make_unique<KTextEditorAdapter>(writingArea);

    auto* layout = new QVBoxLayout(writingArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buffers = new QTabBar(writingArea);
    buffers->setObjectName(QStringLiteral("bufferStrip"));
    buffers->setAccessibleName(QStringLiteral("Open buffers"));
    buffers->setExpanding(false);
    buffers->setMovable(false);
    buffers->addTab(QString::fromLatin1(kNoNameLabel));

    status = new QLabel(writingArea);
    status->setObjectName(QStringLiteral("statusArea"));
    status->setAccessibleName(QStringLiteral("Editor status"));
    status->setContentsMargins(10, 6, 10, 6);

    layout->addWidget(buffers);
    layout->addWidget(editor->widget(), 1);
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
    splitter->addWidget(buildWritingArea(splitter, editor_, bufferStrip_, statusArea_));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 860});

    setCentralWidget(splitter);

    connect(editor_.get(), &EditorAdapter::modeChanged, this, [this] { refreshEditorStatus(); });
    connect(editor_.get(), &EditorAdapter::modifiedChanged, this,
            [this] { refreshEditorStatus(); });
    connect(prefixRouter_.get(), &PrefixRouter::feedbackChanged, statusArea_,
            [this](const QString& message) { statusArea_->setText(message); });
    connect(sidebar_, &Sidebar::fileActivated, this,
            [this](const std::filesystem::path& path) { loadFile(path); });
    connect(sidebar_, &Sidebar::editorFocusRequested, this, [this] {
        if (editor_->widget() != nullptr) {
            editor_->widget()->setFocus(Qt::ShortcutFocusReason);
        }
    });
    auto* focusSidebar = new QShortcut(QKeySequence(QStringLiteral("Ctrl+H")), this);
    focusSidebar->setContext(Qt::WidgetWithChildrenShortcut);
    connect(focusSidebar, &QShortcut::activated, this, [this] {
        if (editor_->mode() == EditorMode::Normal) {
            prefixRouter_->cancelPending();
            sidebar_->focusTree();
        }
    });
    refreshEditorStatus();

    if (auto* application = QApplication::instance(); application != nullptr) {
        application->installEventFilter(this);
    }
    if (editor_->widget() != nullptr) {
        editor_->widget()->setFocus(Qt::OtherFocusReason);
    }

    if (launchRequest_.requestedFile.has_value()) {
        loadFile(*launchRequest_.requestedFile);
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
        watchedWidget != nullptr && editor_->widget() != nullptr &&
        (watchedWidget == editor_->widget() || editor_->widget()->isAncestorOf(watchedWidget));

    if (insideWindow && event->type() == QEvent::MouseButtonPress) {
        prefixRouter_->cancelPending();
    } else if (insideEditor && event->type() == QEvent::KeyPress) {
        auto& keyEvent = *static_cast<QKeyEvent*>(event);
        if (prefixRouter_->route(keyEvent, editor_->mode())) {
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::loadFile(const std::filesystem::path& path) {
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

    currentFile_ = *resolved;
    bufferStrip_->setTabText(0, displayName(*resolved));
    editor_->loadText({text, displayName(*resolved)});
    refreshEditorStatus();
}

void MainWindow::refreshEditorStatus() {
    const auto modifiedMarker = editor_->isModified() ? QStringLiteral(" [+]") : QString{};
    const auto fileName =
        currentFile_.has_value() ? displayName(*currentFile_) : QString::fromLatin1(kNoNameLabel);
    statusArea_->setText(
        QStringLiteral("%1    %2%3").arg(editor_->modeName().toUpper(), fileName, modifiedMarker));
}

} // namespace omanotes
