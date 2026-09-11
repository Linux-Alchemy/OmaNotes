#include "app/launch_request.hpp"
#include "persistence/note_reader.hpp"
#include "ui/main_window.hpp"
#include "workspace/file_tree_model.hpp"

#include <KTextEditor/Command>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAbstractButton>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QtTest>

#include <filesystem>
#include <map>
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
    editor.window()->activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    // Type at the view's own input widget (its focus proxy), not at whatever
    // QApplication::focusWidget() says: after a modal dialog the offscreen
    // platform can leave that pointing at the dialog's button.
    auto* editorTarget = editor.focusProxy() != nullptr ? editor.focusProxy() : &editor;
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    // The command line is a line edit inside the editor; insist on it, so a
    // stale focus widget (a dismissed dialog's button, say) is never typed at.
    QTRY_VERIFY(qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr &&
                editor.isAncestorOf(QApplication::focusWidget()));
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
    void refusesToOpenAnOversizedNote();
    void showsUntrustedNamesAsPlainText();
    void keepsTheBufferWhenItsNoteIsSwappedForASymlink();
    void keepsTheBufferWhenItsNoteGrowsPastTheLimit();
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
    void mouseTogglesTheSidebar();
    void insertModePasteRoutesTheClipboard();
    void superChordsRouteUniversalCopyAndPaste();
    void readingViewTogglesAndPreservesState();
    void readingViewRoutesCopyAndRefusesPaste();
    void themeDressesEveryRegion();
    void focusMovesTheAccentMarkBetweenPanes();
    void themeFollowsALiveThemeSwitch();
    void searchOpensMatchesAndHelpRunsCommands();
    void helpIsReachableFromSidebar();
    void configuredKeysRouteAndAppearInHelp();
    void malformedKeymapKeepsDefaultsAndShowsLocation();
    void remappedPaneKeyReplacesTheOldRoute();
    void leaderOverridesWinOverDirectShiftKeys();
    void closesCleanly();
    void sidebarSelectionBarFollowsFocus();
    void colonQuitsWithPromptForUnsavedWork();
    void colonQuitVariantsSaveOrDiscard();
    void scrollbarsAreNeverShown();
    void halfPageKeysScrollWritingAndReading();
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
    // Mode only, sentence case, no editor prefix: "Normal", not "VI: NORMAL".
    QCOMPARE(status->text(), QStringLiteral("Normal"));

    editor->document()->setText(QStringLiteral("scratch"));
    QTRY_VERIFY(editor->document()->isModified());
    // The status line names neither the buffer nor its unsaved state.
    QVERIFY(!status->text().contains(QStringLiteral("[+]")));
    QVERIFY(!status->text().contains(QStringLiteral("Untitled")));

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* eventTarget = QApplication::focusWidget();
    QVERIFY(eventTarget != nullptr);
    QTest::keyClicks(eventTarget, QStringLiteral("i"));
    QTRY_COMPARE(status->text(), QStringLiteral("Insert"));
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
    QCOMPARE(buffers->tabText(0), QStringLiteral("Untitled"));
    QCOMPARE(buffers->tabText(1), QStringLiteral("note.md"));
    QCOMPARE(buffers->currentIndex(), 1);
    QVERIFY(!editor->document()->isModified());

    editor->document()->setText(QStringLiteral("changed in memory"));
    QTRY_VERIFY(editor->document()->isModified());
    QCOMPARE(buffers->tabText(1), QStringLiteral("note.md"));
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
    QTRY_VERIFY(editor->document()->isModified());
    QCOMPARE(buffers->tabText(0), QStringLiteral("note.md"));

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
    QTRY_VERIFY(editor->document()->isModified());
    QCOMPARE(buffers->tabText(0), QStringLiteral("note.md"));

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
    QCOMPARE(buffers->tabText(0), QStringLiteral("Untitled"));

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

    // :saveas must never reach KTextEditor's own save, which would open a
    // modal dialog and write outside the workspace. (:wq is ours now: it
    // writes through the atomic saver and quits.)
    auto* editorTarget = QApplication::focusWidget();
    QTest::keyClick(editorTarget, Qt::Key_Colon);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr &&
                QApplication::focusWidget() != editorTarget);
    auto* commandLine = QApplication::focusWidget();
    QTest::keyClicks(commandLine, QStringLiteral("saveas"));
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

