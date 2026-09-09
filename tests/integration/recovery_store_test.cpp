#include "persistence/recovery_store.hpp"

#include "session/session_store.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <vector>

Q_DECLARE_METATYPE(omanotes::AtomicWriteFaults)

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

mode_t modeOf(const std::filesystem::path& path) {
    struct stat status{};
    if (::lstat(path.c_str(), &status) != 0) {
        return 0;
    }
    return status.st_mode & 07777;
}

int entriesIn(const std::filesystem::path& directory) {
    int found = 0;
    std::error_code error;
    for ([[maybe_unused]] const auto& entry :
         std::filesystem::directory_iterator(directory, error)) {
        ++found;
    }
    return found;
}

std::optional<omanotes::BufferRecovery> loadOptional(const omanotes::RecoveryStore& store,
                                                     omanotes::RecoveryId id) {
    const auto loaded = store.load(id);
    return loaded.has_value() ? *loaded : std::nullopt;
}

omanotes::BufferRecovery present(const std::optional<omanotes::BufferRecovery>& state) {
    return state.value_or(omanotes::BufferRecovery{});
}

/// A workspace with one saved note, and a private recovery directory.
struct Fixture {
    QTemporaryDir workspaceDir;
    QTemporaryDir stateDir;
    std::filesystem::path recovery;
    std::expected<omanotes::WorkspaceRoot, omanotes::WorkspaceError> resolved;
    QByteArray savedBytes{"# Ideas\n\nfirst line\n"};

    Fixture()
        : recovery(pathFor(stateDir.path()) / "sessions" / "abc" / "recovery"),
          resolved(omanotes::WorkspaceRoot::resolve(pathFor(workspaceDir.path()))) {
        if (resolved) {
            QFile note(QString::fromStdString((resolved->path() / "ideas.md").string()));
            if (note.open(QIODevice::WriteOnly)) {
                note.write(savedBytes);
            }
        }
    }
    [[nodiscard]] bool valid() const {
        return workspaceDir.isValid() && stateDir.isValid() && resolved.has_value();
    }
    [[nodiscard]] const omanotes::WorkspaceRoot& root() const { return *resolved; }
    [[nodiscard]] std::filesystem::path note() const { return root().path() / "ideas.md"; }
    [[nodiscard]] omanotes::BufferRecovery dirtyNote() const {
        return {"ideas.md", QStringLiteral("# Ideas\n\nfirst line\nunsaved second line\n"),
                omanotes::SavedRevision::of(savedBytes)};
    }
};

} // namespace

class RecoveryStoreTest final : public QObject {
    Q_OBJECT

  private slots:
    void checkpointsOwnerOnlyAndReusesTheId();
    void survivesARestartThroughTheSnapshotReference();
    void saveAndDiscardRemoveRecords();
    void removesOrphansButKeepsLiveRecords();
    void previousRecordSurvivesEveryInjectedFailure_data();
    void previousRecordSurvivesEveryInjectedFailure();
    void refusesOversizedBuffersWithoutWriting();
    void idsNeverBecomePaths();
    void refusesSymlinksAndLeavesTargetsAlone();
    void refusesBadRecordsAndLeavesThemAsFound();
    void plansRestoreAgainstTheLiveDisk();
    void restoreNeverWritesTheWorkspace();
};

void RecoveryStoreTest::checkpointsOwnerOnlyAndReusesTheId() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const mode_t previousMask = ::umask(0);
    const omanotes::RecoveryStore store(fixture.recovery);
    const auto first = store.checkpoint(fixture.dirtyNote());
    ::umask(previousMask);
    QVERIFY2(first.has_value(), qPrintable(first ? QString() : first.error().describe()));
    QVERIFY(!first->isNull());
    QCOMPARE(modeOf(fixture.recovery), mode_t{0700});
    QCOMPARE(entriesIn(fixture.recovery), 1);
    const auto file =
        fixture.recovery / (first->toString(QUuid::WithoutBraces).toStdString() + ".json");
    QCOMPARE(modeOf(file), mode_t{0600});
    QVERIFY(readFile(file).contains("unsaved second line"));

    const auto loaded = loadOptional(store, *first);
    QVERIFY(loaded.has_value());
    QCOMPARE(present(loaded), fixture.dirtyNote());

    // The next checkpoint of the same buffer replaces in place.
    auto later = fixture.dirtyNote();
    later.contents += QStringLiteral("third line\n");
    const auto again = store.checkpoint(later, *first);
    QVERIFY(again.has_value());
    QCOMPARE(*again, *first);
    QCOMPARE(entriesIn(fixture.recovery), 1);
    QCOMPARE(present(loadOptional(store, *first)), later);
    QCOMPARE(modeOf(file), mode_t{0600});

    // A scratch buffer has no path and no base revision.
    const omanotes::BufferRecovery scratch{std::nullopt, QStringLiteral("just a thought\n"),
                                           std::nullopt};
    const auto scratchId = store.checkpoint(scratch);
    QVERIFY(scratchId.has_value());
    QVERIFY(*scratchId != *first);
    QCOMPARE(present(loadOptional(store, *scratchId)), scratch);
    QCOMPARE(entriesIn(fixture.recovery), 2);
}

