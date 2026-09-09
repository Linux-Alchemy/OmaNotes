#include "session/session_store.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <utime.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <thread>
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

int temporariesIn(const std::filesystem::path& directory) {
    int found = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().filename().string().starts_with(omanotes::atomicTemporaryPrefix())) {
            ++found;
        }
    }
    return found;
}

omanotes::SessionSnapshot snapshotFor(const omanotes::WorkspaceRoot& root, int marker) {
    omanotes::SessionSnapshot snapshot;
    snapshot.workspaceRoot = root.path();
    snapshot.window = {1000 + marker, 700, false};
    snapshot.sidebar = {true, 240, std::nullopt};
    omanotes::BufferSnapshot buffer;
    buffer.id = QUuid::createUuid();
    buffer.path = "note.md";
    buffer.cursor = {marker, 0};
    snapshot.buffers = {buffer};
    snapshot.activeBuffer = buffer.id;
    return snapshot;
}

/// A workspace and a private state directory, both disposable.
struct Fixture {
    QTemporaryDir workspaceDir;
    QTemporaryDir stateDir;
    std::filesystem::path sessions;
    std::expected<omanotes::WorkspaceRoot, omanotes::WorkspaceError> resolved;

    Fixture()
        : sessions(pathFor(stateDir.path()) / "omanotes" / "sessions"),
          resolved(omanotes::WorkspaceRoot::resolve(pathFor(workspaceDir.path()))) {}
    [[nodiscard]] bool valid() const {
        return workspaceDir.isValid() && stateDir.isValid() && resolved.has_value();
    }
    [[nodiscard]] const omanotes::WorkspaceRoot& root() const { return *resolved; }
};

/// Flatten a load result for assertions: absent on error or on no session.
std::optional<omanotes::SessionSnapshot> loadOptional(const omanotes::SessionStore& store,
                                                      const omanotes::WorkspaceRoot& root) {
    const auto loaded = store.load(root);
    return loaded.has_value() ? *loaded : std::nullopt;
}

omanotes::SessionSnapshot present(const std::optional<omanotes::SessionSnapshot>& snapshot) {
    return snapshot.value_or(omanotes::SessionSnapshot{});
}

} // namespace

class SessionStoreTest final : public QObject {
    Q_OBJECT

  private slots:
    void derivesAStableOpaqueWorkspaceId();
    void loadsNothingWhenNoSessionExists();
    void savesOwnerOnlyAndRoundTrips();
    void tightensDirectoriesLeftTooOpen();
    void previousSnapshotSurvivesEveryInjectedFailure_data();
    void previousSnapshotSurvivesEveryInjectedFailure();
    void replacesCorruptPriorStateAndNeverRepairsItOnLoad();
    void removesStaleTemporariesButKeepsYoungOnes();
    void refusesSymlinksInTheStore();
    void refusesASnapshotWrittenForAnotherRoot();
    void concurrentSaversNeverProduceAPartialFile();
};

void SessionStoreTest::derivesAStableOpaqueWorkspaceId() {
    const std::filesystem::path first("/home/matt/notes");
    const std::filesystem::path second("/home/matt/notes-archive");
    const auto id = omanotes::SessionStore::workspaceId(first);
    QCOMPARE(id.size(), std::size_t{32});
    QVERIFY(std::ranges::all_of(id, [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c)) &&
               !std::isupper(static_cast<unsigned char>(c));
    }));
    QCOMPARE(omanotes::SessionStore::workspaceId(first), id);
    QVERIFY(omanotes::SessionStore::workspaceId(second) != id);
    QVERIFY(!QString::fromStdString(id).contains(QStringLiteral("notes")));
    // Pinned: changing the derivation orphans every existing session (ADR 0011).
    const auto expected =
        QCryptographicHash::hash(QByteArray("/home/matt/notes"), QCryptographicHash::Sha256)
            .left(16)
            .toHex()
            .toStdString();
    QCOMPARE(id, expected);
}

