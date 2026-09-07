#include "app/launch_request.hpp"
#include "ui/main_window.hpp"
#include "workspace/file_tree_model.hpp"

#include <KTextEditor/Command>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAbstractButton>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

#include <filesystem>
#include <utility>

namespace {

class ScopedKeymap final {
  public:
    explicit ScopedKeymap(const QByteArray& json)
        : previous_(qgetenv("XDG_CONFIG_HOME")),
          hadPrevious_(qEnvironmentVariableIsSet("XDG_CONFIG_HOME")) {
        qputenv("XDG_CONFIG_HOME", directory_.path().toUtf8());
        QDir().mkpath(directory_.path() + QStringLiteral("/omanotes"));
        QFile file(directory_.path() + QStringLiteral("/omanotes/keymap.json"));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(json);
        }
    }
    ~ScopedKeymap() {
        if (hadPrevious_) {
            qputenv("XDG_CONFIG_HOME", previous_);
        } else {
            qunsetenv("XDG_CONFIG_HOME");
        }
    }

  private:
    QTemporaryDir directory_;
    QByteArray previous_;
    bool hadPrevious_;
};

omanotes::LaunchRequest launchRequest() {
    return {std::filesystem::canonical(std::filesystem::current_path()), std::nullopt, false};
}

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

QAbstractButton* closeButtonFor(QTabBar& strip, int index) {
    auto* button = strip.tabButton(index, QTabBar::RightSide);
    if (button == nullptr) {
        button = strip.tabButton(index, QTabBar::LeftSide);
    }
    return qobject_cast<QAbstractButton*>(button);
}

KTextEditor::View* activeEditor(omanotes::MainWindow& window) {
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("editorStack"));
    return stack == nullptr ? nullptr : qobject_cast<KTextEditor::View*>(stack->currentWidget());
}

QByteArray readFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

/// Save the way Neovim does by default: write a sibling, rename it over.
void replaceFileByRename(const std::filesystem::path& path, const QByteArray& contents) {
    auto staging = path;
    staging += ".staging";
    writeFile(staging, contents);
    std::filesystem::rename(staging, path);
}

/// Type `:command<Return>` on the editor's own Vi command line.
void typeViCommand(KTextEditor::View& editor, const QString& command) {
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    auto* editorTarget = QApplication::focusWidget();
    QVERIFY(editorTarget != nullptr);
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                QApplication::focusWidget() != editorTarget);
    auto* commandLine = QApplication::focusWidget();
    QTest::keyClicks(commandLine, command);
    QTest::keyClick(commandLine, Qt::Key_Return);
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
    void reloadsACleanBufferWhenItsFileChangesOnDisk();
    void keepsEditsAndRefusesPlainWriteWhenFileChangedUnderneath();
    void refusesToOverwriteExternalChangesEvenBeforeTheWatcherNotices();
    void reloadsOverUnsavedEditsOnlyWithBang();
    void reportsADeletedFileAndWritesItAgainOnSave();
    void refusesToOverwriteAnotherExistingFileWithoutBang();
    void leavesTheWroteConfirmationStandingAfterItsOwnSave();
    void interceptsWriteWhenTheCommandCompletionPopupHasFocus();
    void everyCommandHasOneImplementationAndARoute();
    void leaderSequencesRunRegisteredCommands();
    void closesBuffersFromTheLeaderAndGuardsUnsavedWork();
    void refusesDisabledCommandsWithAReason();
    void clicksAndShortcutsRunTheSameCommands();
    void mouseCreatesAndClosesBuffers();
    void readingViewTogglesAndPreservesState();
    void searchOpensMatchesAndHelpRunsCommands();
    void helpIsReachableFromSidebar();
    void configuredKeysRouteAndAppearInHelp();
    void malformedKeymapKeepsDefaultsAndShowsLocation();
    void remappedPaneKeyReplacesTheOldRoute();
    void leaderOverridesWinOverDirectShiftKeys();
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
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
    QTRY_VERIFY(help != nullptr && help->isVisible());
    help->reject();
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
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("sidebar"));
    QVERIFY(splitter != nullptr);
    QVERIFY(sidebar != nullptr);
    QCOMPARE(splitter->count(), 2);
    // The sidebar starts hidden (ADR 0007), so the editor has the whole width.
    QCOMPARE(splitter->sizes().at(0), 0);
    QVERIFY(splitter->sizes().at(1) > 0);

    // Once shown, both regions keep a width through a resize.
    sidebar->show();
    window.resize(720, 480);
    QCoreApplication::processEvents();
    QVERIFY(splitter->sizes().at(0) > 0);
    QVERIFY(splitter->sizes().at(1) > 0);
}