void RecoveryStoreTest::survivesARestartThroughTheSnapshotReference() {
    // Crash simulation: one process checkpoints and records the id in the
    // snapshot; a brand-new process reads the snapshot, follows the id, and
    // gets the text back byte for byte.
    Fixture fixture;
    QVERIFY(fixture.valid());
    const auto sessions = pathFor(fixture.stateDir.path()) / "omanotes" / "sessions";
    omanotes::RecoveryId id;
    {
        const omanotes::SessionStore sessionStore(sessions);
        const omanotes::RecoveryStore recoveryStore(
            omanotes::RecoveryStore::directoryBeside(sessionStore.directoryFor(fixture.root())));
        const auto checkpointed = recoveryStore.checkpoint(fixture.dirtyNote());
        QVERIFY(checkpointed.has_value());
        id = *checkpointed;

        omanotes::SessionSnapshot snapshot;
        snapshot.workspaceRoot = fixture.root().path();
        snapshot.window = {1100, 720, false};
        omanotes::BufferSnapshot buffer;
        buffer.id = QUuid::createUuid();
        buffer.path = "ideas.md";
        buffer.modified = true;
        buffer.recovery = id;
        snapshot.buffers = {buffer};
        QVERIFY(sessionStore.save(snapshot).has_value());
        // No clean close, no remove(): the process simply ends here.
    }
    {
        const omanotes::SessionStore sessionStore(sessions);
        const auto snapshot = sessionStore.load(fixture.root());
        QVERIFY(snapshot.has_value());
        const auto restored = snapshot->value_or(omanotes::SessionSnapshot{});
        QVERIFY(!restored.buffers.empty());
        QCOMPARE(restored.dirtyBufferCount(), std::size_t{1});
        QCOMPARE(restored.buffers.front().recovery, std::optional<omanotes::RecoveryId>(id));

        const omanotes::RecoveryStore recoveryStore(
            omanotes::RecoveryStore::directoryBeside(sessionStore.directoryFor(fixture.root())));
        const auto text = loadOptional(recoveryStore, id);
        QVERIFY(text.has_value());
        QCOMPARE(present(text).contents, fixture.dirtyNote().contents);
        // The note on disk was never touched by any of this.
        QCOMPARE(readFile(fixture.note()), fixture.savedBytes);
    }
}

void RecoveryStoreTest::saveAndDiscardRemoveRecords() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore store(fixture.recovery);
    const auto saved = store.checkpoint(fixture.dirtyNote());
    const auto discarded = store.checkpoint({std::nullopt, QStringLiteral("scratch\n"), {}});
    QVERIFY(saved.has_value() && discarded.has_value());
    QCOMPARE(entriesIn(fixture.recovery), 2);

    // Normal save: the record goes.
    QVERIFY(store.remove(*saved).has_value());
    QVERIFY(!loadOptional(store, *saved).has_value());
    QCOMPARE(entriesIn(fixture.recovery), 1);
    // Discarded buffer: the record goes.
    QVERIFY(store.remove(*discarded).has_value());
    QCOMPARE(entriesIn(fixture.recovery), 0);
    // Removing twice is not an error; there is nothing to protect.
    QVERIFY(store.remove(*discarded).has_value());
    QVERIFY(store.remove(QUuid::createUuid()).has_value());
}