void MainWindowTest::showsUntrustedNamesAsPlainText() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto tricky = root / "<img src=x><b>bold.md";
    writeFile(tricky, "# Tricky\n");

    omanotes::MainWindow window({root, tricky, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* heading = window.findChild<QLabel*>(QStringLiteral("sidebarHeading"));
    auto* searchStatus = window.findChild<QLabel*>(QStringLiteral("searchStatus"));
    QVERIFY(status != nullptr && heading != nullptr && searchStatus != nullptr);

    // Every label that shows a file name, a directory name, a link target or
    // a path is plain text: markup in a name is displayed, never rendered,
    // and never made to load an image (docs/threat-model.md, F-3).
    QCOMPARE(status->textFormat(), Qt::PlainText);
    QCOMPARE(heading->textFormat(), Qt::PlainText);
    QCOMPARE(searchStatus->textFormat(), Qt::PlainText);

    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    editor->document()->setText(QStringLiteral("edit"));
    QTRY_VERIFY(editor->document()->isModified());
    auto* strip = window.findChild<QTabBar*>(QStringLiteral("bufferStrip"));
    QVERIFY(strip != nullptr);
    auto* close = closeButtonFor(*strip, strip->currentIndex());
    QVERIFY(close != nullptr);
    QTest::mouseClick(close, Qt::LeftButton);
    auto* prompt = window.findChild<QMessageBox*>(QStringLiteral("closeBufferPrompt"));
    QVERIFY(prompt != nullptr);
    QTRY_VERIFY(prompt->isVisible());
    QCOMPARE(prompt->textFormat(), Qt::PlainText);
    QVERIFY(prompt->text().contains(QStringLiteral("<img src=x><b>bold")));
    QTest::mouseClick(prompt->button(QMessageBox::Cancel), Qt::LeftButton);
    QTRY_VERIFY(!prompt->isVisible());
}

void MainWindowTest::refusesToOpenAnOversizedNote() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto big = root / "big.md";
    writeFile(big, QByteArray(static_cast<qsizetype>(omanotes::kNoteMaxBytes) + 1, 'x'));

    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    QVERIFY(status != nullptr);
    const auto before = window.buffers().buffers().size();

    // Too big to read is refused before any buffer exists for it: no empty
    // tab, no partial text, and the reason names the file and the limit.
    window.focusRequestedFile(big);
    QVERIFY2(status->text().contains(QStringLiteral("big.md is larger than 16 MiB")),
             qPrintable(status->text()));
    QCOMPARE(window.buffers().buffers().size(), before);
}

void MainWindowTest::keepsTheBufferWhenItsNoteIsSwappedForASymlink() {
    QTemporaryDir temporary;
    QTemporaryDir elsewhere;
    QVERIFY(temporary.isValid() && elsewhere.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");
    const auto secret = std::filesystem::canonical(pathFor(elsewhere.path())) / "secret.md";
    writeFile(secret, "# SECRET\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr && editor != nullptr);
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));

    // A writer inside the root replaces the open note with a link to a file
    // outside it. The watcher fires; the reload must not follow the link.
    std::filesystem::remove(note);
    std::filesystem::create_symlink(secret, note);

    QTRY_VERIFY2(status->text().contains(QStringLiteral("could not be read safely")),
                 qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));
    QVERIFY(!editor->document()->text().contains(QStringLiteral("SECRET")));

    // An explicit reload refuses too: re-validation sees the path now resolve
    // outside the root, and the buffer is left as it was.
    typeViCommand(*editor, QStringLiteral("e!"));
    QTRY_VERIFY2(status->text().contains(QStringLiteral("outside the workspace")) ||
                     status->text().contains(QStringLiteral("symlink")) ||
                     status->text().contains(QStringLiteral("resolves elsewhere")),
                 qPrintable(status->text()));
    QVERIFY(!status->text().contains(QStringLiteral("Reloaded")));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));

    // A dangling link is still a link: the reload names it as one rather
    // than reporting the note missing (Matt's gate finding, 2026-09-11).
    std::filesystem::remove(note);
    std::filesystem::create_symlink(root / "nowhere.md", note);
    typeViCommand(*editor, QStringLiteral("e!"));
    QTRY_VERIFY2(status->text().contains(QStringLiteral("note.md is a symlink now")),
                 qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));
}