void MainWindowTest::reloadsACleanBufferWhenItsFileChangesOnDisk() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));

    // An agent rewrites the note a few times in quick succession, the last
    // time by rename-replace.
    writeFile(note, "# Draft 1\n");
    writeFile(note, "# Draft 2\n");
    replaceFileByRename(note, "# Final from outside\n");

    QTRY_COMPARE(editor->document()->text(), QStringLiteral("# Final from outside\n"));
    QVERIFY(!editor->document()->isModified());
    QVERIFY2(status->text().contains(QStringLiteral("Reloaded note.md")),
             qPrintable(status->text()));
}

void MainWindowTest::keepsEditsAndRefusesPlainWriteWhenFileChangedUnderneath() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* buffers = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(buffers != nullptr);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("# Mine, unsaved\n"));
    QTRY_VERIFY(editor->document()->isModified());
    replaceFileByRename(note, "# Theirs\n");

    QTRY_VERIFY2(status->text().contains(QStringLiteral("changed on disk")),
                 qPrintable(status->text()));
    // Both versions survive: the buffer keeps the edits, the disk keeps theirs.
    QCOMPARE(editor->document()->text(), QStringLiteral("# Mine, unsaved\n"));
    QCOMPARE(readFile(note), QByteArray("# Theirs\n"));

    // A plain save is refused with directions, and the disk is untouched.
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);
    QVERIFY2(status->text().contains(QStringLiteral(":w! overwrites")), qPrintable(status->text()));
    QCOMPARE(readFile(note), QByteArray("# Theirs\n"));
    QVERIFY(editor->document()->isModified());

    // The status line keeps saying so after the message has been replaced.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("i"));
    QTRY_VERIFY(status->text().contains(QStringLiteral("INSERT"), Qt::CaseInsensitive));
    QVERIFY2(status->text().contains(QStringLiteral("[changed on disk]")),
             qPrintable(status->text()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);

    // An explicit :w! is the deliberate overwrite.
    typeViCommand(*editor, QStringLiteral("w!"));
    QTRY_COMPARE(readFile(note), QByteArray("# Mine, unsaved\n"));
    QTRY_COMPARE(buffers->tabText(0), QStringLiteral("note.md"));
    QVERIFY(!status->text().contains(QStringLiteral("changed on disk")));
}

void MainWindowTest::refusesToOverwriteExternalChangesEvenBeforeTheWatcherNotices() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    // Change the disk and save in the same breath, before the event loop can
    // deliver a watcher report: the save itself must notice.
    editor->document()->setText(QStringLiteral("# Mine\n"));
    writeFile(note, "# Theirs, seconds ago\n");
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);

    QVERIFY2(status->text().contains(QStringLiteral("changed on disk since it was read")),
             qPrintable(status->text()));
    QCOMPARE(readFile(note), QByteArray("# Theirs, seconds ago\n"));
}

void MainWindowTest::reloadsOverUnsavedEditsOnlyWithBang() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("# Unsaved\n"));
    QTRY_VERIFY(editor->document()->isModified());

    typeViCommand(*editor, QStringLiteral("e"));
    QTRY_VERIFY2(status->text().contains(QStringLiteral("No write since last change")),
                 qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Unsaved\n"));

    typeViCommand(*editor, QStringLiteral("e!"));
    QTRY_COMPARE(editor->document()->text(), QStringLiteral("# Original\n"));
    QVERIFY(!editor->document()->isModified());
    QVERIFY2(status->text().contains(QStringLiteral("Reloaded note.md")),
             qPrintable(status->text()));
}

