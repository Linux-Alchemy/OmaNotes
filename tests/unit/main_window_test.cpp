#include "app/launch_request.hpp"
#include "ui/main_window.hpp"
#include "workspace/file_tree_model.hpp"

#include <KTextEditor/Command>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QStackedWidget>
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

KTextEditor::View* activeEditor(omanotes::MainWindow& window) {
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("editorStack"));
    return stack == nullptr ? nullptr : qobject_cast<KTextEditor::View*>(stack->currentWidget());
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
    void markdownSidebarFiltersAndLoadsWithoutWriting();
    void supportsVimStyleSidebarAndPaneNavigation();
    void rejectsExplicitNonMarkdownFileClearly();
    void opensEachFileInItsOwnBufferAndSwitchesWithShiftKeys();
    void reopeningAnOpenFileKeepsUnsavedEditsAndDoesNotDuplicate();
    void switchesBufferWhenTheStripIsClicked();
    void leavesShiftMotionsToTheEditorWithASingleBuffer();
    void savesAnOpenFileWithControlS();
    void namesAScratchBufferBeforeWritingIt();
    void cancelsScratchNamingWithoutWriting();
    void savesThroughTheEditorWriteCommand();
    void savesWhenWriteIsTypedOnTheViCommandLine();
    void showsANewlySavedNoteInTheSidebar();
    void refusesEditorWriteCommandsItDoesNotImplementYet();
    void doesNotLetNormalModeWriteShortcutsReachTheEditorsWriter();
    void closesCleanly();
};

void MainWindowTest::hasRequiredRegions() {
    omanotes::MainWindow window(launchRequest());

    QVERIFY(window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("sidebar")) != nullptr);
    QVERIFY(window.findChild<QTabBar*>(QStringLiteral("bufferStrip")) != nullptr);
    QVERIFY(window.findChild<QStackedWidget*>(QStringLiteral("editorStack")) != nullptr);
    QVERIFY(window.findChild<KTextEditor::View*>(QStringLiteral("editorPane")) != nullptr);
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("statusArea")) != nullptr);
}

void MainWindowTest::statusTracksEditorState() {
    omanotes::MainWindow window(launchRequest());
    window.show();
    auto* editor = activeEditor(window);
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
    auto* editor = activeEditor(window);
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

void MainWindowTest::markdownSidebarFiltersAndLoadsWithoutWriting() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    std::filesystem::create_directory(root / "folder");
    writeFile(root / "note.md", "# Original\n");
    writeFile(root / "ignored.txt", "not a note\n");

    omanotes::MainWindow window({std::filesystem::canonical(root), std::nullopt, false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* editor = activeEditor(window);
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(tree != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(buffers != nullptr);

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    const auto note = findIndex(*model, QStringLiteral("note.md"));
    QVERIFY(note.isValid());
    QVERIFY(!findIndex(*model, QStringLiteral("ignored.txt")).isValid());

    tree->scrollTo(note);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(note).center());
    QTRY_COMPARE(buffers->count(), 2);
    editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));
    QCOMPARE(buffers->tabText(0), QStringLiteral("[No Name]"));
    QCOMPARE(buffers->tabText(1), QStringLiteral("note.md"));
    QCOMPARE(buffers->currentIndex(), 1);
    QVERIFY(!editor->document()->isModified());

    editor->document()->setText(QStringLiteral("changed in memory"));
    QTRY_COMPARE(buffers->tabText(1), QStringLiteral("note.md [+]"));
    QFile diskFile(QString::fromStdString((root / "note.md").string()));
    QVERIFY(diskFile.open(QIODevice::ReadOnly));
    QCOMPARE(diskFile.readAll(), QByteArray("# Original\n"));
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
    auto* editor = activeEditor(window);
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
    QTRY_VERIFY(activeEditor(window) != nullptr &&
                activeEditor(window)->document()->text() == QStringLiteral("# Nested\n"));
    editor = activeEditor(window);

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

void MainWindowTest::rejectsExplicitNonMarkdownFileClearly() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto textFile = root / "plain.txt";
    writeFile(textFile, "plain text\n");

    omanotes::MainWindow window(
        {std::filesystem::canonical(root), std::filesystem::canonical(textFile), false});
    window.show();
    auto* editor = activeEditor(window);
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    QVERIFY(editor != nullptr);
    QVERIFY(status != nullptr);
    QCOMPARE(editor->document()->text(), QString{});
    QCOMPARE(status->text(), QStringLiteral("Only Markdown (.md) files can be opened"));
}