void MainWindowTest::keepsTheBufferWhenItsNoteGrowsPastTheLimit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Original\n");

    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr && editor != nullptr);

    // The note balloons past the limit on disk; the clean buffer is kept and
    // nothing of that size is read into memory.
    replaceFileByRename(note, QByteArray(static_cast<qsizetype>(omanotes::kNoteMaxBytes) + 1, 'x'));

    QTRY_VERIFY2(status->text().contains(QStringLiteral("could not be read safely")),
                 qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Original\n"));
    QVERIFY(!editor->document()->isModified());
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
    for (const auto* id : {"file.save",
                           "file.open",
                           "buffer.new",
                           "buffer.close",
                           "buffer.close.discard",
                           "buffer.next",
                           "buffer.previous",
                           "buffer.show",
                           "pane.sidebar",
                           "pane.editor",
                           "search.files",
                           "search.text",
                           "help.show",
                           "view.reading",
                           "edit.paste",
                           "edit.copy",
                           "editor.visual-block",
                           "view.half-page-down",
                           "view.half-page-up",
                           "app.quit"}) {
        QVERIFY2(commands.find(QString::fromLatin1(id)) != nullptr, id);
    }
    QCOMPARE(commands.commands().size(), std::size_t{21});
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
    QCOMPARE(strip->tabText(0), QStringLiteral("closing.md"));

    // Space b D discards. The last buffer closing leaves a scratch buffer to
    // type in, with its own editor, and nothing reaches the disk.
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("bD"));
    QTRY_COMPARE(status->text(), QStringLiteral("Closed closing.md, discarding changes"));
    QCOMPARE(strip->count(), 1);
    QCOMPARE(strip->tabText(0), QStringLiteral("Untitled"));
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
    QTRY_COMPARE(status->text(), QStringLiteral("Closed Untitled"));
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
    QCOMPARE(strip->tabText(1), QStringLiteral("Untitled"));
    QVERIFY(activeEditor(window)->document()->text().isEmpty());

    // A dirty buffer's close button asks first; Cancel keeps everything.
    activeEditor(window)->document()->setText(QStringLiteral("draft"));
    QTRY_VERIFY(activeEditor(window)->document()->isModified());
    QCOMPARE(strip->tabText(1), QStringLiteral("Untitled"));
    auto* scratchClose = closeButtonFor(*strip, 1);
    QVERIFY(scratchClose != nullptr);
    QTest::mouseClick(scratchClose, Qt::LeftButton);
    auto* prompt = window.findChild<QMessageBox*>(QStringLiteral("closeBufferPrompt"));
    QVERIFY(prompt != nullptr);
    QTRY_VERIFY(prompt->isVisible());
    QVERIFY(prompt->text().contains(QStringLiteral("Untitled")));
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
    QTRY_VERIFY(activeEditor(window)->document()->isModified());
    QCOMPARE(strip->tabText(0), QStringLiteral("note.md"));
    auto* noteClose = closeButtonFor(*strip, 0);
    QVERIFY(noteClose != nullptr);
    QTest::mouseClick(noteClose, Qt::LeftButton);
    QTRY_VERIFY(prompt->isVisible());
    QTest::mouseClick(prompt->button(QMessageBox::Discard), Qt::LeftButton);
    QTRY_COMPARE(status->text(), QStringLiteral("Closed note.md, discarding changes"));
    QCOMPARE(readFile(note), QByteArray("note\n"));
    QCOMPARE(strip->count(), 1);
    QCOMPARE(strip->tabText(0), QStringLiteral("Untitled"));

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

void MainWindowTest::mouseTogglesTheSidebar() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    writeFile(root / "note.md", "# Note\n");
    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("sidebar"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    auto* toggle = window.findChild<QToolButton*>(QStringLiteral("sidebarToggleButton"));
    auto* editor = activeEditor(window);
    QVERIFY(sidebar != nullptr && tree != nullptr && toggle != nullptr && editor != nullptr);
    QTRY_VERIFY(editor->hasFocus());
    QVERIFY(!sidebar->isVisible());
    QCOMPARE(toggle->focusPolicy(), Qt::NoFocus);
    QVERIFY(toggle->toolTip().contains(QStringLiteral("Space e")));
    // The glyph points the way the tree will go: » while it is away.
    QCOMPARE(toggle->text(), QStringLiteral("\u00bb"));

    // The glyph is the mouse route to pane.sidebar.toggle: one click shows the
    // tree and moves focus into it, exactly as Space e does; the glyph turns
    // round to « and the tooltip says "Hide".
    QTest::mouseClick(toggle, Qt::LeftButton);
    QTRY_VERIFY(sidebar->isVisible());
    QTRY_VERIFY(tree->hasFocus());
    QCOMPARE(toggle->text(), QStringLiteral("\u00ab"));
    QVERIFY(toggle->toolTip().startsWith(QStringLiteral("Hide")));

    // A second click hides it and hands focus back to the text.
    QTest::mouseClick(toggle, Qt::LeftButton);
    QTRY_VERIFY(!sidebar->isVisible());
    QTRY_VERIFY(editor->hasFocus());
    QCOMPARE(toggle->text(), QStringLiteral("\u00bb"));
    QVERIFY(toggle->toolTip().startsWith(QStringLiteral("Show")));

    // The keyboard routes flip it too: Ctrl+H focuses the sidebar, showing it.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_H, Qt::ControlModifier);
    QTRY_VERIFY(tree->hasFocus());
    QCOMPARE(toggle->text(), QStringLiteral("\u00ab"));
    QTest::keyClick(tree, Qt::Key_Space);
    QTest::keyClick(tree, Qt::Key_E);
    QTRY_VERIFY(!sidebar->isVisible());
    QTRY_VERIFY(editor->hasFocus());
    QCOMPARE(toggle->text(), QStringLiteral("\u00bb"));

    // With the tree already open and focus in the editor, the click still
    // closes it and the editor keeps focus.
    QTest::mouseClick(toggle, Qt::LeftButton);
    QTRY_VERIFY(tree->hasFocus());
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::mouseClick(toggle, Qt::LeftButton);
    QTRY_VERIFY(!sidebar->isVisible());
    QVERIFY(editor->hasFocus());
}

