#include "ui/main_window.hpp"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QLabel>
#include <QSplitter>
#include <QTabBar>
#include <QtTest>

class MainWindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void hasRequiredRegions();
    void editorReceivesInitialFocus();
    void resizesWithoutLosingRegions();
    void statusTracksEditorState();
    void spacePrefixDoesNotSwallowInsertTextOrControlB();
    void closesCleanly();
};

void MainWindowTest::hasRequiredRegions() {
    omanotes::MainWindow window;

    QVERIFY(window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("sidebar")) != nullptr);
    QVERIFY(window.findChild<QTabBar*>(QStringLiteral("bufferStrip")) != nullptr);
    QVERIFY(window.findChild<KTextEditor::View*>(QStringLiteral("editorPane")) != nullptr);
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("statusArea")) != nullptr);
}

void MainWindowTest::statusTracksEditorState() {
    omanotes::MainWindow window;
    window.show();
    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));

    QVERIFY(editor != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(status->text().contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive));

    editor->document()->setText(QStringLiteral("scratch"));
    QTRY_VERIFY(status->text().contains(QStringLiteral("[+]")));

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);
    QTest::keyClicks(eventTarget, QStringLiteral("i"));
    QTRY_VERIFY(status->text().contains(QStringLiteral("INSERT"), Qt::CaseInsensitive));
}

void MainWindowTest::spacePrefixDoesNotSwallowInsertTextOrControlB() {
    omanotes::MainWindow window;
    window.show();
    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    QVERIFY(editor != nullptr);
    QVERIFY(status != nullptr);

    editor->document()->setText(QStringLiteral("alpha"));
    editor->document()->setModified(false);
    editor->setCursorPosition(KTextEditor::Cursor(0, 0));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);

    QTest::keyClick(eventTarget, Qt::Key_Space);
    QTRY_COMPARE(status->text(), QStringLiteral("Space …"));
    QTest::keyClicks(eventTarget, QStringLiteral("x"));
    QTRY_COMPARE(status->text(), QStringLiteral("Unknown application command: Space+x"));
    QCOMPARE(editor->document()->text(), QStringLiteral("alpha"));

    QTest::keyClicks(eventTarget, QStringLiteral("ihello world"));
    QCOMPARE(editor->document()->text(), QStringLiteral("hello worldalpha"));
    QTest::keyClick(eventTarget, Qt::Key_Escape);

    editor->document()->setText(QString(40, QLatin1Char('\n')));
    editor->setCursorPosition(KTextEditor::Cursor(39, 0));
    QTest::keyClick(eventTarget, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(editor->cursorPosition().line() < 39);
}

void MainWindowTest::editorReceivesInitialFocus() {
    omanotes::MainWindow window;
    window.show();

    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    QVERIFY(editor != nullptr);
    QTRY_VERIFY(editor->hasFocus());
}

void MainWindowTest::resizesWithoutLosingRegions() {
    omanotes::MainWindow window;
    window.show();
    window.resize(800, 520);
    QCoreApplication::processEvents();

    const auto* splitter = window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter"));
    QVERIFY(splitter != nullptr);
    QCOMPARE(splitter->count(), 2);
    QVERIFY(splitter->sizes().at(0) > 0);
    QVERIFY(splitter->sizes().at(1) > 0);
}

void MainWindowTest::closesCleanly() {
    omanotes::MainWindow window;
    window.show();
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
}

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