void SessionStoreTest::loadsNothingWhenNoSessionExists() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::SessionStore store(fixture.sessions);
    const auto loaded = store.load(fixture.root());
    QVERIFY(loaded.has_value());
    QVERIFY(!loaded->has_value());
    // Loading creates nothing.
    QVERIFY(!std::filesystem::exists(fixture.sessions));
}

void SessionStoreTest::savesOwnerOnlyAndRoundTrips() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const mode_t previousMask = ::umask(0); // the store must not rely on the umask
    const omanotes::SessionStore store(fixture.sessions);
    const auto snapshot = snapshotFor(fixture.root(), 1);
    const auto saved = store.save(snapshot);
    ::umask(previousMask);
    QVERIFY2(saved.has_value(), qPrintable(saved ? QString() : saved.error().describe()));

    const auto file = store.fileFor(fixture.root());
    QCOMPARE(file.parent_path().parent_path(), fixture.sessions);
    QCOMPARE(modeOf(file), mode_t{0600});
    QCOMPARE(modeOf(file.parent_path()), mode_t{0700});
    QCOMPARE(modeOf(fixture.sessions), mode_t{0700});
    QCOMPARE(modeOf(fixture.sessions.parent_path()), mode_t{0700});
    QCOMPARE(temporariesIn(file.parent_path()), 0);
    QCOMPARE(readFile(file), omanotes::serializeSessionSnapshot(snapshot));

    const auto loaded = loadOptional(store, fixture.root());
    QVERIFY(loaded.has_value());
    QCOMPARE(present(loaded), snapshot);

    // A second save replaces, and the file is still a regular owner-only file.
    const auto second = snapshotFor(fixture.root(), 2);
    QVERIFY(store.save(second).has_value());
    QCOMPARE(readFile(file), omanotes::serializeSessionSnapshot(second));
    QCOMPARE(modeOf(file), mode_t{0600});
    QCOMPARE(temporariesIn(file.parent_path()), 0);
}

void SessionStoreTest::tightensDirectoriesLeftTooOpen() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    std::filesystem::create_directories(fixture.sessions);
    QCOMPARE(::chmod(fixture.sessions.c_str(), 0755), 0);
    QCOMPARE(::chmod(fixture.sessions.parent_path().c_str(), 0755), 0);
    const omanotes::SessionStore store(fixture.sessions);
    QVERIFY(store.save(snapshotFor(fixture.root(), 1)).has_value());
    QCOMPARE(modeOf(fixture.sessions), mode_t{0700});
    QCOMPARE(modeOf(fixture.sessions.parent_path()), mode_t{0700});
}

void SessionStoreTest::previousSnapshotSurvivesEveryInjectedFailure_data() {
    QTest::addColumn<omanotes::AtomicWriteFaults>("faults");
    omanotes::AtomicWriteFaults partial;
    partial.failWriteAfterBytes = 40;
    QTest::newRow("partial write, then ENOSPC") << partial;
    omanotes::AtomicWriteFaults full;
    full.failWriteAfterBytes = 0;
    QTest::newRow("full disk from the first byte") << full;
    omanotes::AtomicWriteFaults io;
    io.failWriteAfterBytes = 10;
    io.writeErrno = EIO;
    QTest::newRow("I/O error mid-write") << io;
    omanotes::AtomicWriteFaults sync;
    sync.failSync = true;
    QTest::newRow("flush fails") << sync;
    omanotes::AtomicWriteFaults rename;
    rename.failRename = true;
    QTest::newRow("rename fails") << rename;
}