void MainWindowTest::insertModePasteRoutesTheClipboard() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "start\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QApplication::clipboard()->setText(QStringLiteral("PASTED"));

    // Ctrl+V pastes in Normal mode: it is indistinguishable from Omarchy's
    // Super+V, and universal paste wins in every mode.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(editor->document()->text(), QStringLiteral("PASTEDstart\n"));

    // Visual block lives on Ctrl+Q, exactly as gvim resolves the clash.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Q, Qt::ControlModifier);
    QTRY_COMPARE(editor->viewMode(), KTextEditor::View::ViModeVisualBlock);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTRY_COMPARE(editor->viewMode(), KTextEditor::View::ViModeNormal);
    QCOMPARE(editor->document()->text(), QStringLiteral("PASTEDstart\n"));

    // Insert mode pastes too, and typing continues normally afterwards.
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("i"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(editor->document()->text().count(QStringLiteral("PASTED")), qsizetype{2});
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("x"));
    QTRY_VERIFY2(editor->document()->text().contains(QStringLiteral("PASTEDx")),
                 qPrintable(editor->document()->text()));
}

void MainWindowTest::superChordsRouteUniversalCopyAndPaste() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "stack overflow\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QApplication::clipboard()->setText(QStringLiteral("seed"));

    // Omarchy's Super+C arrives as Ctrl+Meta+C. Over a Visual selection it
    // copies; the held Super must not defeat the shortcut match.
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("vllll"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_C, Qt::ControlModifier | Qt::MetaModifier);
    QTRY_COMPARE(QApplication::clipboard()->text(), QStringLiteral("stack"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);

    // Without a selection the same chord stays Vi's abort: no copy.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_C, Qt::ControlModifier | Qt::MetaModifier);
    QTest::qWait(50);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("stack"));

    // Super+V (Ctrl+Meta+V) pastes what was copied, in Insert mode.
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("A"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_V, Qt::ControlModifier | Qt::MetaModifier);
    QTRY_COMPARE(editor->document()->text(), QStringLiteral("stack overflowstack\n"));

    // Super+V pastes in Normal mode too — the universal promise — while a
    // bare Ctrl+V there remains Vi's visual block and inserts nothing.
    // The chord pastes in Normal mode too, even if a lingering Meta from the
    // held Super ever does reach the app. Vi's Escape steps the cursor back
    // onto the final 'k', so the paste lands before it: deterministic, if
    // not pretty.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_V, Qt::ControlModifier | Qt::MetaModifier);
    QTRY_COMPARE(editor->document()->text(), QStringLiteral("stack overflowstacstackk\n"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
}

void MainWindowTest::scrollbarsAreNeverShown() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "long.md";
    QByteArray body("# Long\n\n");
    for (int line = 0; line < 400; ++line) {
        body += "a line of body text that goes on for a while\n";
    }
    writeFile(note, body);
    omanotes::MainWindow window({root, note, false});
    window.resize(600, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    auto* reading = window.findChild<QTextBrowser*>(QStringLiteral("readingView"));
    QVERIFY(reading != nullptr);
    auto* tree = window.findChild<QTreeView*>();
    QVERIFY(tree != nullptr);

    // A note far taller than the window would earn a scrollbar anywhere
    // else. Here the bars have no size, so nothing to grab, nothing to see.
    QTRY_VERIFY(editor->document()->lines() > 300);
    QVERIFY(editor->verticalScrollBar()->maximum() > 0);
    // Layout runs on a posted event, and a bar Qt never shows keeps a stale
    // default geometry, so "hidden or zero width" is the honest check.
    const auto unseen = [](const QScrollBar* bar) {
        return !bar->isVisible() || bar->width() == 0;
    };
    QTRY_VERIFY(unseen(editor->verticalScrollBar()));
    QTRY_VERIFY(unseen(editor->horizontalScrollBar()));

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_VERIFY(reading->isVisible());
    QTRY_VERIFY(reading->verticalScrollBar()->maximum() > 0);
    QTRY_VERIFY(unseen(reading->verticalScrollBar()));
    QTRY_VERIFY(unseen(tree->verticalScrollBar()));

    // Scrolling itself still works: the wheel moves the view without a bar.
    const auto before = reading->verticalScrollBar()->value();
    QWheelEvent wheel(QPointF(10, 10), reading->viewport()->mapToGlobal(QPoint(10, 10)),
                      QPoint(0, -120), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(reading->viewport(), &wheel);
    QTRY_VERIFY(reading->verticalScrollBar()->value() > before);
}

void MainWindowTest::halfPageKeysScrollWritingAndReading() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "long.md";
    QByteArray body("# Long\n\n");
    for (int line = 0; line < 400; ++line) {
        body += "a line of body text\n";
    }
    writeFile(note, body);
    omanotes::MainWindow window({root, note, false});
    window.resize(600, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    QTRY_VERIFY(editor->document()->lines() > 300);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QCOMPARE(editor->cursorPosition(), KTextEditor::Cursor(0, 0));

    // Normal mode: Ctrl+D and Ctrl+U are Vi's half page, not Kate's Comment
    // and Uppercase, so the cursor moves and the text does not.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_D, Qt::ControlModifier);
    QTRY_VERIFY(editor->cursorPosition().line() > 0);
    const auto afterDown = editor->cursorPosition().line();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_D, Qt::ControlModifier);
    QTRY_VERIFY(editor->cursorPosition().line() > afterDown);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_U, Qt::ControlModifier);
    QTRY_COMPARE(editor->cursorPosition().line(), afterDown);
    QCOMPARE(editor->document()->text(), QString::fromUtf8(body));
    QVERIFY(!editor->document()->isModified());

    // Insert mode keeps Vi's own meaning (dedent; nothing to dedent here)
    // rather than commenting the line.
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("i"));
    QTRY_COMPARE(editor->viewMode(), KTextEditor::View::ViModeInsert);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_D, Qt::ControlModifier);
    QTest::qWait(50);
    QCOMPARE(editor->document()->text(), QString::fromUtf8(body));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QTRY_COMPARE(editor->viewMode(), KTextEditor::View::ViModeNormal);

    // The reading view has no Vi; the same keys scroll it half a screen.
    auto* reading = window.findChild<QTextBrowser*>(QStringLiteral("readingView"));
    QVERIFY(reading != nullptr);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_VERIFY(reading->isVisible());
    QTRY_VERIFY(reading->verticalScrollBar()->maximum() > 0);
    const auto top = reading->verticalScrollBar()->value();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_D, Qt::ControlModifier);
    QTRY_VERIFY(reading->verticalScrollBar()->value() > top);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_U, Qt::ControlModifier);
    QTRY_COMPARE(reading->verticalScrollBar()->value(), top);

    // Both are in the help with their keys.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("?"));
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpOverlay"));
    QTRY_VERIFY(help->isVisible());
    auto* commands = help->findChild<QTreeWidget*>(QStringLiteral("helpCommands"));
    std::map<QString, QString> keysById;
    for (int row = 0; row < commands->topLevelItemCount(); ++row) {
        auto* item = commands->topLevelItem(row);
        keysById[item->data(0, Qt::UserRole).toString()] = item->text(1);
    }
    // Lower case: Ctrl+d is not Ctrl+Shift+D, and the help must not imply it.
    QCOMPARE(keysById[QStringLiteral("view.half-page-down")], QStringLiteral("Ctrl+d"));
    QCOMPARE(keysById[QStringLiteral("view.half-page-up")], QStringLiteral("Ctrl+u"));
    QCOMPARE(keysById[QStringLiteral("file.save")], QStringLiteral("Ctrl+s"));
    QCOMPARE(keysById[QStringLiteral("pane.editor")], QStringLiteral("Ctrl+l (sidebar)"));
    // The way out is listed, labelled as Matt asked, with its Vi route.
    QCOMPARE(keysById[QStringLiteral("app.quit")], QStringLiteral(":q"));
    QTreeWidgetItem* quit = nullptr;
    for (int row = 0; row < commands->topLevelItemCount(); ++row) {
        if (commands->topLevelItem(row)->data(0, Qt::UserRole).toString() ==
            QStringLiteral("app.quit")) {
            quit = commands->topLevelItem(row);
        }
    }
    QVERIFY(quit != nullptr);
    QCOMPARE(quit->text(0), QStringLiteral("IYKYK"));
    QTest::keyClick(commands, Qt::Key_Escape);
    QTRY_VERIFY(!help->isVisible());
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
    QTRY_COMPARE(status->text(), QStringLiteral("Reading"));

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
    QVERIFY2(!status->text().contains(QStringLiteral("[+]")), qPrintable(status->text()));
}

