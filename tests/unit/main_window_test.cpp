#include "app/launch_request.hpp"
#include "ui/main_window.hpp"
#include "workspace/file_tree_model.hpp"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QFile>
#include <QLabel>
#include <QSplitter>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

#include <filesystem>

namespace {

omanotes::LaunchRequest launchRequest() {
    return {std::filesystem::canonical(std::filesystem::current_path()), std::nullopt, false};
}

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

QModelIndex findIndex(omanotes::FileTreeModel& model, const QString& name,
                      const QModelIndex& parent = {}) {
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const auto index = model.index(row, 0, parent);
        if (index.data().toString() == name) {
            return index;
        }
    }
    return {};
}

} // namespace

class MainWindowTest final : public QObject {
    Q_OBJECT

  private slots:
    void hasRequiredRegions();
    void editorReceivesInitialFocus();
    void resizesWithoutLosingRegions();
    void statusTracksEditorState();
    void spacePrefixDoesNotSwallowInsertTextOrControlB();
    void generalSidebarShowsAndLoadsTextWithoutWriting();
    void supportsVimStyleSidebarAndPaneNavigation();
    void loadsExplicitTextFileWithKateHighlighting();
    void rejectsBinaryLookingFileClearly();
    void closesCleanly();
};

void MainWindowTest::hasRequiredRegions() {
    omanotes::MainWindow window(launchRequest());

    QVERIFY(window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("sidebar")) != nullptr);
    QVERIFY(window.findChild<QTabBar*>(QStringLiteral("bufferStrip")) != nullptr);
    QVERIFY(window.findChild<KTextEditor::View*>(QStringLiteral("editorPane")) != nullptr);
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("statusArea")) != nullptr);
}

