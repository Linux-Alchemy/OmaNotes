#include "ui/sidebar.hpp"

#include "workspace/file_tree_model.hpp"

#include <QAbstractItemView>
#include <QByteArray>
#include <QFile>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QTreeView>
#include <QVBoxLayout>

namespace omanotes {

Sidebar::Sidebar(const std::filesystem::path& root, QWidget* parent)
    : QFrame(parent), model_(new FileTreeModel(root, this)), tree_(new QTreeView(this)) {
    setObjectName(QStringLiteral("sidebar"));
    setAccessibleName(QStringLiteral("Workspace files"));
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(180);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 8, 12);
    layout->setSpacing(8);

    auto* heading =
        new QLabel(QFile::decodeName(QByteArray::fromStdString(root.filename().native())), this);
    heading->setObjectName(QStringLiteral("sidebarHeading"));

    tree_->setObjectName(QStringLiteral("fileTree"));
    tree_->setAccessibleName(QStringLiteral("Workspace file tree"));
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(true);
    tree_->installEventFilter(this);

    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!model_->isDirectory(index)) {
            activate(index);
        }
    });

    layout->addWidget(heading);
    layout->addWidget(tree_, 1);
}

void Sidebar::focusTree() {
    tree_->setFocus(Qt::ShortcutFocusReason);
    if (!tree_->currentIndex().isValid()) {
        if (model_->canFetchMore({})) {
            model_->fetchMore({});
        }
        tree_->setCurrentIndex(model_->index(0, 0));
    }
}

void Sidebar::noteFileCreated(const std::filesystem::path& path) { model_->noteFileCreated(path); }

bool Sidebar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tree_ && event->type() == QEvent::KeyPress) {
        const auto& keyEvent = *static_cast<QKeyEvent*>(event);
        if (keyEvent.key() == Qt::Key_L && keyEvent.modifiers() == Qt::ControlModifier) {
            emit editorFocusRequested();
            return true;
        }
        if (keyEvent.modifiers() == Qt::NoModifier) {
            switch (keyEvent.key()) {
            case Qt::Key_J:
                moveSelection(true);
                return true;
            case Qt::Key_K:
                moveSelection(false);
                return true;
            case Qt::Key_H:
                moveLeft();
                return true;
            case Qt::Key_L:
                moveRight();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                activate(tree_->currentIndex());
                return true;
            default:
                break;
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void Sidebar::activate(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    if (model_->isDirectory(index)) {
        tree_->setExpanded(index, !tree_->isExpanded(index));
        return;
    }
    emit fileActivated(model_->pathForIndex(index));
}

void Sidebar::moveSelection(bool down) {
    auto current = tree_->currentIndex();
    if (!current.isValid()) {
        focusTree();
        return;
    }
    const auto next = down ? tree_->indexBelow(current) : tree_->indexAbove(current);
    if (next.isValid()) {
        tree_->setCurrentIndex(next);
    }
}

void Sidebar::moveLeft() {
    const auto current = tree_->currentIndex();
    if (!current.isValid()) {
        return;
    }
    if (model_->isDirectory(current) && tree_->isExpanded(current)) {
        tree_->collapse(current);
        return;
    }
    const auto parent = current.parent();
    if (parent.isValid()) {
        tree_->setCurrentIndex(parent);
    }
}

void Sidebar::moveRight() {
    const auto current = tree_->currentIndex();
    if (!current.isValid() || !model_->isDirectory(current)) {
        return;
    }
    if (!tree_->isExpanded(current)) {
        tree_->expand(current);
        return;
    }
    if (model_->canFetchMore(current)) {
        model_->fetchMore(current);
    }
    const auto child = model_->index(0, 0, current);
    if (child.isValid()) {
        tree_->setCurrentIndex(child);
    }
}

} // namespace omanotes