void MainWindowTest::readingViewRoutesCopyAndRefusesPaste() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Title\n\nbody text\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("editorStack"));
    auto* reading = window.findChild<QTextBrowser*>(QStringLiteral("readingView"));
    auto* editor = activeEditor(window);
    QVERIFY(status != nullptr && stack != nullptr && reading != nullptr && editor != nullptr);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());

    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    QTRY_COMPARE(stack->currentWidget(), reading);
    QTRY_VERIFY(reading->hasFocus());

    // Super+C (Ctrl+Meta+C) over a reading-view selection copies it — the
    // gate must consult the visible pane's selection, not the hidden editor's.
    auto cursor = reading->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    reading->setTextCursor(cursor);
    QApplication::clipboard()->setText(QStringLiteral("seed"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_C, Qt::ControlModifier | Qt::MetaModifier);
    QTRY_COMPARE(QApplication::clipboard()->text(), QStringLiteral("Title"));

    // Without a selection the chord stays inert.
    cursor.clearSelection();
    reading->setTextCursor(cursor);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_C, Qt::ControlModifier | Qt::MetaModifier);
    QTest::qWait(50);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Title"));

    // Paste while reading must never mutate the hidden source buffer.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_V, Qt::ControlModifier | Qt::MetaModifier);
    QTRY_VERIFY2(status->text().contains(QStringLiteral("read-only")), qPrintable(status->text()));
    QCOMPARE(editor->document()->text(), QStringLiteral("# Title\n\nbody text\n"));
    QVERIFY(!editor->document()->isModified());

    // Ctrl+Q (visual block) is a writing-mode key; reading ignores it.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Q, Qt::ControlModifier);
    QTest::qWait(50);
    QCOMPARE(stack->currentWidget(), reading);
    QCOMPARE(editor->document()->text(), QStringLiteral("# Title\n\nbody text\n"));
}