void MainWindowTest::opensEachFileInItsOwnBufferAndSwitchesWithShiftKeys() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "alpha.md", "# Alpha\n");
    writeFile(root / "beta.md", "# Beta\n");

    omanotes::MainWindow window(
        {std::filesystem::canonical(root), std::filesystem::canonical(root / "alpha.md"), false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(tree != nullptr);
    QVERIFY(buffers != nullptr);

    // A requested file opens alone: no empty scratch tab tags along.
    QCOMPARE(buffers->count(), 1);
    QCOMPARE(buffers->tabText(0), QStringLiteral("alpha.md"));

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    const auto beta = findIndex(*model, QStringLiteral("beta.md"));
    QVERIFY(beta.isValid());
    tree->scrollTo(beta);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(beta).center());

    QTRY_COMPARE(buffers->count(), 2);
    QCOMPARE(buffers->currentIndex(), 1);
    QTRY_VERIFY(activeEditor(window) != nullptr &&
                activeEditor(window)->document()->text() == QStringLiteral("# Beta\n"));

    auto* editor = activeEditor(window);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);

    QKeyEvent previousBuffer(QEvent::KeyPress, Qt::Key_H, Qt::ShiftModifier, QStringLiteral("H"));
    QApplication::sendEvent(eventTarget, &previousBuffer);
    QTRY_COMPARE(buffers->currentIndex(), 0);
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("# Alpha\n"));

    eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);
    QKeyEvent nextBuffer(QEvent::KeyPress, Qt::Key_L, Qt::ShiftModifier, QStringLiteral("L"));
    QApplication::sendEvent(eventTarget, &nextBuffer);
    QTRY_COMPARE(buffers->currentIndex(), 1);
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("# Beta\n"));
}

void MainWindowTest::reopeningAnOpenFileKeepsUnsavedEditsAndDoesNotDuplicate() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "note.md", "# Original\n");

    omanotes::MainWindow window(
        {std::filesystem::canonical(root), std::filesystem::canonical(root / "note.md"), false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(tree != nullptr);
    QVERIFY(buffers != nullptr);
    QCOMPARE(buffers->count(), 1);

    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("unsaved work"));
    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("note.md [+]"));

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    const auto note = findIndex(*model, QStringLiteral("note.md"));
    QVERIFY(note.isValid());
    tree->scrollTo(note);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(note).center());

    // Re-opening activates the existing buffer; it must not add a tab and must
    // not reload the file over work the user has not saved.
    QCOMPARE(buffers->count(), 1);
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("unsaved work"));
}

void MainWindowTest::switchesBufferWhenTheStripIsClicked() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "alpha.md", "# Alpha\n");

    omanotes::MainWindow window({std::filesystem::canonical(root), std::nullopt, false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(tree != nullptr);
    QVERIFY(buffers != nullptr);

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    const auto alpha = findIndex(*model, QStringLiteral("alpha.md"));
    QVERIFY(alpha.isValid());
    tree->scrollTo(alpha);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(alpha).center());
    QTRY_COMPARE(buffers->count(), 2);

    QTest::mouseClick(buffers, Qt::LeftButton, Qt::NoModifier, buffers->tabRect(0).center());

    QTRY_COMPARE(buffers->currentIndex(), 0);
    QVERIFY(activeEditor(window) != nullptr);
    QCOMPARE(activeEditor(window)->document()->text(), QString{});
}

void MainWindowTest::leavesShiftMotionsToTheEditorWithASingleBuffer() {
    omanotes::MainWindow window(launchRequest());
    window.show();
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("one\ntwo\nthree\n"));
    editor->setCursorPosition(KTextEditor::Cursor(2, 0));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);

    // With one buffer there is nowhere to switch to, so the key must reach the
    // editor rather than being swallowed by the buffer switcher.
    QKeyEvent topOfView(QEvent::KeyPress, Qt::Key_H, Qt::ShiftModifier, QStringLiteral("H"));
    QApplication::sendEvent(eventTarget, &topOfView);
    QTRY_COMPARE(editor->cursorPosition().line(), 0);
}

