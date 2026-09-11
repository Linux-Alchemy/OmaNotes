#include "app/application_controller.hpp"
#include "app/launch_request.hpp"
#include "persistence/recovery_store.hpp"
#include "session/session_snapshot.hpp"
#include "session/session_store.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_adapter.hpp"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QFile>
#include <QLabel>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QtTest>

#include <filesystem>
#include <memory>

namespace {

std::filesystem::path pathFor(const QString& path) {
    return std::filesystem::canonical(path.toStdString());
}

QByteArray readFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

KTextEditor::View* activeView(omanotes::MainWindow& window) {
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("editorStack"));
    return stack == nullptr ? nullptr : qobject_cast<KTextEditor::View*>(stack->currentWidget());
}

QString status(omanotes::MainWindow& window) { return window.statusText(); }

/// Type text into the active editor the way a user would, leaving the
/// buffer modified.
void typeInto(omanotes::MainWindow& window, const QString& text) {
    auto* view = activeView(window);
    QVERIFY(view != nullptr);
    view->document()->insertText(view->document()->documentEnd(), text);
    QVERIFY(view->document()->isModified());
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

struct Workspace {
    QTemporaryDir workspaceDir;
    QTemporaryDir stateDir;
    std::filesystem::path root;
    std::filesystem::path sessions;

    Workspace() {
        root = pathFor(workspaceDir.path());
        sessions = pathFor(stateDir.path()) / "omanotes" / "sessions";
    }
    [[nodiscard]] bool valid() const { return workspaceDir.isValid() && stateDir.isValid(); }
    [[nodiscard]] std::filesystem::path note(const char* name) const { return root / name; }
    [[nodiscard]] std::filesystem::path sessionFile() const {
        return sessions / omanotes::SessionStore::workspaceId(root) / "session.json";
    }
    [[nodiscard]] std::filesystem::path recoveryDirectory() const {
        return sessions / omanotes::SessionStore::workspaceId(root) / "recovery";
    }
    [[nodiscard]] int recoveryRecords() const {
        int found = 0;
        std::error_code error;
        for ([[maybe_unused]] const auto& entry :
             std::filesystem::directory_iterator(recoveryDirectory(), error)) {
            ++found;
        }
        return found;
    }
};

/// One launch of the application: a window and its session controller,
/// against a workspace and a private state directory.
struct Launch {
    std::unique_ptr<omanotes::MainWindow> window;
    std::unique_ptr<omanotes::ApplicationController> controller;

    /// Launch in `workspace`, against its own state directory unless
    /// `sessionsOverride` points at another workspace's.
    explicit Launch(const Workspace& workspace,
                    std::optional<std::filesystem::path> requested = std::nullopt,
                    bool fresh = false,
                    const std::optional<std::filesystem::path>& sessionsOverride = std::nullopt) {
        const auto& root = workspace.root;
        omanotes::LaunchRequest request{root, std::move(requested), fresh};
        omanotes::ThemeSources sources{root / ".no-theme-state", root / ".no-theme-config"};
        window = std::make_unique<omanotes::MainWindow>(request, sources);
        controller = std::make_unique<omanotes::ApplicationController>(
            *window, request, sessionsOverride.value_or(workspace.sessions));
        controller->start();
        window->show();
    }
    /// A clean close: the window's close event runs and the desk is saved.
    void close() {
        window->close();
        QCoreApplication::processEvents();
        controller.reset();
        window.reset();
    }
    /// A forced kill: nothing runs. Whatever was checkpointed is what survives.
    void kill() {
        controller.reset();
        window.reset();
    }
    [[nodiscard]] const omanotes::BufferRegistry& buffers() const { return window->buffers(); }
    [[nodiscard]] QStringList names() const {
        QStringList result;
        for (const auto& buffer : buffers().buffers()) {
            result << buffer.displayName;
        }
        return result;
    }
    [[nodiscard]] QString activeName() const {
        const auto active = buffers().activeId();
        const auto* state = active ? buffers().find(*active) : nullptr;
        return state != nullptr ? state->displayName : QString();
    }
    void openFromSidebar(const std::filesystem::path& file) { window->focusRequestedFile(file); }
    void toggleSidebar() {
        auto* view = activeView(*window);
        QVERIFY(view != nullptr);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_E);
    }
    /// `Space m`, from whichever pane is showing.
    void toggleReading() {
        auto* stack = window->findChild<QStackedWidget*>(QStringLiteral("editorStack"));
        QVERIFY(stack != nullptr && stack->currentWidget() != nullptr);
        stack->currentWidget()->setFocus();
        QTRY_VERIFY(stack->currentWidget()->hasFocus());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
        QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("m"));
    }
};

} // namespace