void MainWindowTest::reportsADeletedFileAndWritesItAgainOnSave() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Only copy\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);

    QVERIFY(std::filesystem::remove(note));
    QTRY_VERIFY2(status->text().contains(QStringLiteral("deleted on disk")),
                 qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Only copy\n"));

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("i"));
    QTRY_VERIFY2(status->text().contains(QStringLiteral("[deleted on disk]")),
                 qPrintable(status->text()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);

    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);
    QVERIFY2(status->text().contains(QStringLiteral("Wrote note.md")), qPrintable(status->text()));
    QCOMPARE(readFile(note), QByteArray("# Only copy\n"));
    QTRY_VERIFY(!status->text().contains(QStringLiteral("deleted")));
}

void MainWindowTest::refusesToOverwriteAnotherExistingFileWithoutBang() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto existing = root / "existing.md";
    writeFile(existing, "# Do not clobber\n");

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("# Scratch\n"));

    typeViCommand(*editor, QStringLiteral("w existing.md"));
    QTRY_VERIFY2(status->text().contains(QStringLiteral("File exists")),
                 qPrintable(status->text()));
    QCOMPARE(readFile(existing), QByteArray("# Do not clobber\n"));

    typeViCommand(*editor, QStringLiteral("w! existing.md"));
    QTRY_COMPARE(readFile(existing), QByteArray("# Scratch\n"));
}

void MainWindowTest::leavesTheWroteConfirmationStandingAfterItsOwnSave() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);

    editor->document()->setText(QStringLiteral("# Edited\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);
    QVERIFY(status->text().contains(QStringLiteral("Wrote note.md")));

    // The watcher sees our own write; that must not read as an external
    // change, reload anything, or replace the confirmation.
    QTest::qWait(400);
    QVERIFY2(status->text().contains(QStringLiteral("Wrote note.md")), qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Edited\n"));
    QVERIFY(!editor->document()->isModified());
}

void MainWindowTest::interceptsWriteWhenTheCommandCompletionPopupHasFocus() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "test_note_2.md";
    writeFile(note, "one\ntwo\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("one\ntwo\nthree\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    // A bare `:w` leaves the command bar's completion popup open (w, wa, wq,
    // ...), and on a real keyboard the Return lands on that popup, not on the
    // line edit. That is the route a Save As dialog escaped through.
    auto* editorTarget = QApplication::focusWidget();
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                QApplication::focusWidget() != editorTarget);
    auto* commandLine = QApplication::focusWidget();
    QTest::keyClicks(commandLine, QStringLiteral("w"));
    QTRY_VERIFY(QApplication::activePopupWidget() != nullptr);
    auto* popup = QApplication::activePopupWidget();
    QTest::keyClick(popup, Qt::Key_Return);

    QTRY_COMPARE(readFile(note), QByteArray("one\ntwo\nthree\n"));
    QVERIFY2(status->text().contains(QStringLiteral("Wrote test_note_2.md")),
             qPrintable(status->text()));
    QVERIFY(QApplication::activeModalWidget() == nullptr);
}

void MainWindowTest::everyCommandHasOneImplementationAndARoute() {
    omanotes::MainWindow window(launchRequest());
    const auto findings = window.auditCommands();
    QString report;
    for (const auto& finding : findings) {
        report += finding.commandId + QStringLiteral(": ") + finding.detail + QLatin1Char('\n');
    }
    QVERIFY2(findings.empty(), qPrintable(report));

    const auto& commands = window.commands();
    for (const auto* id :
         {"file.save", "file.open", "buffer.new", "buffer.close", "buffer.close.discard",
          "buffer.next", "buffer.previous", "buffer.show", "pane.sidebar", "pane.editor",
          "search.files", "search.text", "help.show", "view.reading"}) {
        QVERIFY2(commands.find(QString::fromLatin1(id)) != nullptr, id);
    }
    QCOMPARE(commands.commands().size(), std::size_t{15});
}

void MainWindowTest::leaderSequencesRunRegisteredCommands() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* strip = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr);
    QVERIFY(strip != nullptr);
    QVERIFY(editor != nullptr);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();

    // Space f n: a new scratch buffer, with the half-typed sequence reported.
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("f"));
    QTRY_COMPARE(status->text(), QStringLiteral("Space+f …"));
    QTest::keyClicks(target, QStringLiteral("n"));
    QTRY_COMPARE(strip->count(), 2);

    // Space b p / Space b n cycle, as Shift+H / Shift+L do.
    auto* second = activeEditor(window);
    QVERIFY(second != nullptr && second != editor);
    second->setFocus();
    QTRY_VERIFY(second->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bp"));
    QTRY_COMPARE(strip->currentIndex(), 0);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bn"));
    QTRY_COMPARE(strip->currentIndex(), 1);

    // The sidebar starts hidden. Space e shows it and moves focus there;
    // Space e again hides it and hands focus back, as LazyVim's explorer does.
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("sidebar"));
    QVERIFY(sidebar != nullptr);
    QVERIFY(!sidebar->isVisible());
    second->setFocus();
    QTRY_VERIFY(second->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("e"));
    QTRY_VERIFY(sidebar->isVisible());
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                sidebar->isAncestorOf(QApplication::focusWidget()));
    QVERIFY(QApplication::activeModalWidget() == nullptr);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(second->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("e"));
    QTRY_VERIFY(!sidebar->isVisible());
    QVERIFY(second->hasFocus());
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("e"));
    QTRY_VERIFY(sidebar->isVisible());
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                sidebar->isAncestorOf(QApplication::focusWidget()));

    // Ctrl+H reaches a hidden sidebar too: it shows it rather than doing nothing.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(second->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("e"));
    QTRY_VERIFY(!sidebar->isVisible());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_H, Qt::ControlModifier);
    QTRY_VERIFY(sidebar->isVisible());
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                sidebar->isAncestorOf(QApplication::focusWidget()));
}