void RecoveryStoreTest::removesOrphansButKeepsLiveRecords() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore store(fixture.recovery);
    std::vector<omanotes::RecoveryId> ids;
    for (int index = 0; index < 5; ++index) {
        const auto id = store.checkpoint(
            {std::nullopt, QStringLiteral("buffer %1\n").arg(index), std::nullopt});
        QVERIFY(id.has_value());
        ids.push_back(*id);
    }
    // Something that is not a record sits in the directory too; not our business.
    writeFile(fixture.recovery / "notes.txt", "leave me");
    const std::vector<omanotes::RecoveryId> live{ids[1], ids[3]};

    const auto removed = store.removeAllExcept(live);
    QVERIFY(removed.has_value());
    QCOMPARE(*removed, std::size_t{3});
    const auto remaining = store.list();
    QVERIFY(remaining.has_value());
    QCOMPARE(remaining->size(), std::size_t{2});
    QVERIFY(std::ranges::is_permutation(*remaining, live));
    QVERIFY(std::filesystem::exists(fixture.recovery / "notes.txt"));
    QVERIFY(loadOptional(store, ids[1]).has_value());
    QVERIFY(!loadOptional(store, ids[0]).has_value());

    // An empty live set on a directory that never existed is fine too.
    const omanotes::RecoveryStore untouched(fixture.recovery / "never");
    const auto nothing = untouched.removeAllExcept({});
    QVERIFY(nothing.has_value());
    QCOMPARE(*nothing, std::size_t{0});
}

void RecoveryStoreTest::previousRecordSurvivesEveryInjectedFailure_data() {
    QTest::addColumn<omanotes::AtomicWriteFaults>("faults");
    omanotes::AtomicWriteFaults partial;
    partial.failWriteAfterBytes = 12;
    QTest::newRow("partial write, then ENOSPC") << partial;
    omanotes::AtomicWriteFaults sync;
    sync.failSync = true;
    QTest::newRow("flush fails") << sync;
    omanotes::AtomicWriteFaults rename;
    rename.failRename = true;
    QTest::newRow("rename fails") << rename;
}

void RecoveryStoreTest::previousRecordSurvivesEveryInjectedFailure() {
    QFETCH(omanotes::AtomicWriteFaults, faults);
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore healthy(fixture.recovery);
    const auto id = healthy.checkpoint(fixture.dirtyNote());
    QVERIFY(id.has_value());
    const auto file =
        fixture.recovery / (id->toString(QUuid::WithoutBraces).toStdString() + ".json");
    const auto before = readFile(file);

    const omanotes::RecoveryStore failing(fixture.recovery, &faults);
    auto newer = fixture.dirtyNote();
    newer.contents += QStringLiteral("more\n");
    const auto attempt = failing.checkpoint(newer, *id);
    QVERIFY(!attempt.has_value());
    QCOMPARE(attempt.error().code, omanotes::RecoveryErrorCode::StoreFailed);

    QCOMPARE(readFile(file), before);
    QCOMPARE(entriesIn(fixture.recovery), 1);
    QCOMPARE(present(loadOptional(healthy, *id)), fixture.dirtyNote());
}

void RecoveryStoreTest::refusesOversizedBuffersWithoutWriting() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore store(fixture.recovery);
    omanotes::BufferRecovery huge{std::nullopt, {}, std::nullopt};
    huge.contents =
        QString(static_cast<qsizetype>(omanotes::kRecoveryMaxContentBytes) + 1, QLatin1Char('x'));
    const auto refused = store.checkpoint(huge);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, omanotes::RecoveryErrorCode::Oversized);
    QVERIFY(!refused.error().message.isEmpty());
    QVERIFY(!std::filesystem::exists(fixture.recovery));

    // Exactly at the cap is fine.
    huge.contents.chop(1);
    const auto accepted = store.checkpoint(huge);
    QVERIFY2(accepted.has_value(), qPrintable(accepted ? QString() : accepted.error().describe()));
    QCOMPARE(present(loadOptional(store, *accepted)).contents.size(), huge.contents.size());
}