class SessionRestoreTest final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanCloseRestoresTheDesk();
    void forcedKillRestoresDirtyTextAndSaveReleasesIt();
    void missingFileIsSkippedAndTheRestComesBack();
    void anotherRootStartsCleanWithAParkedWorkNotice();
    void corruptStateStartsCleanAndLeavesTheFileAlone();
    void freshBypassesWithoutDestroyingTheSession();
    void requestedFileIsFocusedLast();
    void concurrentLaunchesLastCloseWinsWithoutCorruption();
    void secondInstanceLeavesTheFirstsRecordsAlone();
    void theLockFollowsTheLiveInstance();
    void parkedWorkNoticeSurvivesAVanishedRoot();
    void orphanRecordComesBackDirty();
    void recoveredNoteChangedOnDiskRefusesPlainWrite();
    void readingViewFollowsTheCursor();
    void desktopChangesAreCheckpointedAfterTheDebounce();
};

void SessionRestoreTest::initTestCase() {
    QVERIFY2(qEnvironmentVariableIsSet("QT_QPA_PLATFORM"),
             "run through ctest for the headless env");
}

void SessionRestoreTest::cleanCloseRestoresTheDesk() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("alpha.md"), "# Alpha\n\none\ntwo\nthree\n");
    writeFile(workspace.note("beta.md"), "# Beta\n");
    {
        Launch first(workspace);
        first.openFromSidebar(workspace.note("alpha.md"));
        first.openFromSidebar(workspace.note("beta.md"));
        first.toggleReading();
        first.openFromSidebar(workspace.note("alpha.md"));
        first.toggleSidebar();
        QCOMPARE(first.names(),
                 (QStringList{omanotes::scratchDisplayName(), "alpha.md", "beta.md"}));
        first.close();
    }
    QVERIFY(std::filesystem::exists(workspace.sessionFile()));
    const auto saved = omanotes::readSessionSnapshot(workspace.sessionFile());
    QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error().describe()));
    QVERIFY(saved->sidebar.visible);
    QCOMPARE(saved->buffers.size(), std::size_t{3});
    QCOMPARE(saved->buffers[2].viewMode, omanotes::ViewMode::Reading);
    QVERIFY(!readFile(workspace.sessionFile()).contains("three"));

    Launch second(workspace);
    // The desk as left, including the empty scratch that was part of it; the
    // constructor's own scratch does not double it.
    QCOMPARE(second.names(), (QStringList{omanotes::scratchDisplayName(), "alpha.md", "beta.md"}));
    QCOMPARE(second.activeName(), QStringLiteral("alpha.md"));
    QVERIFY(second.window->findChild<QWidget*>(QStringLiteral("sidebar"))->isVisible());
    QVERIFY(status(*second.window).contains(QStringLiteral("Restored 3 buffers")));
    QCOMPARE(second.controller->lastReport().restored, 3);
    QCOMPARE(second.controller->lastReport().skipped.size(), std::size_t{0});
    second.close();
}