void MainWindowTest::closesBuffersFromTheLeaderAndGuardsUnsavedWork() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "closing.md";
    writeFile(note, "keep me\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* strip = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("editorStack"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr && strip != nullptr && stack != nullptr && editor != nullptr);
    QCOMPARE(strip->count(), 1);
    // One editor per open buffer plus the permanent reading view.
    QCOMPARE(stack->count(), 2);

    editor->document()->setText(QStringLiteral("keep me\nand this\n"));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();

    // Dirty: Space b d refuses and names the way out.
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bd"));
    QTRY_VERIFY2(
        status->text().contains(QStringLiteral("No write since last change for closing.md")),
        qPrintable(status->text()));
    QVERIFY(status->text().contains(QStringLiteral("Space+b+D")));
    QCOMPARE(strip->count(), 1);
    QCOMPARE(strip->tabText(0), QStringLiteral("closing.md [+]"));

    // Space b D discards. The last buffer closing leaves a scratch buffer to
    // type in, with its own editor, and nothing reaches the disk.
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bD"));
    QTRY_COMPARE(status->text(), QStringLiteral("Closed closing.md, discarding changes"));
    QCOMPARE(strip->count(), 1);
    QCOMPARE(strip->tabText(0), QStringLiteral("[No Name]"));
    // One editor per open buffer plus the permanent reading view.
    QCOMPARE(stack->count(), 2);
    QCOMPARE(readFile(note), QByteArray("keep me\n"));
    auto* scratch = activeEditor(window);
    QVERIFY(scratch != nullptr && scratch != editor);
    QVERIFY(scratch->document()->text().isEmpty());

    // Clean: Space b d closes outright. With two buffers, the neighbour shows.
    scratch->document()->setText(QStringLiteral("scratch"));
    scratch->setFocus();
    QTRY_VERIFY(scratch->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("fn"));
    QTRY_COMPARE(strip->count(), 2);
    auto* third = activeEditor(window);
    QVERIFY(third != nullptr);
    third->setFocus();
    QTRY_VERIFY(third->hasFocus());
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bd"));
    QTRY_COMPARE(status->text(), QStringLiteral("Closed [No Name]"));
    QCOMPARE(strip->count(), 1);
    // One editor per open buffer plus the permanent reading view.
    QCOMPARE(stack->count(), 2);
    QCOMPARE(activeEditor(window), scratch);
    QCOMPARE(scratch->document()->text(), QStringLiteral("scratch"));
    QVERIFY(scratch->hasFocus());
}