namespace {

/// A distinct fixture theme so every assertion below proves a colour came
/// from the theme, not from any default.
omanotes::ThemeSources writeFixtureTheme(const QTemporaryDir& directory) {
    const auto root = pathFor(directory.path());
    const omanotes::ThemeSources sources{root / "state" / "omarchy" / "current",
                                         root / "config" / "omarchy"};
    std::filesystem::create_directories(sources.stateDir / "theme");
    std::filesystem::create_directories(sources.configDir);
    writeFile(sources.stateDir / "theme" / "colors.toml", "mode = \"dark\"\n"
                                                          "accent = \"#d08050\"\n"
                                                          "selection = \"#303a60\"\n"
                                                          "muted = \"#8a90a8\"\n"
                                                          "background = \"#101018\"\n"
                                                          "dark_background = \"#181826\"\n"
                                                          "lighter_background = \"#262636\"\n"
                                                          "foreground = \"#e6e0d2\"\n"
                                                          "blue = \"#7090d0\"\n");
    writeFile(sources.configDir / "shell.toml", "[font]\nbase-size = 14\n");
    return sources;
}

} // namespace

void MainWindowTest::themeDressesEveryRegion() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path())) / "notes";
    const auto note = root / "note.md";
    std::filesystem::create_directories(root);
    writeFile(note, "# Title\n");
    omanotes::MainWindow window({root, note, false}, writeFixtureTheme(temporary));
    window.show();

    // The sidebar's selected row is painted only while the sidebar has
    // focus, and vanishes otherwise; no accent line marks either pane (Matt's
    // gate finding, 2026-09-10). Under an active stylesheet Qt ignores
    // QPalette for item selection, so the rules must be in the stylesheet.
    const auto sheet = window.styleSheet();
    QVERIFY(sheet.contains(QStringLiteral("QFrame#sidebar[paneActive=\"true\"] "
                                          "QTreeView::item:selected { background-color: #303a60")));
    QVERIFY(sheet.contains(
        QStringLiteral("QFrame#sidebar QTreeView::item:selected { background-color: #181826")));
    QVERIFY(!sheet.contains(QStringLiteral("border-top")));

    // The reading pane's ground is stylesheet-painted; its document colours
    // (text, links) still come from the palette it renders with.
    QVERIFY(sheet.contains(QStringLiteral("QTextBrowser#readingView { background-color: #101018")));
    auto* reading = window.findChild<QTextBrowser*>(QStringLiteral("readingView"));
    QVERIFY(reading != nullptr);
    QCOMPARE(reading->palette().color(QPalette::Link), QColor(QStringLiteral("#7090d0")));
    QCOMPARE(reading->font().pointSizeF(), 15.0); // base-size plus the reading point.
    QCOMPARE(reading->font().family(), QStringLiteral("monospace"));

    // Pure Omarchy: the whole app wears the system monospace at base-size —
    // through the stylesheet, since app-wide setFont is ignored under an
    // active style sheet (found at Matt's gate).
    QVERIFY(sheet.contains(QStringLiteral("* { font-family: monospace; font-size: 14pt; }")));

    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->configValue(QStringLiteral("background-color")).value<QColor>(),
             QColor(QStringLiteral("#101018")));
    QCOMPARE(editor->configValue(QStringLiteral("font")).value<QFont>().pointSizeF(), 14.0);

    // The window chrome carries the theme's grounds and accent, and the tree
    // itself is painted — not just the frame around it (Matt's gate finding).
    QVERIFY(window.styleSheet().contains(QStringLiteral("#d08050")));
    QVERIFY(window.styleSheet().contains(QStringLiteral("#181826")));
    QVERIFY(window.styleSheet().contains(QStringLiteral("QFrame#sidebar QTreeView")));
    // The help glyph sits flat in the status row, not in a stock button box.
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QToolButton#helpButton { background: transparent")));
    // Each bar is one band in the pane's own colour: tabs, +, the run after
    // them, the mode and the ? all share the editor's ground (Matt's gate
    // finding, 2026-09-10).
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("bufferRow")) != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("statusRow")) != nullptr);
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QWidget#bufferRow { background-color: #101018")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QWidget#statusRow { background-color: #101018")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QLabel#statusArea { background-color: #101018")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QTabBar#bufferStrip::tab { background-color: #101018")));
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QToolButton#sidebarToggleButton { background-color: #101018")));
    // Vi's `:` line is a child of the editor and wears the pane colour with
    // no frame; its completion drop-down has no parent, so its rule lives on
    // the application (Matt's gate finding, 2026-09-10).
    QVERIFY(window.styleSheet().contains(
        QStringLiteral("QLineEdit#commandtext { background-color: #101018")));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("QListView { background-color: #181826")));

    // The dialogs are separate windows the scoped rules used to miss: the
    // help overlay, the search palette, and the close prompt all theme.
    QVERIFY(sheet.contains(QStringLiteral("QTreeWidget#helpCommands")));
    QVERIFY(sheet.contains(QStringLiteral(
        "QTreeWidget#helpCommands QHeaderView::section { background-color: #181826")));
    QVERIFY(sheet.contains(QStringLiteral("QListWidget#searchResults")));
    QVERIFY(sheet.contains(QStringLiteral("QMessageBox")));
}

