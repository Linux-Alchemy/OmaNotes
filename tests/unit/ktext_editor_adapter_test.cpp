#include "editor/ktext_editor_adapter.hpp"
#include "ui/theme_adapter.hpp"

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <KActionCollection>
#include <QAction>
#include <QKeySequence>
#include <QPointer>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

#include <memory>

class KTextEditorAdapterTest final : public QObject {
    Q_OBJECT

  private slots:
    void ownsEditorLifetime();
    void configuresMarkdownWritingView();
    void roundTripsTextAndTracksModification();
    void loadsFileTextAsCleanMemoryOnlyContent();
    void reportsViModeTransitions();
    void neverGivesTheDocumentAUrl();
    void ignoresModelinesInNoteText();
};

void KTextEditorAdapterTest::ownsEditorLifetime() {
    QWidget parent;

    for (auto iteration = 0; iteration < 50; ++iteration) {
        auto adapter = std::make_unique<omanotes::KTextEditorAdapter>(&parent);
        QPointer<QWidget> view(adapter->widget());
        QVERIFY(view != nullptr);
        adapter.reset();
        QVERIFY(view == nullptr);
    }
}

void KTextEditorAdapterTest::configuresMarkdownWritingView() {
    QWidget parent;
    omanotes::KTextEditorAdapter adapter(&parent);
    auto* view = qobject_cast<KTextEditor::View*>(adapter.widget());

    QVERIFY(view != nullptr);
    QCOMPARE(view->viewInputMode(), KTextEditor::View::ViInputMode);
    QCOMPARE(adapter.mode(), omanotes::EditorMode::Normal);
    QCOMPARE(view->document()->highlightingMode(), QStringLiteral("Markdown"));
    QCOMPARE(view->configValue(QStringLiteral("dynamic-word-wrap")).toBool(), true);
    QCOMPARE(view->configValue(QStringLiteral("line-numbers")).toBool(), false);
    QCOMPARE(view->configValue(QStringLiteral("icon-bar")).toBool(), false);
    QCOMPARE(view->configValue(QStringLiteral("folding-bar")).toBool(), false);
    QCOMPARE(view->configValue(QStringLiteral("scrollbar-minimap")).toBool(), false);
    QVERIFY(!view->isStatusBarEnabled());

    // The bundled themes are found through KSyntaxHighlighting's addons
    // resource path, and their current-line colour is fully transparent:
    // no bar under the cursor line (Matt's call, 2026-09-10).
    const auto& repository = KTextEditor::Editor::instance()->repository();
    for (const auto* name : {"OmaNotes Dark", "OmaNotes Light"}) {
        const auto theme = repository.theme(QString::fromLatin1(name));
        QVERIFY2(theme.isValid(), name);
        QCOMPARE(qAlpha(theme.editorColor(KSyntaxHighlighting::Theme::CurrentLine)), 0);
        // Likewise the icon-border separator: a hairline the hidden border
        // would otherwise leave down the text area's left edge.
        QCOMPARE(qAlpha(theme.editorColor(KSyntaxHighlighting::Theme::Separator)), 0);
    }
    for (const bool dark : {true, false}) {
        omanotes::ThemePalette palette;
        palette.dark = dark;
        palette.background = QColor(dark ? QStringLiteral("#101018") : QStringLiteral("#fafafa"));
        palette.selection = QColor(QStringLiteral("#2d5c76"));
        adapter.applyTheme(palette);
        QCOMPARE(view->configValue(QStringLiteral("theme")).toString(),
                 dark ? QStringLiteral("OmaNotes Dark") : QStringLiteral("OmaNotes Light"));
        QCOMPARE(qAlpha(view->theme().editorColor(KSyntaxHighlighting::Theme::CurrentLine)), 0);
    }

    const auto releasedSequences = {
        QKeySequence(QStringLiteral("Ctrl+H")),      QKeySequence(QStringLiteral("Ctrl+B")),
        QKeySequence(QStringLiteral("Ctrl+F")),      QKeySequence(QStringLiteral("Ctrl+R")),
        QKeySequence(QStringLiteral("Ctrl+D")),      QKeySequence(QStringLiteral("Ctrl+U")),
        QKeySequence(QStringLiteral("Ctrl+V")),      QKeySequence(QStringLiteral("Ctrl+S")),
        QKeySequence(QStringLiteral("Ctrl+Shift+S"))};
    for (const auto& sequence : releasedSequences) {
        for (const auto* action : view->actionCollection()->actions()) {
            QVERIFY2(!action->shortcuts().contains(sequence), qPrintable(action->objectName()));
        }
    }
}

void KTextEditorAdapterTest::loadsFileTextAsCleanMemoryOnlyContent() {
    QWidget parent;
    omanotes::KTextEditorAdapter adapter(&parent);

    adapter.loadText(QStringLiteral("# Loaded\n"));

    QCOMPARE(adapter.text(), QStringLiteral("# Loaded\n"));
    QVERIFY(!adapter.isModified());
}