void MainWindowTest::refusesDisabledCommandsWithAReason() {
    omanotes::MainWindow window(launchRequest());
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr && editor != nullptr);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();

    // One buffer: cycling has nowhere to go, and says so rather than nothing.
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bn"));
    QTRY_COMPARE(status->text(), QStringLiteral("Only one buffer is open"));

    // A pending prefix in Insert mode is cancelled, never executed.
    QTest::keyClicks(target, QStringLiteral("i"));
    QTRY_VERIFY(status->text().contains(QStringLiteral("INSERT"), Qt::CaseInsensitive));
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bD"));
    QCOMPARE(editor->document()->text(), QStringLiteral(" bD"));
}

void MainWindowTest::clicksAndShortcutsRunTheSameCommands() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto first = root / "first.md";
    const auto second = root / "second.md";
    writeFile(first, "first\n");
    writeFile(second, "second\n");
    omanotes::MainWindow window({root, first, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* strip = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("sidebar"));
    auto* tree = window.findChild<QTreeView*>();
    QVERIFY(status != nullptr && strip != nullptr && sidebar != nullptr && tree != nullptr);
    QVERIFY(!sidebar->isVisible());
    sidebar->show();
    QTRY_VERIFY(tree->isVisible());

    // Opening from the tree runs file.open.
    auto* model = tree->model();
    QVERIFY(model != nullptr);
    QTRY_VERIFY(model->rowCount() >= 2);
    QModelIndex secondIndex;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data().toString() == QStringLiteral("second.md")) {
            secondIndex = index;
        }
    }
    QVERIFY(secondIndex.isValid());
    tree->setCurrentIndex(secondIndex);
    const auto point = tree->visualRect(secondIndex).center();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(strip->count(), 2);
    QCOMPARE(strip->currentIndex(), 1);

    // Clicking a tab runs buffer.show.
    QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, strip->tabRect(0).center());
    QTRY_COMPARE(strip->currentIndex(), 0);
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("first\n"));

    // Shift+L and Shift+H run buffer.next and buffer.previous.
    auto* editor = activeEditor(window);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_L, Qt::ShiftModifier);
    QTRY_COMPARE(strip->currentIndex(), 1);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_H, Qt::ShiftModifier);
    QTRY_COMPARE(strip->currentIndex(), 0);

    // Ctrl+H runs pane.sidebar; Ctrl+L in the tree runs pane.editor.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_H, Qt::ControlModifier);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                sidebar->isAncestorOf(QApplication::focusWidget()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(activeEditor(window)->hasFocus());
}