void MainWindowTest::focusMovesTheAccentMarkBetweenPanes() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path())) / "notes";
    const auto note = root / "note.md";
    std::filesystem::create_directories(root);
    writeFile(note, "# Title\n");
    omanotes::MainWindow window({root, note, false}, writeFixtureTheme(temporary));
    window.show();
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("sidebar"));
    auto* writingArea = window.findChild<QWidget*>(QStringLiteral("writingArea"));
    auto* editor = activeEditor(window);
    QVERIFY(sidebar != nullptr && writingArea != nullptr && editor != nullptr);

    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTRY_VERIFY(writingArea->property("paneActive").toBool());
    QVERIFY(!sidebar->property("paneActive").toBool());

    // Space e brings the tree; the mark follows the focus into the sidebar.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("e"));
    QTRY_VERIFY(sidebar->property("paneActive").toBool());
    QVERIFY(!writingArea->property("paneActive").toBool());

    // Ctrl+L returns to the editor; the mark returns with it.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(writingArea->property("paneActive").toBool());
    QVERIFY(!sidebar->property("paneActive").toBool());
}

void MainWindowTest::themeFollowsALiveThemeSwitch() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path())) / "notes";
    const auto note = root / "note.md";
    std::filesystem::create_directories(root);
    writeFile(note, "# Title\n");
    const auto sources = writeFixtureTheme(temporary);
    omanotes::MainWindow window({root, note, false}, sources);
    window.show();
    QVERIFY(window.styleSheet().contains(QStringLiteral("#d08050")));

    // A theme switch, as Omarchy performs it: the palette file changes and
    // theme.name is rewritten. The window must follow without a restart.
    writeFile(sources.stateDir / "theme" / "colors.toml", "mode = \"dark\"\n"
                                                          "accent = \"#40c057\"\n"
                                                          "selection = \"#2b4a33\"\n"
                                                          "background = \"#0e1410\"\n"
                                                          "foreground = \"#d8e8dc\"\n");
    writeFile(sources.stateDir / "theme.name", "fixture-green\n");

    QTRY_VERIFY(window.styleSheet().contains(QStringLiteral("#40c057")));
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    QTRY_COMPARE(editor->configValue(QStringLiteral("background-color")).value<QColor>(),
                 QColor(QStringLiteral("#0e1410")));

    // The text scale follows too, app-wide through the stylesheet.
    writeFile(sources.configDir / "shell.toml", "[font]\nbase-size = 18\n");
    QTRY_COMPARE(editor->configValue(QStringLiteral("font")).value<QFont>().pointSizeF(), 18.0);
    QVERIFY(window.styleSheet().contains(QStringLiteral("font-size: 18pt")));
}

void MainWindowTest::closesCleanly() {
    omanotes::MainWindow window(launchRequest());
    window.show();
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
}

void MainWindowTest::colonQuitsWithPromptForUnsavedWork() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "clean\n");
    omanotes::MainWindow window({root, note, false});
    window.show();
    auto* editor = activeEditor(window);
    QVERIFY(editor != nullptr);
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));

    // Unsaved work: :q asks rather than leaving or refusing silently.
    editor->document()->setText(QStringLiteral("clean\nedited\n"));
    QTRY_VERIFY(editor->document()->isModified());
    typeViCommand(*editor, QStringLiteral("q"));
    auto* prompt = window.findChild<QMessageBox*>(QStringLiteral("quitPrompt"));
    QTRY_VERIFY(prompt != nullptr && prompt->isVisible());
    QVERIFY2(prompt->text().contains(QStringLiteral("note.md")), qPrintable(prompt->text()));

    // Cancel stays, with everything as it was.
    QTest::mouseClick(prompt->button(QMessageBox::Cancel), Qt::LeftButton);
    QTRY_VERIFY(!prompt->isVisible());
    QVERIFY(window.isVisible());
    QVERIFY(activeEditor(window)->document()->isModified());
    QCOMPARE(readFile(note), QByteArray("clean\n"));

    // Save writes inside the workspace and then quits.
    typeViCommand(*activeEditor(window), QStringLiteral("q"));
    QTRY_VERIFY(prompt->isVisible());
    QTest::mouseClick(prompt->button(QMessageBox::Save), Qt::LeftButton);
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(readFile(note), QByteArray("clean\nedited\n"));
    Q_UNUSED(status);
}

