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

QTEST_MAIN(KTextEditorAdapterTest)
#include "ktext_editor_adapter_test.moc"