void MainWindowTest::mouseCreatesAndClosesBuffers() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "note\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* strip = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* newBuffer = window.findChild<QToolButton*>(QStringLiteral("newBufferButton"));
    QVERIFY(status != nullptr && strip != nullptr && newBuffer != nullptr);
    QCOMPARE(strip->count(), 1);

    // The + button runs buffer.new: a scratch buffer opens and takes over.
    QTest::mouseClick(newBuffer, Qt::LeftButton);
    QTRY_COMPARE(strip->count(), 2);
    QCOMPARE(strip->currentIndex(), 1);
    QCOMPARE(strip->tabText(1), QStringLiteral("[No Name]"));
    QVERIFY(activeEditor(window)->document()->text().isEmpty());

    // A dirty buffer's close button asks first; Cancel keeps everything.
    activeEditor(window)->document()->setText(QStringLiteral("draft"));
    QTRY_COMPARE(strip->tabText(1), QStringLiteral("[No Name] [+]"));
    auto* scratchClose = closeButtonFor(*strip, 1);
    QVERIFY(scratchClose != nullptr);
    QTest::mouseClick(scratchClose, Qt::LeftButton);
    auto* prompt = window.findChild<QMessageBox*>(QStringLiteral("closeBufferPrompt"));
    QVERIFY(prompt != nullptr);
    QTRY_VERIFY(prompt->isVisible());
    QVERIFY(prompt->text().contains(QStringLiteral("[No Name]")));
    QTest::mouseClick(prompt->button(QMessageBox::Cancel), Qt::LeftButton);
    QTRY_VERIFY(!prompt->isVisible());
    QCOMPARE(strip->count(), 2);
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("draft"));

    // Save routes a scratch through the save-as prompt, then finishes the close.
    QTest::mouseClick(scratchClose, Qt::LeftButton);
    QTRY_VERIFY(prompt->isVisible());
    QTest::mouseClick(prompt->button(QMessageBox::Save), Qt::LeftButton);
    auto* namePrompt = window.findChild<QLineEdit*>(QStringLiteral("namePrompt"));
    QVERIFY(namePrompt != nullptr);
    QTRY_VERIFY(namePrompt->isVisible());
    QTest::keyClicks(namePrompt, QStringLiteral("kept.md"));
    QTest::keyClick(namePrompt, Qt::Key_Return);
    QTRY_COMPARE(strip->count(), 1);
    QVERIFY(readFile(root / "kept.md").startsWith("draft"));
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("note\n"));

    // Discard closes without touching the disk; the last buffer leaves a scratch.
    activeEditor(window)->document()->setText(QStringLiteral("note\nedited\n"));
    QTRY_COMPARE(strip->tabText(0), QStringLiteral("note.md [+]"));
    auto* noteClose = closeButtonFor(*strip, 0);
    QVERIFY(noteClose != nullptr);
    QTest::mouseClick(noteClose, Qt::LeftButton);
    QTRY_VERIFY(prompt->isVisible());
    QTest::mouseClick(prompt->button(QMessageBox::Discard), Qt::LeftButton);
    QTRY_COMPARE(status->text(), QStringLiteral("Closed note.md, discarding changes"));
    QCOMPARE(readFile(note), QByteArray("note\n"));
    QCOMPARE(strip->count(), 1);
    QCOMPARE(strip->tabText(0), QStringLiteral("[No Name]"));

    // A clean close needs no prompt and acts on the clicked tab, not the
    // active buffer.
    QTest::mouseClick(newBuffer, Qt::LeftButton);
    QTRY_COMPARE(strip->count(), 2);
    activeEditor(window)->document()->setText(QStringLiteral("second"));
    auto* firstClose = closeButtonFor(*strip, 0);
    QVERIFY(firstClose != nullptr);
    QTest::mouseClick(firstClose, Qt::LeftButton);
    QTRY_COMPARE(strip->count(), 1);
    QVERIFY(!prompt->isVisible());
    QCOMPARE(activeEditor(window)->document()->text(), QStringLiteral("second"));
}

void MainWindowTest::readingViewTogglesAndPreservesState() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Title\n\nbody text\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* strip = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("editorStack"));
    auto* reading = window.findChild<QTextBrowser*>(QStringLiteral("readingView"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr && strip != nullptr && stack != nullptr && reading != nullptr &&
            editor != nullptr);

    editor->setCursorPosition(KTextEditor::Cursor(2, 5));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    // Space m projects the note; the source and its state are untouched.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_COMPARE(stack->currentWidget(), reading);
    QVERIFY(reading->toPlainText().contains(QStringLiteral("body text")));
    QVERIFY(!editor->document()->isModified());
    QTRY_VERIFY2(status->text().contains(QStringLiteral("READING")), qPrintable(status->text()));

    // Space m from the reading view returns to writing, cursor intact.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_COMPARE(stack->currentWidget(), editor);
    QCOMPARE(activeEditor(window)->cursorPosition(), KTextEditor::Cursor(2, 5));
    QTRY_VERIFY(status->text().contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive));

    // The mode is per buffer: a reading note stays reading behind a new
    // scratch, which opens writing.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_COMPARE(stack->currentWidget(), reading);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("fn"));
    QTRY_COMPARE(strip->count(), 2);
    QVERIFY(stack->currentWidget() != reading);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_H, Qt::ShiftModifier);
    QTRY_COMPARE(stack->currentWidget(), reading);

    // Dirty text projects too, without ever cleaning or dirtying the buffer.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_VERIFY(activeEditor(window) != nullptr);
    auto* backToWriting = activeEditor(window);
    backToWriting->document()->setText(QStringLiteral("# Title\n\nedited body\n"));
    QTRY_VERIFY(backToWriting->document()->isModified());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_COMPARE(stack->currentWidget(), reading);
    QVERIFY(reading->toPlainText().contains(QStringLiteral("edited body")));
    QVERIFY(backToWriting->document()->isModified());
    QVERIFY2(status->text().contains(QStringLiteral("[+]")), qPrintable(status->text()));
}