void RecoveryStoreTest::idsNeverBecomePaths() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore store(fixture.recovery);

    const auto nullLoad = store.load(QUuid());
    QVERIFY(!nullLoad.has_value());
    QCOMPARE(nullLoad.error().code, omanotes::RecoveryErrorCode::InvalidId);
    QVERIFY(!store.remove(QUuid()).has_value());
    QVERIFY(!store.checkpoint(fixture.dirtyNote(), QUuid()).has_value());

    // A record's path field is validated exactly like a snapshot path.
    const auto escaping = store.checkpoint({"../../.ssh/id_ed25519", QStringLiteral("x"), {}});
    QVERIFY(!escaping.has_value());
    QCOMPARE(escaping.error().code, omanotes::RecoveryErrorCode::InvalidField);
    const auto absolute = store.checkpoint({"/etc/passwd", QStringLiteral("x"), {}});
    QVERIFY(!absolute.has_value());
    QVERIFY(!std::filesystem::exists(fixture.recovery));

    // Only well-formed <uuid>.json names are records; anything else is ignored.
    std::filesystem::create_directories(fixture.recovery);
    writeFile(fixture.recovery / "..json", "{}");
    writeFile(fixture.recovery / "not-a-uuid.json", "{}");
    writeFile(fixture.recovery / "{0d5b7d8e-1f6a-4c3b-9e2d-8a7f6b5c4d3e}.json", "{}");
    const auto listed = store.list();
    QVERIFY(listed.has_value());
    QVERIFY(listed->empty());
}

void RecoveryStoreTest::refusesSymlinksAndLeavesTargetsAlone() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const auto victim = pathFor(elsewhere.path()) / "victim.json";
    writeFile(victim, "keep me");
    const omanotes::RecoveryStore store(fixture.recovery);
    std::filesystem::create_directories(fixture.recovery);
    const auto id = QUuid::createUuid();
    const auto file =
        fixture.recovery / (id.toString(QUuid::WithoutBraces).toStdString() + ".json");
    std::filesystem::create_symlink(victim, file);

    QVERIFY(!store.checkpoint(fixture.dirtyNote(), id).has_value());
    QVERIFY(!store.load(id).has_value());
    QVERIFY(!store.remove(id).has_value());
    QCOMPARE(readFile(victim), QByteArray("keep me"));
    QVERIFY(std::filesystem::is_symlink(file));
    const auto listed = store.list();
    QVERIFY(listed.has_value());
    QVERIFY(listed->empty());

    // A symlinked recovery directory is refused before anything is written.
    std::filesystem::remove(file);
    std::filesystem::remove(fixture.recovery);
    std::filesystem::create_symlink(pathFor(elsewhere.path()), fixture.recovery);
    QVERIFY(!store.checkpoint(fixture.dirtyNote()).has_value());
    QVERIFY(!store.list().has_value());
    QCOMPARE(entriesIn(pathFor(elsewhere.path())), 1);
}

void RecoveryStoreTest::refusesBadRecordsAndLeavesThemAsFound() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore store(fixture.recovery);
    std::filesystem::create_directories(fixture.recovery);
    struct Case {
        QByteArray bytes;
        omanotes::RecoveryErrorCode code;
    };
    const std::vector<Case> cases{
        {"{\"version\": 1, \"contents\": \"trunc", omanotes::RecoveryErrorCode::Malformed},
        {"{\"version\": 2, \"contents\": \"x\", \"cipher\": \"aes\"}\n",
         omanotes::RecoveryErrorCode::FutureVersion},
        {"{\"version\": 0, \"contents\": \"x\"}\n",
         omanotes::RecoveryErrorCode::UnsupportedVersion},
        {"{\"version\": 1, \"contents\": \"x\", \"extra\": 1}\n",
         omanotes::RecoveryErrorCode::InvalidField},
        {"{\"version\": 1, \"contents\": \"x\", \"path\": \"../out.md\"}\n",
         omanotes::RecoveryErrorCode::InvalidField},
        {"{\"version\": 1, \"contents\": \"x\", \"baseDigest\": \"zz\"}\n",
         omanotes::RecoveryErrorCode::InvalidField},
        {"{\"version\": 1}\n", omanotes::RecoveryErrorCode::InvalidField},
    };
    for (const auto& item : cases) {
        const auto id = QUuid::createUuid();
        const auto file =
            fixture.recovery / (id.toString(QUuid::WithoutBraces).toStdString() + ".json");
        writeFile(file, item.bytes);
        const auto loaded = store.load(id);
        QVERIFY2(!loaded.has_value(), item.bytes.constData());
        QCOMPARE(loaded.error().code, item.code);
        QVERIFY(loaded.error().location.contains(QString::fromStdString(file.filename().string())));
        QCOMPARE(readFile(file), item.bytes);
    }
}

