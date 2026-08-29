#include "ui/main_window.hpp"

#include "editor/editor_adapter.hpp"
#include "editor/ktext_editor_adapter.hpp"

#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>

namespace omanotes {

namespace {

constexpr auto kWindowTitle = "Omanotes";
constexpr auto kNoNameLabel = "[No Name]";

QFrame* buildSidebar(QWidget* parent) {
    auto* sidebar = new QFrame(parent);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAccessibleName(QStringLiteral("Workspace files"));
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setMinimumWidth(180);

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 12, 8, 12);
    layout->setSpacing(8);

    auto* heading = new QLabel(QStringLiteral("Workspace"), sidebar);
    heading->setObjectName(QStringLiteral("sidebarHeading"));

    auto* placeholder = new QListWidget(sidebar);
    placeholder->setObjectName(QStringLiteral("fileTreePlaceholder"));
    placeholder->setAccessibleName(QStringLiteral("Workspace file tree"));
    placeholder->addItem(QStringLiteral("No Markdown files loaded"));
    placeholder->setEnabled(false);
    placeholder->setFrameShape(QFrame::NoFrame);

    layout->addWidget(heading);
    layout->addWidget(placeholder, 1);
    return sidebar;
}

QWidget* buildWritingArea(QWidget* parent, std::unique_ptr<EditorAdapter>& editor) {
    auto* writingArea = new QWidget(parent);
    writingArea->setObjectName(QStringLiteral("writingArea"));
    editor = std::make_unique<KTextEditorAdapter>(writingArea);

    auto* layout = new QVBoxLayout(writingArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* buffers = new QTabBar(writingArea);
    buffers->setObjectName(QStringLiteral("bufferStrip"));
    buffers->setAccessibleName(QStringLiteral("Open buffers"));
    buffers->setExpanding(false);
    buffers->setMovable(false);
    buffers->addTab(QString::fromLatin1(kNoNameLabel));

    auto* status = new QLabel(writingArea);
    status->setObjectName(QStringLiteral("statusArea"));
    status->setAccessibleName(QStringLiteral("Editor status"));
    status->setContentsMargins(10, 6, 10, 6);

    const auto updateStatus = [status, editorPtr = editor.get()]() {
        const auto modifiedMarker = editorPtr->isModified() ? QStringLiteral(" [+]") : QString{};
        status->setText(QStringLiteral("%1    [No Name]%2")
                            .arg(editorPtr->modeName().toUpper(), modifiedMarker));
    };
    QObject::connect(editor.get(), &EditorAdapter::modeChanged, status, updateStatus);
    QObject::connect(editor.get(), &EditorAdapter::modifiedChanged, status, updateStatus);
    updateStatus();

    layout->addWidget(buffers);
    layout->addWidget(editor->widget(), 1);
    layout->addWidget(status);
    return writingArea;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QString::fromLatin1(kWindowTitle));
    setMinimumSize(720, 480);
    resize(1100, 720);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("workspaceSplitter"));
    splitter->setAccessibleName(QStringLiteral("Workspace and editor panes"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildSidebar(splitter));
    splitter->addWidget(buildWritingArea(splitter, editor_));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 860});

    setCentralWidget(splitter);

    if (editor_->widget() != nullptr) {
        editor_->widget()->setFocus(Qt::OtherFocusReason);
    }
}

MainWindow::~MainWindow() = default;

} // namespace omanotes