void SessionRestoreTest::forcedKillRestoresDirtyTextAndSaveReleasesIt() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("draft.md"), "# Draft\n");
    {
        Launch first(workspace);
        first.openFromSidebar(workspace.note("draft.md"));
        typeInto(*first.window, QStringLiteral("unsaved paragraph\n"));
        first.controller->checkpointNow(); // the debounce, without the wait
        QCOMPARE(workspace.recoveryRecords(), 1);
        first.kill();
    }
    QCOMPARE(readFile(workspace.note("draft.md")), QByteArray("# Draft\n"));

    Launch second(workspace);
    QCOMPARE(second.names(), (QStringList{omanotes::scratchDisplayName(), "draft.md"}));
    QCOMPARE(second.activeName(), QStringLiteral("draft.md"));
    auto* view = activeView(*second.window);
    QVERIFY(view != nullptr);
    QVERIFY(view->document()->text().contains(QStringLiteral("unsaved paragraph")));
    QVERIFY(view->document()->isModified());
    QVERIFY(status(*second.window).contains(QStringLiteral("recovered 1 with unsaved changes")));
    QCOMPARE(second.controller->recoveryIds().size(), std::size_t{1});
    // Loading did not consume the record; saving does.
    QCOMPARE(workspace.recoveryRecords(), 1);
    typeViCommand(*view, QStringLiteral("w"));
    QTRY_VERIFY(status(*second.window).startsWith(QStringLiteral("Wrote")));
    QVERIFY(readFile(workspace.note("draft.md")).contains("unsaved paragraph"));
    QCOMPARE(workspace.recoveryRecords(), 0);
    QVERIFY(second.controller->recoveryIds().empty());
    second.close();
}

void SessionRestoreTest::missingFileIsSkippedAndTheRestComesBack() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("keep.md"), "# Keep\n");
    writeFile(workspace.note("gone.md"), "# Gone\n");
    {
        Launch first(workspace);
        first.openFromSidebar(workspace.note("gone.md"));
        first.openFromSidebar(workspace.note("keep.md"));
        first.close();
    }
    std::filesystem::remove(workspace.note("gone.md"));

    Launch second(workspace);
    QCOMPARE(second.names(), (QStringList{omanotes::scratchDisplayName(), "keep.md"}));
    QCOMPARE(second.activeName(), QStringLiteral("keep.md"));
    QVERIFY(status(*second.window).contains(QStringLiteral("skipped gone.md (missing)")));
    // Nothing was created to fill the gap.
    QVERIFY(!std::filesystem::exists(workspace.note("gone.md")));
    second.close();
}

void SessionRestoreTest::anotherRootStartsCleanWithAParkedWorkNotice() {
    Workspace first;
    Workspace other;
    QVERIFY(first.valid() && other.valid());
    writeFile(first.note("secret.md"), "# Secret\n");
    {
        Launch launch(first);
        launch.openFromSidebar(first.note("secret.md"));
        typeInto(*launch.window, QStringLiteral("private thought\n"));
        launch.close();
    }
    QCOMPARE(first.recoveryRecords(), 1);

    // Same state directory, different workspace.
    Launch elsewhere(other, std::nullopt, false, first.sessions);
    QCOMPARE(elsewhere.names(), (QStringList{omanotes::scratchDisplayName()}));
    const auto line = status(*elsewhere.window);
    QVERIFY2(line.contains(QStringLiteral("Unsaved work waiting in")), qPrintable(line));
    QVERIFY(line.contains(QStringLiteral("(1 buffer)")));
    QVERIFY(!line.contains(QStringLiteral("private thought")));
    QVERIFY(!line.contains(QStringLiteral("secret.md")));
    elsewhere.close();
    // The other root's session and record are untouched by all of this.
    QCOMPARE(first.recoveryRecords(), 1);
    QVERIFY(omanotes::readSessionSnapshot(first.sessionFile()).has_value());
}

void SessionRestoreTest::corruptStateStartsCleanAndLeavesTheFileAlone() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    std::filesystem::create_directories(workspace.sessionFile().parent_path());
    const QByteArray garbage("{\"version\": 1, \"buffers\": [{\"id\": ");
    writeFile(workspace.sessionFile(), garbage);

    Launch launch(workspace);
    QCOMPARE(launch.names(), (QStringList{omanotes::scratchDisplayName()}));
    QVERIFY(status(*launch.window).contains(QStringLiteral("Session state ignored")));
    QVERIFY(status(*launch.window).contains(QStringLiteral("starting clean")));
    QCOMPARE(readFile(workspace.sessionFile()), garbage);
    launch.close();
    // A clean close writes a valid session over the corrupt one.
    QVERIFY(omanotes::readSessionSnapshot(workspace.sessionFile()).has_value());
}