void MainWindowTest::colonQuitVariantsSaveOrDiscard() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "clean\n");

    // A clean desk: :q simply closes.
    {
        omanotes::MainWindow window({root, note, false});
        window.show();
        typeViCommand(*activeEditor(window), QStringLiteral("q"));
        QTRY_VERIFY(!window.isVisible());
    }

    // :q! discards: the window closes and the disk is untouched.
    {
        omanotes::MainWindow window({root, note, false});
        window.show();
        auto* editor = activeEditor(window);
        editor->document()->setText(QStringLiteral("clean\nlost\n"));
        QTRY_VERIFY(editor->document()->isModified());
        typeViCommand(*editor, QStringLiteral("q!"));
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(readFile(note), QByteArray("clean\n"));
    }

    // :wq writes the active buffer and quits, no prompt.
    {
        omanotes::MainWindow window({root, note, false});
        window.show();
        auto* editor = activeEditor(window);
        editor->document()->setText(QStringLiteral("clean\nkept\n"));
        QTRY_VERIFY(editor->document()->isModified());
        typeViCommand(*editor, QStringLiteral("wq"));
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(readFile(note), QByteArray("clean\nkept\n"));
        QVERIFY(window.findChild<QMessageBox*>(QStringLiteral("quitPrompt")) == nullptr);
    }

    // Save on the prompt cannot name an Untitled buffer: it refuses with a
    // message and stays, rather than quitting past the unsaved work.
    {
        omanotes::MainWindow window({root, std::nullopt, false});
        window.show();
        auto* editor = activeEditor(window);
        editor->document()->setText(QStringLiteral("draft"));
        QTRY_VERIFY(editor->document()->isModified());
        typeViCommand(*editor, QStringLiteral("q"));
        auto* prompt = window.findChild<QMessageBox*>(QStringLiteral("quitPrompt"));
        QTRY_VERIFY(prompt != nullptr && prompt->isVisible());
        QTest::mouseClick(prompt->button(QMessageBox::Save), Qt::LeftButton);
        QTRY_VERIFY(!prompt->isVisible());
        auto* status = window.findChild<QLabel*>(QStringLiteral("statusArea"));
        QTRY_VERIFY2(status->text().contains(QStringLiteral("no file name")),
                     qPrintable(status->text()));
        QVERIFY(window.isVisible());

        // Discard on the prompt closes it and quits.
        typeViCommand(*activeEditor(window), QStringLiteral("q"));
        QTRY_VERIFY(prompt->isVisible());
        QTest::mouseClick(prompt->button(QMessageBox::Discard), Qt::LeftButton);
        QTRY_VERIFY(!window.isVisible());
    }
}

void MainWindowTest::sidebarSelectionBarFollowsFocus() {
    QTemporaryDir temporary;
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    writeFile(root / "alpha.md", "# Alpha\n");
    writeFile(root / "beta.md", "# Beta\n");
    omanotes::MainWindow window({root, std::nullopt, false});
    window.show();
    auto* editor = activeEditor(window);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("sidebar"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("fileTree"));
    QVERIFY(sidebar != nullptr && tree != nullptr);

    // Entering the sidebar selects the row it lands on, not merely makes it
    // current: a QTreeView given focus with no current row picks the first
    // one itself without selecting it, and an unselected row paints no bar.
    auto* target = editor->focusProxy() != nullptr ? editor->focusProxy() : editor;
    QTest::keyClick(target, Qt::Key_Space);
    QTest::keyClicks(target, QStringLiteral("e"));
    QTRY_VERIFY(tree->hasFocus());
    QTRY_VERIFY(sidebar->property("paneActive").toBool());
    QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("alpha.md"));
    QVERIFY(tree->selectionModel()->isSelected(tree->currentIndex()));

    // Leaving the sidebar clears its focus flag; the stylesheet keys the
    // bar's colour to that flag, so the bar goes with it.
    QTest::keyClick(tree, Qt::Key_L, Qt::ControlModifier);
    QTRY_VERIFY(activeEditor(window)->hasFocus());
    QTRY_VERIFY(!sidebar->property("paneActive").toBool());
    QVERIFY(tree->selectionModel()->isSelected(tree->currentIndex()));
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
            QCOMPARE(item->text(1), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+S"))
                                        .toString(QKeySequence::NativeText));
        }
        if (id == QStringLiteral("buffer.new")) {
            sawNew = true;
            QCOMPARE(item->text(1), QStringLiteral("Space n"));
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