void SessionStoreTest::previousSnapshotSurvivesEveryInjectedFailure() {
    QFETCH(omanotes::AtomicWriteFaults, faults);
    Fixture fixture;
    QVERIFY(fixture.valid());

    const omanotes::SessionStore healthy(fixture.sessions);
    const auto original = snapshotFor(fixture.root(), 1);
    QVERIFY(healthy.save(original).has_value());
    const auto file = healthy.fileFor(fixture.root());
    const auto before = readFile(file);

    const omanotes::SessionStore failing(fixture.sessions, &faults);
    const auto attempt = failing.save(snapshotFor(fixture.root(), 2));
    QVERIFY(!attempt.has_value());
    QCOMPARE(attempt.error().code, omanotes::SessionErrorCode::StoreFailed);
    QVERIFY(!attempt.error().message.isEmpty());

    QCOMPARE(readFile(file), before);
    QCOMPARE(modeOf(file), mode_t{0600});
    QCOMPARE(temporariesIn(file.parent_path()), 0);
    const auto loaded = loadOptional(healthy, fixture.root());
    QVERIFY(loaded.has_value());
    QCOMPARE(present(loaded), original);
}

void SessionStoreTest::replacesCorruptPriorStateAndNeverRepairsItOnLoad() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::SessionStore store(fixture.sessions);
    const auto file = store.fileFor(fixture.root());
    std::filesystem::create_directories(file.parent_path());
    const QByteArray garbage("{\"version\": 1, \"workspaceRoot\": \"/nope\", \"buffers\": [");
    writeFile(file, garbage);

    const auto loaded = store.load(fixture.root());
    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error().code, omanotes::SessionErrorCode::Malformed);
    QVERIFY(loaded.error().location.contains(QStringLiteral("session.json")));
    QCOMPARE(readFile(file), garbage);

    const auto fresh = snapshotFor(fixture.root(), 3);
    QVERIFY(store.save(fresh).has_value());
    const auto again = loadOptional(store, fixture.root());
    QVERIFY(again.has_value());
    QCOMPARE(present(again), fresh);
}

void SessionStoreTest::removesStaleTemporariesButKeepsYoungOnes() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::SessionStore store(fixture.sessions);
    const auto directory = store.directoryFor(fixture.root());
    std::filesystem::create_directories(directory);
    const auto prefix = omanotes::atomicTemporaryPrefix() + "session.json-";
    const auto stale = directory / (prefix + "stale0");
    const auto young = directory / (prefix + "young0");
    const auto unrelated = directory / (omanotes::atomicTemporaryPrefix() + "other-abc");
    writeFile(stale, "partial");
    writeFile(young, "partial");
    writeFile(unrelated, "partial");
    const auto hourAgo = static_cast<time_t>(std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() - std::chrono::hours(1)));
    const utimbuf old{hourAgo, hourAgo};
    QCOMPARE(::utime(stale.c_str(), &old), 0);
    QCOMPARE(::utime(unrelated.c_str(), &old), 0);

    QVERIFY(store.save(snapshotFor(fixture.root(), 1)).has_value());
    QVERIFY(!std::filesystem::exists(stale));
    QVERIFY(std::filesystem::exists(young));
    // Only this store's own session temporaries are its business.
    QVERIFY(std::filesystem::exists(unrelated));
}

void SessionStoreTest::refusesSymlinksInTheStore() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const auto target = pathFor(elsewhere.path()) / "victim.json";
    writeFile(target, "keep me");

    const omanotes::SessionStore store(fixture.sessions);
    const auto file = store.fileFor(fixture.root());
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::create_symlink(target, file);

    const auto saved = store.save(snapshotFor(fixture.root(), 1));
    QVERIFY(!saved.has_value());
    QCOMPARE(saved.error().code, omanotes::SessionErrorCode::StoreFailed);
    QCOMPARE(readFile(target), QByteArray("keep me"));
    QVERIFY(std::filesystem::is_symlink(file));

    const auto loaded = store.load(fixture.root());
    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error().code, omanotes::SessionErrorCode::StoreFailed);

    // A symlinked workspace directory is refused too, before anything is written.
    std::filesystem::remove(file);
    std::filesystem::remove(file.parent_path());
    std::filesystem::create_symlink(pathFor(elsewhere.path()), file.parent_path());
    const auto redirected = store.save(snapshotFor(fixture.root(), 1));
    QVERIFY(!redirected.has_value());
    QVERIFY(!std::filesystem::exists(pathFor(elsewhere.path()) / "session.json"));
}