void RecoveryStoreTest::plansRestoreAgainstTheLiveDisk() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const auto& root = fixture.root();
    using omanotes::RecoveryTarget;

    const auto scratch =
        omanotes::planRecovery({std::nullopt, QStringLiteral("x"), std::nullopt}, root);
    QVERIFY(scratch.has_value());
    QCOMPARE(scratch->target, RecoveryTarget::Scratch);
    QVERIFY(!scratch->resolved.has_value());

    const auto unchanged = omanotes::planRecovery(fixture.dirtyNote(), root);
    QVERIFY(unchanged.has_value());
    QCOMPARE(unchanged->target, RecoveryTarget::File);
    QCOMPARE(unchanged->resolved, std::optional(fixture.note()));

    writeFile(fixture.note(), "# Ideas\n\nsomeone else's line\n");
    const auto changed = omanotes::planRecovery(fixture.dirtyNote(), root);
    QVERIFY(changed.has_value());
    QCOMPARE(changed->target, RecoveryTarget::FileChangedOnDisk);

    std::filesystem::remove(fixture.note());
    const auto gone = omanotes::planRecovery(fixture.dirtyNote(), root);
    QVERIFY(gone.has_value());
    QCOMPARE(gone->target, RecoveryTarget::FileMissing);
    QCOMPARE(gone->resolved, std::optional(fixture.note()));

    // Named but never saved: no base revision, nothing on disk.
    const omanotes::BufferRecovery fresh{"drafts/new.md", QStringLiteral("new\n"), std::nullopt};
    const auto neverSaved = omanotes::planRecovery(fresh, root);
    QVERIFY(neverSaved.has_value());
    QCOMPARE(neverSaved->target, RecoveryTarget::FileMissing);
    QCOMPARE(neverSaved->resolved, std::optional(root.path() / "drafts" / "new.md"));

    // ...and if someone created it meanwhile, that is a conflict, not a claim.
    std::filesystem::create_directories(root.path() / "drafts");
    writeFile(root.path() / "drafts" / "new.md", "theirs\n");
    const auto raced = omanotes::planRecovery(fresh, root);
    QVERIFY(raced.has_value());
    QCOMPARE(raced->target, RecoveryTarget::FileChangedOnDisk);

    // A path that now leaves the root through a symlink is refused outright.
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    writeFile(pathFor(outside.path()) / "secret.md", "secret\n");
    std::filesystem::create_symlink(pathFor(outside.path()) / "secret.md",
                                    root.path() / "linked.md");
    const auto escaped =
        omanotes::planRecovery({"linked.md", QStringLiteral("x"), std::nullopt}, root);
    QVERIFY(!escaped.has_value());
    QCOMPARE(escaped.error().code, omanotes::WorkspaceErrorCode::OutsideRoot);

    // A missing file under a symlinked directory that leaves the root: also refused.
    std::filesystem::create_directory_symlink(pathFor(outside.path()), root.path() / "shared");
    const auto escapedDirectory =
        omanotes::planRecovery({"shared/todo.md", QStringLiteral("x"), std::nullopt}, root);
    QVERIFY(!escapedDirectory.has_value());
    QCOMPARE(escapedDirectory.error().code, omanotes::WorkspaceErrorCode::OutsideRoot);
}

void RecoveryStoreTest::restoreNeverWritesTheWorkspace() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::RecoveryStore store(fixture.recovery);
    const auto id = store.checkpoint(fixture.dirtyNote());
    QVERIFY(id.has_value());
    const auto before = readFile(fixture.note());
    const auto workspaceEntries = entriesIn(fixture.root().path());

    const auto loaded = loadOptional(store, *id);
    QVERIFY(loaded.has_value());
    const auto plan = omanotes::planRecovery(present(loaded), fixture.root());
    QVERIFY(plan.has_value());

    QCOMPARE(readFile(fixture.note()), before);
    QCOMPARE(entriesIn(fixture.root().path()), workspaceEntries);
    // The recovered text differs from disk and stays in the record until the
    // user saves or discards; loading did not remove it.
    QVERIFY(present(loaded).contents.toUtf8() != before);
    QVERIFY(loadOptional(store, *id).has_value());
}

QTEST_GUILESS_MAIN(RecoveryStoreTest)
#include "recovery_store_test.moc"