void MainWindowTest::savesAnOpenFileWithControlS() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window(
        {std::filesystem::canonical(root), std::filesystem::canonical(note), false});
    window.show();
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(buffers != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("# Edited\n"));
    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("note.md [+]"));

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);

    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("note.md"));
    QVERIFY(status->text().contains(QStringLiteral("Wrote note.md")));
    QVERIFY(!editor->document()->isModified());

    QFile written(QString::fromStdString(note.string()));
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("# Edited\n"));
}

void MainWindowTest::namesAScratchBufferBeforeWritingIt() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* prompt = window.findChild<QLineEdit*>(QStringLiteral("namePrompt"));
    auto* editor = activeEditor(window);
    QVERIFY(buffers != nullptr);
    QVERIFY(prompt != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(!prompt->isVisible());
    QCOMPARE(buffers->tabText(0), QStringLiteral("[No Name]"));

    editor->document()->setText(QStringLiteral("# Fresh\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);

    // Nothing may reach the disk until the buffer has been named.
    QTRY_VERIFY(prompt->isVisible());
    QCOMPARE(std::distance(std::filesystem::directory_iterator(root),
                           std::filesystem::directory_iterator{}),
             std::ptrdiff_t{0});

    QTest::keyClicks(prompt, QStringLiteral("idea.md"));
    QTest::keyClick(prompt, Qt::Key_Return);

    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("idea.md"));
    QVERIFY(!prompt->isVisible());
    QFile written(QString::fromStdString((root / "idea.md").string()));
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("# Fresh\n"));
}

void MainWindowTest::cancelsScratchNamingWithoutWriting() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* prompt = window.findChild<QLineEdit*>(QStringLiteral("namePrompt"));
    auto* editor = activeEditor(window);
    QVERIFY(prompt != nullptr);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("# Fresh\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);
    QTRY_VERIFY(prompt->isVisible());

    QTest::keyClicks(prompt, QStringLiteral("unwanted.md"));
    QTest::keyClick(prompt, Qt::Key_Escape);

    QTRY_VERIFY(!prompt->isVisible());
    QVERIFY(!std::filesystem::exists(root / "unwanted.md"));
    QCOMPARE(std::distance(std::filesystem::directory_iterator(root),
                           std::filesystem::directory_iterator{}),
             std::ptrdiff_t{0});
    QTRY_VERIFY(editor->hasFocus());
}

void MainWindowTest::savesThroughTheEditorWriteCommand() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* editor = activeEditor(window);
    QVERIFY(buffers != nullptr);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("# Written by command\n"));

    // `:w` must be the application's save, not any editor-internal write.
    auto* command = KTextEditor::Editor::instance()->queryCommand(QStringLiteral("w"));
    QVERIFY(command != nullptr);

    QString message;
    QVERIFY2(command->exec(editor, QStringLiteral("w typed.md"), message), qPrintable(message));

    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("typed.md"));
    QFile written(QString::fromStdString((root / "typed.md").string()));
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("# Written by command\n"));

    // A bare `:w` on an already-named buffer writes it again.
    editor->document()->setText(QStringLiteral("# Second write\n"));
    QVERIFY2(command->exec(editor, QStringLiteral("w"), message), qPrintable(message));
    QFile again(QString::fromStdString((root / "typed.md").string()));
    QVERIFY(again.open(QIODevice::ReadOnly));
    QCOMPARE(again.readAll(), QByteArray("# Second write\n"));
}