void MainWindowTest::statusTracksEditorState() {
    omanotes::MainWindow window(launchRequest());
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
    omanotes::MainWindow window(launchRequest());
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

    QTest::keyClick(eventTarget, Qt::Key_Space);
    QKeyEvent shiftPress(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    QApplication::sendEvent(eventTarget, &shiftPress);
    QTRY_COMPARE(status->text(), QStringLiteral("Space …"));
    QKeyEvent questionPress(QEvent::KeyPress, Qt::Key_Question, Qt::ShiftModifier,
                            QStringLiteral("?"));
    QApplication::sendEvent(eventTarget, &questionPress);
    QTRY_COMPARE(status->text(), QStringLiteral("Space+? command is not available yet"));
    QCOMPARE(editor->document()->text(), QStringLiteral("alpha"));

    QTest::keyClicks(eventTarget, QStringLiteral("ihello world"));
    QCOMPARE(editor->document()->text(), QStringLiteral("hello worldalpha"));
    QTest::keyClick(eventTarget, Qt::Key_Escape);

    editor->document()->setText(QString(40, QLatin1Char('\n')));
    editor->setCursorPosition(KTextEditor::Cursor(39, 0));
    QTest::keyClick(eventTarget, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(editor->cursorPosition().line() < 39);
}

void MainWindowTest::generalSidebarShowsAndLoadsTextWithoutWriting() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    std::filesystem::create_directory(root / "folder");
    writeFile(root / "note.md", "# Original\n");
    writeFile(root / "script.py", "print('hello')\n");
    writeFile(root / ".env", "VISIBLE=yes\n");

    omanotes::MainWindow window({std::filesystem::canonical(root), std::nullopt, false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(tree != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(buffers != nullptr);

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    const auto script = findIndex(*model, QStringLiteral("script.py"));
    QVERIFY(script.isValid());
    QVERIFY(findIndex(*model, QStringLiteral("note.md")).isValid());
    QVERIFY(findIndex(*model, QStringLiteral(".env")).isValid());

    tree->scrollTo(script);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(script).center());
    QTRY_COMPARE(editor->document()->text(), QStringLiteral("print('hello')\n"));
    QCOMPARE(editor->document()->highlightingMode(), QStringLiteral("Python"));
    QCOMPARE(buffers->tabText(0), QStringLiteral("script.py"));
    QVERIFY(!editor->document()->isModified());

    editor->document()->setText(QStringLiteral("changed in memory"));
    QFile diskFile(QString::fromStdString((root / "script.py").string()));
    QVERIFY(diskFile.open(QIODevice::ReadOnly));
    QCOMPARE(diskFile.readAll(), QByteArray("print('hello')\n"));
}

void MainWindowTest::supportsVimStyleSidebarAndPaneNavigation() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto folder = root / "folder";
    std::filesystem::create_directory(folder);
    writeFile(folder / "nested.md", "# Nested\n");
    writeFile(root / "root.md", "# Root\n");

    omanotes::MainWindow window({std::filesystem::canonical(root), std::nullopt, false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    QVERIFY(tree != nullptr);
    QVERIFY(editor != nullptr);
    QTRY_VERIFY(editor->hasFocus());

    auto* editorTarget = QApplication::focusWidget();
    QVERIFY(editorTarget != nullptr);
    QTest::keyClick(editorTarget, Qt::Key_H, Qt::ControlModifier);
    QTRY_VERIFY(tree->hasFocus());

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    const auto folderIndex = findIndex(*model, QStringLiteral("folder"));
    QVERIFY(folderIndex.isValid());
    tree->setCurrentIndex(folderIndex);

    QTest::keyClick(tree, Qt::Key_L);
    QTRY_VERIFY(tree->isExpanded(folderIndex));
    QTest::keyClick(tree, Qt::Key_L);
    QTRY_COMPARE(tree->currentIndex().data().toString(), QStringLiteral("nested.md"));
    QTest::keyClick(tree, Qt::Key_Return);
    QTRY_COMPARE(editor->document()->text(), QStringLiteral("# Nested\n"));

    QTest::keyClick(tree, Qt::Key_H);
    QCOMPARE(tree->currentIndex(), folderIndex);
    QTest::keyClick(tree, Qt::Key_H);
    QVERIFY(!tree->isExpanded(folderIndex));
    QTest::keyClick(tree, Qt::Key_J);
    QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("root.md"));
    QTest::keyClick(tree, Qt::Key_K);
    QCOMPARE(tree->currentIndex(), folderIndex);

    QTest::keyClick(tree, Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(editor->hasFocus());
}

void MainWindowTest::loadsExplicitTextFileWithKateHighlighting() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto textFile = root / "script.py";
    writeFile(textFile, "value = 42\n");

    omanotes::MainWindow window(
        {std::filesystem::canonical(root), std::filesystem::canonical(textFile), false});
    window.show();
    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(editor != nullptr);
    QVERIFY(buffers != nullptr);
    QCOMPARE(editor->document()->text(), QStringLiteral("value = 42\n"));
    QCOMPARE(editor->document()->highlightingMode(), QStringLiteral("Python"));
    QCOMPARE(buffers->tabText(0), QStringLiteral("script.py"));
}

void MainWindowTest::rejectsBinaryLookingFileClearly() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto binaryFile = root / "sample.bin";
    writeFile(binaryFile, QByteArray("text\0binary", 11));

    omanotes::MainWindow window(
        {std::filesystem::canonical(root), std::filesystem::canonical(binaryFile), false});
    window.show();
    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    QVERIFY(editor != nullptr);
    QVERIFY(status != nullptr);
    QCOMPARE(editor->document()->text(), QString{});
    QCOMPARE(status->text(), QStringLiteral("Refusing binary-looking file: sample.bin"));
}

void MainWindowTest::editorReceivesInitialFocus() {
    omanotes::MainWindow window(launchRequest());
    window.show();

    auto* editor = window.findChild<KTextEditor::View*>(QStringLiteral("editorPane"));
    QVERIFY(editor != nullptr);
    QTRY_VERIFY(editor->hasFocus());
}

void MainWindowTest::resizesWithoutLosingRegions() {
    omanotes::MainWindow window(launchRequest());
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
    omanotes::MainWindow window(launchRequest());
    window.show();
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
}

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
