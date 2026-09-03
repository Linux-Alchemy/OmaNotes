#include "editor/ktext_editor_adapter.hpp"

#include <KTextEditor/Document>
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

    const auto viSequences = {
        QKeySequence(QStringLiteral("Ctrl+B")), QKeySequence(QStringLiteral("Ctrl+F")),
        QKeySequence(QStringLiteral("Ctrl+R")), QKeySequence(QStringLiteral("Ctrl+V"))};
    for (const auto& sequence : viSequences) {
        for (const auto* action : view->actionCollection()->actions()) {
            QVERIFY2(!action->shortcuts().contains(sequence), qPrintable(action->objectName()));
        }
    }
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