void MainWindowTest::closesCleanly() {
    omanotes::MainWindow window(launchRequest());
    window.show();
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
}

void MainWindowTest::searchOpensMatchesAndHelpRunsCommands() {
    QTemporaryDir temporary;
    const auto root = pathFor(temporary.path());
    writeFile(root / "found.md", "first\nfind me here\n");
    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* editor = activeEditor(window);
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("/"));
    auto* palette = window.findChild<QDialog*>(QStringLiteral("searchPalette"));
    QTRY_VERIFY(palette->isVisible());
    auto* query = palette->findChild<QLineEdit*>(QStringLiteral("searchQuery"));
    QTest::keyClicks(query, QStringLiteral("find me"));
    auto* list = palette->findChild<QListWidget*>(QStringLiteral("searchResults"));
    QTRY_COMPARE(list->count(), 1);
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_COMPARE(activeEditor(window)->document()->text(), QStringLiteral("first\nfind me here\n"));
    QCOMPARE(activeEditor(window)->cursorPosition(), KTextEditor::Cursor(1, 0));
    auto* helpButton = window.findChild<QToolButton*>(QStringLiteral("helpButton"));
    QTest::mouseClick(helpButton, Qt::LeftButton);
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
    QTRY_VERIFY(help->isVisible());
    auto* commands = help->findChild<QTreeWidget*>(QStringLiteral("helpCommands"));
    QTreeWidgetItem* newBuffer = nullptr;
    for (int row = 0; row < commands->topLevelItemCount(); ++row) {
        auto* item = commands->topLevelItem(row);
        if (item->data(0, Qt::UserRole).toString() == QStringLiteral("buffer.new")) {
            newBuffer = item;
        }
    }
    QVERIFY(newBuffer != nullptr);
    commands->setCurrentItem(newBuffer);
    QTest::keyClick(commands, Qt::Key_Return);
    QTRY_VERIFY(!help->isVisible());
    QCOMPARE(activeEditor(window)->document()->text(), QString{});
}