void SessionRestoreTest::freshBypassesWithoutDestroyingTheSession() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("real.md"), "# Real\n");
    {
        Launch first(workspace);
        first.openFromSidebar(workspace.note("real.md"));
        first.close();
    }
    const auto before = readFile(workspace.sessionFile());
    QVERIFY(!before.isEmpty());

    Launch fresh(workspace, std::nullopt, /*fresh=*/true);
    QCOMPARE(fresh.names(), (QStringList{omanotes::scratchDisplayName()}));
    QVERIFY(!status(*fresh.window).contains(QStringLiteral("Restored")));
    typeInto(*fresh.window, QStringLiteral("throwaway\n"));
    fresh.close();
    QCOMPARE(readFile(workspace.sessionFile()), before);
    // The throwaway text is still protected, as an orphan for the next launch.
    QCOMPARE(workspace.recoveryRecords(), 1);

    Launch again(workspace);
    QVERIFY(again.names().contains(QStringLiteral("real.md")));
    QVERIFY(again.names().contains(omanotes::scratchDisplayName()));
    again.close();
}

void SessionRestoreTest::requestedFileIsFocusedLast() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("a.md"), "# A\n");
    writeFile(workspace.note("b.md"), "# B\n");
    writeFile(workspace.note("c.md"), "# C\n");
    {
        Launch first(workspace);
        first.openFromSidebar(workspace.note("b.md"));
        first.openFromSidebar(workspace.note("a.md"));
        first.close();
    }
    // A file already in the session comes to the front, not open twice.
    Launch known(workspace, workspace.note("b.md"));
    QCOMPARE(known.names(), (QStringList{"b.md", omanotes::scratchDisplayName(), "a.md"}));
    QCOMPARE(known.activeName(), QStringLiteral("b.md"));
    known.close();
    // A new file joins the restored desk and takes focus.
    Launch fresh(workspace, workspace.note("c.md"));
    // The previous launch left b.md first; c.md, requested now, opens ahead of it.
    QCOMPARE(fresh.names(), (QStringList{"c.md", "b.md", omanotes::scratchDisplayName(), "a.md"}));
    QCOMPARE(fresh.activeName(), QStringLiteral("c.md"));
    fresh.close();
}

void SessionRestoreTest::concurrentLaunchesLastCloseWinsWithoutCorruption() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("shared.md"), "# Shared\n");
    writeFile(workspace.note("second.md"), "# Second\n");
    {
        Launch seed(workspace);
        seed.openFromSidebar(workspace.note("shared.md"));
        seed.close();
    }
    Launch one(workspace);
    Launch two(workspace);
    QCOMPARE(one.names(), (QStringList{omanotes::scratchDisplayName(), "shared.md"}));
    QCOMPARE(two.names(), (QStringList{omanotes::scratchDisplayName(), "shared.md"}));
    two.openFromSidebar(workspace.note("second.md"));
    two.controller->checkpointNow();
    one.controller->checkpointNow();
    // Both wrote; the file is always a complete document.
    QVERIFY(omanotes::readSessionSnapshot(workspace.sessionFile()).has_value());
    one.close();
    two.close();

    Launch after(workspace);
    QCOMPARE(after.names(),
             (QStringList{omanotes::scratchDisplayName(), "shared.md", "second.md"}));
    after.close();
}