void MainWindowTest::savesWhenWriteIsTypedOnTheViCommandLine() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* editor = activeEditor(window);
    QVERIFY(buffers != nullptr);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("# Typed colon w\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    // Drive the editor's own command line exactly as a Vim user would. The
    // colon moves focus to the command line, so the rest must be typed there.
    auto* editorTarget = QApplication::focusWidget();
    QVERIFY(editorTarget != nullptr);
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                QApplication::focusWidget() != editorTarget);

    auto* commandLine = QApplication::focusWidget();
    QTest::keyClicks(commandLine, QStringLiteral("w colon.md"));
    // The space in the command must survive the application leader.
    QCOMPARE(qobject_cast<QLineEdit*>(commandLine)->text(), QStringLiteral("w colon.md"));
    QTest::keyClick(commandLine, Qt::Key_Return);

    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("colon.md"));
    QFile written(QString::fromStdString((root / "colon.md").string()));
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("# Typed colon w\n"));
}

void MainWindowTest::refusesEditorWriteCommandsItDoesNotImplementYet() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("# Text\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    // :wq must never reach KTextEditor's own save, which would open a modal
    // dialog and write outside the workspace.
    auto* editorTarget = QApplication::focusWidget();
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                QApplication::focusWidget() != editorTarget);
    auto* commandLine = QApplication::focusWidget();
    QTest::keyClicks(commandLine, QStringLiteral("wq"));
    QTest::keyClick(commandLine, Qt::Key_Return);

    QTRY_VERIFY(status->text().contains(QStringLiteral("not available yet")));
    QCOMPARE(std::distance(std::filesystem::directory_iterator(root),
                           std::filesystem::directory_iterator{}),
             std::ptrdiff_t{0});
}

void MainWindowTest::doesNotLetNormalModeWriteShortcutsReachTheEditorsWriter() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("# Text\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    // ZZ is Vim's write-and-quit. If KTextEditor implements it, it reaches its
    // own writer without passing the command bar, so this pins the behaviour.
    auto* target = QApplication::focusWidget();
    QKeyEvent firstZ(QEvent::KeyPress, Qt::Key_Z, Qt::ShiftModifier, QStringLiteral("Z"));
    QApplication::sendEvent(target, &firstZ);
    QKeyEvent secondZ(QEvent::KeyPress, Qt::Key_Z, Qt::ShiftModifier, QStringLiteral("Z"));
    QApplication::sendEvent(target, &secondZ);

    QCOMPARE(std::distance(std::filesystem::directory_iterator(root),
                           std::filesystem::directory_iterator{}),
             std::ptrdiff_t{0});
}

void MainWindowTest::showsANewlySavedNoteInTheSidebar() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    std::filesystem::create_directory(root / "projects");
    writeFile(root / "existing.md", "# Existing\n");

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* editor = activeEditor(window);
    QVERIFY(tree != nullptr);
    QVERIFY(editor != nullptr);

    auto* model = static_cast<omanotes::FileTreeModel*>(tree->model());
    if (model->canFetchMore({})) {
        model->fetchMore({});
    }
    QVERIFY(!findIndex(*model, QStringLiteral("fresh.md")).isValid());

    editor->document()->setText(QStringLiteral("# Fresh\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* editorTarget = QApplication::focusWidget();
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                QApplication::focusWidget() != editorTarget);
    auto* commandLine = QApplication::focusWidget();
    QTest::keyClicks(commandLine, QStringLiteral("w fresh.md"));
    QTest::keyClick(commandLine, Qt::Key_Return);

    // The note must appear in the sidebar straight away, in sorted position,
    // without relaunching.
    QTRY_VERIFY(findIndex(*model, QStringLiteral("fresh.md")).isValid());
    QCOMPARE(model->rowCount({}), 3);
    QCOMPARE(model->index(0, 0, {}).data().toString(), QStringLiteral("projects"));
    QCOMPARE(model->index(1, 0, {}).data().toString(), QStringLiteral("existing.md"));
    QCOMPARE(model->index(2, 0, {}).data().toString(), QStringLiteral("fresh.md"));

    // Saving the same file again must not add it twice.
    editor->document()->setText(QStringLiteral("# Fresh again\n"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Colon);
    QTRY_VERIFY(qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("w"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QTest::qWait(50);
    QCOMPARE(model->rowCount({}), 3);
}

void MainWindowTest::editorReceivesInitialFocus() {
    omanotes::MainWindow window(launchRequest());
    window.show();

    auto* editor = activeEditor(window);
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
