#include "editor/ktext_editor_adapter.hpp"

#include "ui/theme_adapter.hpp"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <KActionCollection>
#include <QAction>
#include <QFont>
#include <QKeySequence>
#include <QWidget>

#include <algorithm>

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
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+H")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+B")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+F")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+R")));
    // Kate binds Ctrl+D to Comment and Ctrl+U to Uppercase; in Vi they are
    // half-page down and up (and, while inserting, dedent and delete-to-start).
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+D")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+U")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+V")));
    // Copy routes through the application's edit.copy command so Omarchy's
    // Super+C (delivered as Ctrl+Meta+C) can reach it; a widget-level QAction
    // shortcut can never match the Meta-carrying chord.
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+C")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+S")));
    releaseShortcutToVi(view, QKeySequence(QStringLiteral("Ctrl+Shift+S")));
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

void KTextEditorAdapter::loadText(const QString& text) {
    // A reload from disk should not throw the cursor to the top of the note;
    // keep it where it was, clamped to whatever the new text still has.
    const auto previous = view_->cursorPosition();
    document_->setText(text);
    document_->setModified(false);
    const auto line = std::min(previous.line(), std::max(document_->lines() - 1, 0));
    const auto column = std::min(previous.column(), document_->lineLength(line));
    view_->setCursorPosition(KTextEditor::Cursor(line, column));
}

bool KTextEditorAdapter::isModified() const noexcept { return document_->isModified(); }

void KTextEditorAdapter::markSaved() { document_->setModified(false); }

void KTextEditorAdapter::markModified() { document_->setModified(true); }

EditorPosition KTextEditorAdapter::cursorPosition() const {
    const auto cursor = view_->cursorPosition();
    return {cursor.line(), cursor.column()};
}

void KTextEditorAdapter::setCursorPosition(EditorPosition position) {
    const auto line = std::clamp(position.line, 0, std::max(document_->lines() - 1, 0));
    const auto column = std::clamp(position.column, 0, document_->lineLength(line));
    view_->setCursorPosition(KTextEditor::Cursor(line, column));
}

int KTextEditorAdapter::firstVisibleLine() const { return view_->firstDisplayedLine(); }

void KTextEditorAdapter::scrollToLine(int line) {
    const auto clamped = std::clamp(line, 0, std::max(document_->lines() - 1, 0));
    view_->setScrollPosition(KTextEditor::Cursor(clamped, 0));
}

int KTextEditorAdapter::lineCount() const { return document_->lines(); }

EditorMode KTextEditorAdapter::mode() const noexcept {
    switch (view_->viewMode()) {
    case KTextEditor::View::ViModeNormal:
        return EditorMode::Normal;
    case KTextEditor::View::ViModeInsert:
        return EditorMode::Insert;
    case KTextEditor::View::ViModeVisual:
    case KTextEditor::View::ViModeVisualLine:
    case KTextEditor::View::ViModeVisualBlock:
        return EditorMode::Visual;
    case KTextEditor::View::ViModeReplace:
        return EditorMode::Replace;
    case KTextEditor::View::NormalModeInsert:
    case KTextEditor::View::NormalModeOverwrite:
        return EditorMode::Other;
    }

    return EditorMode::Other;
}

QString KTextEditorAdapter::modeName() const { return view_->viewModeHuman(); }

void KTextEditorAdapter::applyTheme(const ThemePalette& palette) {
    // The syntax theme supplies token colours in the right register; the
    // semantic grounds are overlaid so the editor sits in the same room as
    // the rest of the window.
    view_->setConfigValue(QStringLiteral("theme"), palette.dark ? QStringLiteral("Breeze Dark")
                                                                : QStringLiteral("Breeze Light"));
    view_->setConfigValue(QStringLiteral("background-color"), palette.background);
    view_->setConfigValue(QStringLiteral("selection-color"), palette.selection);
    // No current-line bar (Matt's call, 2026-09-10): the cursor says where
    // you are. KTextEditor has no public switch for the highlight, so it is
    // painted in the ground colour, which is the same as not painting it.
    view_->setConfigValue(QStringLiteral("current-line-color"), palette.background);
    auto editorFont = view_->configValue(QStringLiteral("font")).value<QFont>();
    editorFont.setPointSizeF(palette.baseFontPointSize);
    view_->setConfigValue(QStringLiteral("font"), editorFont);
}

} // namespace omanotes