void KTextEditorAdapterTest::roundTripsTextAndTracksModification() {
    QWidget parent;
    omanotes::KTextEditorAdapter adapter(&parent);
    QSignalSpy modifiedSpy(&adapter, &omanotes::EditorAdapter::modifiedChanged);

    QVERIFY(!adapter.isModified());
    adapter.setText(QStringLiteral("# Omanotes\n\nA scratch document.\n"));

    QCOMPARE(adapter.text(), QStringLiteral("# Omanotes\n\nA scratch document.\n"));
    QVERIFY(adapter.isModified());
    QCOMPARE(modifiedSpy.count(), 1);
}

void KTextEditorAdapterTest::reportsViModeTransitions() {
    QWidget parent;
    omanotes::KTextEditorAdapter adapter(&parent);
    QSignalSpy modeSpy(&adapter, &omanotes::EditorAdapter::modeChanged);
    adapter.setText(QStringLiteral("scratch"));
    QVBoxLayout layout(&parent);
    layout.addWidget(adapter.widget());
    parent.show();
    adapter.widget()->setFocus();

    QTRY_VERIFY(adapter.widget()->hasFocus());
    auto* eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);
    QVERIFY(adapter.modeName().contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive));
    QTest::keyClicks(eventTarget, QStringLiteral("i"));
    QTRY_VERIFY(adapter.modeName().contains(QStringLiteral("INSERT"), Qt::CaseInsensitive));
    QCOMPARE(adapter.mode(), omanotes::EditorMode::Insert);
    QTest::keyClick(eventTarget, Qt::Key_Escape);
    QTRY_VERIFY(adapter.modeName().contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive));
    QCOMPARE(adapter.mode(), omanotes::EditorMode::Normal);
    QVERIFY(modeSpy.count() >= 2);
}

void KTextEditorAdapterTest::neverGivesTheDocumentAUrl() {
    // Load-bearing for the threat model (S1): KTextEditor's swap files,
    // backup files, encoding sniffing and disk reload all key off the
    // document's URL. The adapter feeds text and never a URL, so none of
    // them has anything to act on. A future `openUrl` call must fail here.
    QWidget parent;
    omanotes::KTextEditorAdapter adapter(&parent);
    auto* view = qobject_cast<KTextEditor::View*>(adapter.widget());
    QVERIFY(view != nullptr);
    auto* document = view->document();
    if (document == nullptr) {
        QFAIL("the view has no document");
    }
    QVERIFY(document->url().isEmpty());
    adapter.loadText(QStringLiteral("# Loaded\n"));
    adapter.setText(QStringLiteral("# Set\n"));
    adapter.markModified();
    adapter.markSaved();
    QVERIFY(document->url().isEmpty());
}

void KTextEditorAdapterTest::ignoresModelinesInNoteText() {
    // `kate:` variable lines are read by KTextEditor when it opens a file
    // itself. On this adapter's load path they are inert (verified by
    // experiment on 2026-09-11, docs/threat-model.md T-E4); this pins it.
    QWidget parent;
    omanotes::KTextEditorAdapter adapter(&parent);
    auto* view = qobject_cast<KTextEditor::View*>(adapter.widget());
    QVERIFY(view != nullptr);
    auto* document = view->document();
    if (document == nullptr) {
        QFAIL("the view has no document");
    }
    const auto indentWidth = document->configValue(QStringLiteral("indent-width"));
    const auto tabWidth = document->configValue(QStringLiteral("tab-width"));
    const auto replaceTabs = document->configValue(QStringLiteral("replace-tabs"));
    const auto wordWrap = view->configValue(QStringLiteral("dynamic-word-wrap"));
    const auto lineNumbers = view->configValue(QStringLiteral("line-numbers"));

    const auto hostile = QStringLiteral(
        "<!-- kate: indent-width 7; tab-width 9; replace-tabs off; dynamic-word-wrap off; "
        "line-numbers on; remove-trailing-spaces all; hl C++; -->\n"
        "body\n");
    adapter.loadText(hostile);
    adapter.setText(hostile);
    document->setHighlightingMode(QStringLiteral("Markdown"));
    adapter.loadText(QStringLiteral("body\n\n\n<!-- kate: indent-width 5; -->\n"));
    adapter.markModified();
    adapter.markSaved();
    QTest::qWait(50);

    QCOMPARE(document->configValue(QStringLiteral("indent-width")), indentWidth);
    QCOMPARE(document->configValue(QStringLiteral("tab-width")), tabWidth);
    QCOMPARE(document->configValue(QStringLiteral("replace-tabs")), replaceTabs);
    QCOMPARE(view->configValue(QStringLiteral("dynamic-word-wrap")), wordWrap);
    QCOMPARE(view->configValue(QStringLiteral("line-numbers")), lineNumbers);
    QCOMPARE(document->highlightingMode(), QStringLiteral("Markdown"));
}

QTEST_MAIN(KTextEditorAdapterTest)
#include "ktext_editor_adapter_test.moc"