void SessionRestoreTest::secondInstanceLeavesTheFirstsRecordsAlone() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("draft.md"), "# Draft\n");
    writeFile(workspace.note("other.md"), "# Other\n");

    // The first window owns the workspace: it holds the lock and protects
    // its unsaved text in a record.
    Launch one(workspace);
    QVERIFY(one.controller->holdsInstanceLock());
    one.openFromSidebar(workspace.note("draft.md"));
    typeInto(*one.window, QStringLiteral("first instance text "));
    one.controller->checkpointNow();
    QCOMPARE(workspace.recoveryRecords(), 1);
    std::filesystem::path firstRecord;
    for (const auto& entry : std::filesystem::directory_iterator(workspace.recoveryDirectory())) {
        firstRecord = entry.path();
    }
    const auto firstBytes = readFile(firstRecord);
    QVERIFY(firstBytes.contains("first instance text"));

    // A second window on the same root does not hold the lock. It sees the
    // desk, but the first window's unsaved text stays with the first window:
    // draft.md comes back clean and no record id is adopted.
    Launch two(workspace);
    QVERIFY(!two.controller->holdsInstanceLock());
    QVERIFY2(status(*two.window).contains(QStringLiteral("Another OmaNotes has this workspace")),
             qPrintable(status(*two.window)));
    QVERIFY2(status(*two.window).contains(QStringLiteral("held by another OmaNotes")),
             qPrintable(status(*two.window)));
    QVERIFY(two.controller->recoveryIds().empty());
    QVERIFY(two.names().contains(QStringLiteral("draft.md")));
    two.openFromSidebar(workspace.note("draft.md"));
    QVERIFY(
        !activeView(*two.window)->document()->text().contains(QStringLiteral("first instance")));
    QVERIFY(!activeView(*two.window)->document()->isModified());

    // The second window's own unsaved text gets its own record; the first
    // window's record is byte-for-byte untouched.
    two.openFromSidebar(workspace.note("other.md"));
    typeInto(*two.window, QStringLiteral("second instance text "));
    two.controller->checkpointNow();
    QCOMPARE(workspace.recoveryRecords(), 2);
    QCOMPARE(readFile(firstRecord), firstBytes);
    one.controller->checkpointNow();
    QCOMPARE(readFile(firstRecord), firstBytes);

    two.close();
    one.close();
    QCOMPARE(workspace.recoveryRecords(), 2);

    // With both gone, the next launch holds the lock and brings back both
    // texts: one referenced by the last snapshot, one as an orphan.
    Launch three(workspace);
    QVERIFY(three.controller->holdsInstanceLock());
    QVERIFY2(status(*three.window).contains(QStringLiteral("recovered 2 with unsaved changes")),
             qPrintable(status(*three.window)));
    QCOMPARE(three.controller->recoveryIds().size(), std::size_t{2});
    three.close();
}

void SessionRestoreTest::theLockFollowsTheLiveInstance() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    {
        Launch one(workspace);
        QVERIFY(one.controller->holdsInstanceLock());
        // A clean close releases it.
        one.close();
    }
    {
        Launch two(workspace);
        QVERIFY(two.controller->holdsInstanceLock());
        // So does a kill: the kernel drops the lock with the descriptor.
        two.kill();
    }
    Launch three(workspace);
    QVERIFY(three.controller->holdsInstanceLock());
    three.close();
}

void SessionRestoreTest::parkedWorkNoticeSurvivesAVanishedRoot() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    Launch launch(workspace);
    QVERIFY(launch.controller->parkedWorkNotice().isEmpty());

    // The workspace directory disappears under a running window. The notice
    // is a courtesy built from other roots' metadata; it must come back
    // empty, not dereference a failed resolution.
    std::filesystem::remove_all(workspace.root);
    QVERIFY(launch.controller->parkedWorkNotice().isEmpty());
    std::filesystem::create_directories(workspace.root);
    launch.close();
}

void SessionRestoreTest::orphanRecordComesBackDirty() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    {
        Launch seed(workspace);
        seed.close();
    }
    const omanotes::RecoveryStore records(workspace.recoveryDirectory());
    const auto planted =
        records.checkpoint({std::nullopt, QStringLiteral("orphaned thought\n"), std::nullopt});
    QVERIFY(planted.has_value());

    Launch launch(workspace);
    QVERIFY(status(*launch.window).contains(QStringLiteral("recovered 1 with unsaved changes")));
    auto* view = activeView(*launch.window);
    QVERIFY(view != nullptr);
    QVERIFY(view->document()->text().contains(QStringLiteral("orphaned thought")));
    QVERIFY(view->document()->isModified());
    QCOMPARE(workspace.recoveryRecords(), 1);
    launch.close();
    // Still dirty at close: still protected.
    QCOMPARE(workspace.recoveryRecords(), 1);
}

