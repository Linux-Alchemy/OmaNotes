#include "ui/main_window.hpp"

#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabBar>
#include <QtTest>

class MainWindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void hasRequiredRegions();
    void editorReceivesInitialFocus();
    void resizesWithoutLosingRegions();
    void closesCleanly();
};

void MainWindowTest::hasRequiredRegions() {
    omanotes::MainWindow window;

    QVERIFY(window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("sidebar")) != nullptr);
    QVERIFY(window.findChild<QTabBar*>(QStringLiteral("bufferStrip")) != nullptr);
    QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("editorPane")) != nullptr);
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("statusArea")) != nullptr);
}

void MainWindowTest::editorReceivesInitialFocus() {
    omanotes::MainWindow window;
    window.show();

    auto* editor = window.findChild<QPlainTextEdit*>(QStringLiteral("editorPane"));
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