void SessionStoreTest::refusesASnapshotWrittenForAnotherRoot() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    QTemporaryDir otherDir;
    QVERIFY(otherDir.isValid());
    const auto otherResolved = omanotes::WorkspaceRoot::resolve(pathFor(otherDir.path()));
    QVERIFY(otherResolved.has_value());
    const auto& other = *otherResolved;
    const omanotes::SessionStore store(fixture.sessions);

    // Each root has its own directory; one never sees the other's session.
    QVERIFY(store.save(snapshotFor(fixture.root(), 1)).has_value());
    const auto otherLoad = store.load(other);
    QVERIFY(otherLoad.has_value());
    QVERIFY(!otherLoad->has_value());

    // A file planted in the wrong directory is caught by the root recorded inside.
    const auto planted = store.fileFor(other);
    std::filesystem::create_directories(planted.parent_path());
    writeFile(planted, readFile(store.fileFor(fixture.root())));
    const auto mismatch = store.load(other);
    QVERIFY(!mismatch.has_value());
    QCOMPARE(mismatch.error().code, omanotes::SessionErrorCode::RootMismatch);

    // Saving a snapshot whose root does not resolve is refused, not stored anywhere.
    auto homeless = snapshotFor(fixture.root(), 1);
    homeless.workspaceRoot = "/nonexistent/omanotes/root";
    const auto saved = store.save(homeless);
    QVERIFY(!saved.has_value());
    QCOMPARE(saved.error().code, omanotes::SessionErrorCode::RootMismatch);
}

void SessionStoreTest::concurrentSaversNeverProduceAPartialFile() {
    Fixture fixture;
    QVERIFY(fixture.valid());
    const omanotes::SessionStore store(fixture.sessions);
    QVERIFY(store.save(snapshotFor(fixture.root(), 0)).has_value());

    constexpr int writers = 4;
    constexpr int rounds = 25;
    std::vector<omanotes::SessionSnapshot> candidates;
    for (int writer = 0; writer < writers; ++writer) {
        for (int round = 0; round < rounds; ++round) {
            candidates.push_back(snapshotFor(fixture.root(), 1 + writer * rounds + round));
        }
    }
    std::atomic<int> saveFailures{0};
    std::atomic<int> loadFailures{0};
    std::atomic<int> loads{0};
    std::atomic<bool> stop{false};

    std::thread reader([&] {
        while (!stop.load()) {
            const auto loaded = loadOptional(store, fixture.root());
            if (!loaded.has_value()) {
                ++loadFailures;
                continue;
            }
            ++loads;
            const auto seen = present(loaded);
            const bool known =
                seen.window.width == 1000 ||
                std::ranges::any_of(candidates, [&seen](const auto& c) { return c == seen; });
            if (!known) {
                ++loadFailures;
            }
        }
    });
    std::vector<std::thread> threads;
    threads.reserve(writers);
    for (int writer = 0; writer < writers; ++writer) {
        threads.emplace_back([&, writer] {
            for (int round = 0; round < rounds; ++round) {
                const auto& candidate = candidates[static_cast<std::size_t>(writer) * rounds +
                                                   static_cast<std::size_t>(round)];
                if (!store.save(candidate).has_value()) {
                    ++saveFailures;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    stop.store(true);
    reader.join();

    QCOMPARE(saveFailures.load(), 0);
    QCOMPARE(loadFailures.load(), 0);
    QVERIFY(loads.load() > 0);
    QCOMPARE(temporariesIn(store.directoryFor(fixture.root())), 0);
    const auto final = loadOptional(store, fixture.root());
    QVERIFY(final.has_value());
    const auto last = present(final);
    QVERIFY(std::ranges::any_of(candidates, [&last](const auto& c) { return c == last; }));
}

QTEST_GUILESS_MAIN(SessionStoreTest)
#include "session_store_test.moc"