void SessionRestoreTest::recoveredNoteChangedOnDiskRefusesPlainWrite() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("shared.md"), "# Shared\n");
    {
        Launch first(workspace);
        first.openFromSidebar(workspace.note("shared.md"));
        typeInto(*first.window, QStringLiteral("mine\n"));
        first.controller->checkpointNow();
        first.kill();
    }
    writeFile(workspace.note("shared.md"), "# Shared\n\ntheirs\n");

    Launch second(workspace);
    auto* view = activeView(*second.window);
    QVERIFY(view != nullptr);
    QVERIFY(view->document()->text().contains(QStringLiteral("mine")));
    QVERIFY(view->document()->isModified());
    typeViCommand(*view, QStringLiteral("w"));
    QTRY_VERIFY(status(*second.window).contains(QStringLiteral("changed on disk")));
    QCOMPARE(readFile(workspace.note("shared.md")), QByteArray("# Shared\n\ntheirs\n"));
    typeViCommand(*view, QStringLiteral("w!"));
    QTRY_VERIFY(status(*second.window).startsWith(QStringLiteral("Wrote")));
    QVERIFY(readFile(workspace.note("shared.md")).contains("mine"));
    QCOMPARE(workspace.recoveryRecords(), 0);
    second.close();
}

void SessionRestoreTest::readingViewFollowsTheCursor() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    QByteArray longNote("# Long\n\n");
    for (int line = 0; line < 300; ++line) {
        longNote += QByteArray("Paragraph ") + QByteArray::number(line) + " of a long note.\n\n";
    }
    writeFile(workspace.note("long.md"), longNote);
    Launch launch(workspace);
    launch.window->resize(800, 500);
    launch.openFromSidebar(workspace.note("long.md"));
    auto* view = activeView(*launch.window);
    QVERIFY(view != nullptr);
    view->setCursorPosition(KTextEditor::Cursor(view->document()->lines() - 1, 0));

    launch.toggleReading();
    auto* reading = launch.window->findChild<QTextBrowser*>(QStringLiteral("readingView"));
    QVERIFY(reading != nullptr);
    QTRY_VERIFY(reading->isVisible());
    QTRY_VERIFY(reading->verticalScrollBar()->maximum() > 0);
    QVERIFY2(reading->verticalScrollBar()->value() > reading->verticalScrollBar()->maximum() / 2,
             "reading view should open near the cursor, not at the top");

    // Coming back lands near where the reader was, without moving the cursor.
    reading->verticalScrollBar()->setValue(reading->verticalScrollBar()->maximum() / 2);
    launch.toggleReading();
    QTRY_VERIFY(activeView(*launch.window) != nullptr);
    QCOMPARE(activeView(*launch.window)->cursorPosition().line(), view->document()->lines() - 1);
    QVERIFY(activeView(*launch.window)->firstDisplayedLine() > 0);
    launch.close();
}

void SessionRestoreTest::desktopChangesAreCheckpointedAfterTheDebounce() {
    Workspace workspace;
    QVERIFY(workspace.valid());
    writeFile(workspace.note("note.md"), "# Note\n");
    Launch launch(workspace);
    launch.openFromSidebar(workspace.note("note.md"));
    QVERIFY(!std::filesystem::exists(workspace.sessionFile()));
    typeInto(*launch.window, QStringLiteral("soon protected\n"));
    // Nothing is written immediately...
    QVERIFY(!std::filesystem::exists(workspace.sessionFile()));
    // ...and within the debounce window both the record and the snapshot land.
    QTRY_VERIFY_WITH_TIMEOUT(std::filesystem::exists(workspace.sessionFile()), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(workspace.recoveryRecords(), 1, 5000);
    const auto snapshot = omanotes::readSessionSnapshot(workspace.sessionFile());
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->dirtyBufferCount(), std::size_t{1});
    launch.kill();
}

QTEST_MAIN(SessionRestoreTest)
#include "session_restore_test.moc"