void MainWindowTest::helpIsReachableFromSidebar() {
    omanotes::MainWindow window(launchRequest());
    window.show();
    auto* editor = activeEditor(window);
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("e"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    QTRY_VERIFY(tree->hasFocus());
    QTest::keyClick(tree, Qt::Key_Space);
    QTest::keyClicks(tree, QStringLiteral("?"));
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
    QTRY_VERIFY(help->isVisible());
    QTest::keyClick(help, Qt::Key_Escape);
    QTRY_VERIFY(!help->isVisible());
}

void MainWindowTest::configuredKeysRouteAndAppearInHelp() {
    ScopedKeymap configuration(
        R"({"leaderBindings":{"buffer.new":["n"]},"shortcuts":{"file.save":"Ctrl+Alt+Shift+S"}})");
    QTemporaryDir workspace;
    const auto root = pathFor(workspace.path());
    writeFile(root / "note.md", "original");
    omanotes::MainWindow window({root, root / "note.md", false});
    window.show();
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("keymapWarning")) == nullptr);
    auto* editor = activeEditor(window);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();
    QTest::keyClicks(target, QStringLiteral("ilocal "));
    QVERIFY(editor->document()->isModified());
    QTest::keyClick(target, Qt::Key_S, Qt::ControlModifier);
    QCOMPARE(readFile(root / "note.md"), QByteArray("original"));
    QTest::keyClick(target, Qt::Key_S, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    QTRY_COMPARE(readFile(root / "note.md"), QByteArray("local original"));
    QVERIFY(!editor->document()->isModified());
    QTest::keyClick(target, Qt::Key_Escape);
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("n"));
    QCOMPARE(window.findChild<QTabBar*>(QStringLiteral("bufferStrip"))->count(), 2);
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("?"));
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
    QTRY_VERIFY(help->isVisible());
    auto* commands = help->findChild<QTreeWidget*>(QStringLiteral("helpCommands"));
    bool sawSave = false;
    bool sawNew = false;
    for (int row = 0; row < commands->topLevelItemCount(); ++row) {
        const auto* item = commands->topLevelItem(row);
        const auto id = item->data(0, Qt::UserRole).toString();
        if (id == QStringLiteral("file.save")) {
            sawSave = true;
            QCOMPARE(item->text(2), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+S"))
                                        .toString(QKeySequence::NativeText));
        }
        if (id == QStringLiteral("buffer.new")) {
            sawNew = true;
            QCOMPARE(item->text(2), QStringLiteral("Space n"));
        }
    }
    QVERIFY(sawSave && sawNew);
    help->reject();
}

void MainWindowTest::malformedKeymapKeepsDefaultsAndShowsLocation() {
    for (const auto& json :
         {QByteArray("{ broken"),
          QByteArray(
              R"({"leaderBindings":{"buffer.new":["n"]},"shortcuts":{"file.save":"Ctrl+B"}})")}) {
        ScopedKeymap configuration(json);
        omanotes::MainWindow window(launchRequest());
        window.show();
        auto* warning = window.findChild<QLabel*>(QStringLiteral("keymapWarning"));
        QVERIFY(warning != nullptr);
        QVERIFY(warning->text().contains(QStringLiteral("keymap.json")));
        QVERIFY(warning->text().contains(QStringLiteral("Default keys")));
        QCOMPARE(window.commands().lookup(QStringLiteral("f n")).commandId,
                 QStringLiteral("buffer.new"));
        QCOMPARE(window.commands().lookup(QStringLiteral("n")).match,
                 omanotes::SequenceMatch::None);
        auto* editor = activeEditor(window);
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        auto* target = QApplication::focusWidget();
        QTest::keyClick(target, Qt::Key_Space);
        QTest::keyClicks(target, QStringLiteral("?"));
        auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
        QTRY_VERIFY(help->isVisible());
        help->reject();
    }
}

void MainWindowTest::remappedPaneKeyReplacesTheOldRoute() {
    ScopedKeymap configuration(R"({"shortcuts":{"pane.editor":"Ctrl+Alt+Shift+L"}})");
    omanotes::MainWindow window(launchRequest());
    window.show();
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("keymapWarning")) == nullptr);
    auto* editor = activeEditor(window);
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_H, Qt::ControlModifier);
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    QTRY_VERIFY(tree->hasFocus());
    QTest::keyClick(tree, Qt::Key_L, Qt::ControlModifier);
    QVERIFY(tree->hasFocus());
    QTest::keyClick(tree, Qt::Key_L, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    QTRY_VERIFY(editor->hasFocus());
}

void MainWindowTest::leaderOverridesWinOverDirectShiftKeys() {
    ScopedKeymap configuration(R"({"leaderBindings":{"help.show":["?","H"]}})");
    omanotes::MainWindow window(launchRequest());
    window.show();
    auto* editor = activeEditor(window);
    QTRY_VERIFY(editor->hasFocus());
    auto* target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("fn"));
    target = QApplication::focusWidget();
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("H"));
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
    QTRY_VERIFY(help->isVisible());
    help->reject();
}

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
