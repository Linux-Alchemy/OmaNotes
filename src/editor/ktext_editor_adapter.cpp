#include "editor/ktext_editor_adapter.hpp"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <KActionCollection>
#include <QAction>
#include <QKeySequence>
#include <QWidget>

namespace omanotes {

namespace {

void releaseShortcutToVi(KTextEditor::View& view, const QKeySequence& sequence) {
    for (auto* action : view.actionCollection()->actions()) {
        auto shortcuts = action->shortcuts();
        if (shortcuts.removeAll(sequence) > 0) {
            action->setShortcuts(shortcuts);
        }
    }
}

void releaseCanonicalViShortcuts(KTextEditor::View& view) {
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+B")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+F")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+R")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+V")));
}

} // namespace

KTextEditorAdapter::KTextEditorAdapter(QWidget* viewParent, QObject* parent)
    : EditorAdapter(parent), document_(KTextEditor::Editor::instance()->createDocument(this)),
      view_(document_->createView(viewParent)) {
    view_->setObjectName(QStringLiteral("editorPane"));
    view_->setAccessibleName(QStringLiteral("Markdown editor"));
    view_->setViewInputMode(KTextEditor::View::ViInputMode);
    view_->setStatusBarEnabled(false);
    view_->setConfigValue(QStringLiteral("line-numbers"), false);
    view_->setConfigValue(QStringLiteral("icon-bar"), false);
    view_->setConfigValue(QStringLiteral("folding-bar"), false);
    view_->setConfigValue(QStringLiteral("dynamic-word-wrap"), true);
    view_->setConfigValue(QStringLiteral("scrollbar-minimap"), false);
    document_->setHighlightingMode(QStringLiteral("Markdown"));
    releaseCanonicalViShortcuts(*view_);

    connect(
        view_, &KTextEditor::View::viewModeChanged, this,
        [this](KTextEditor::View*, KTextEditor::View::ViewMode) { emit modeChanged(modeName()); });
    connect(document_, &KTextEditor::Document::modifiedChanged, this,
            [this](KTextEditor::Document*) { emit modifiedChanged(isModified()); });
}

KTextEditorAdapter::~KTextEditorAdapter() = default;

QWidget* KTextEditorAdapter::widget() noexcept { return view_; }

QString KTextEditorAdapter::text() const { return document_->text(); }

void KTextEditorAdapter::setText(const QString& text) { document_->setText(text); }

bool KTextEditorAdapter::isModified() const noexcept { return document_->isModified(); }

QString KTextEditorAdapter::modeName() const { return view_->viewModeHuman(); }

} // namespace omanotes
