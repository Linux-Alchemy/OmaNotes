#include "workspace/file_watcher.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <filesystem>

namespace {

constexpr std::chrono::milliseconds kQuiet{60};
constexpr int kLongerThanQuiet = 250;

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class FileWatcherTest final : public QObject {
    Q_OBJECT

  private slots:
    void coalescesRapidWritesIntoOneReport();
    void detectsRenameReplaceAndKeepsWatchingAfterwards();
    void detectsRemoval();
    void ignoresNeighboursAndUnwatchedFiles();
};

void FileWatcherTest::coalescesRapidWritesIntoOneReport() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto note = pathFor(temporary.path()) / "note.md";
    writeFile(note, "0\n");

    omanotes::FileWatcher watcher(kQuiet);
    QSignalSpy reports(&watcher, &omanotes::FileWatcher::fileChanged);
    watcher.watch(note);
    QVERIFY(watcher.isWatching(note));

    // An agent hammering a note: twenty writes with no pause between them.
    for (int round = 1; round <= 20; ++round) {
        writeFile(note, QByteArray::number(round) + "\n");
    }

    QTRY_COMPARE(reports.count(), 1);
    QTest::qWait(kLongerThanQuiet);
    QCOMPARE(reports.count(), 1);
    QCOMPARE(reports.first().first().value<std::filesystem::path>(), note);
}

void FileWatcherTest::detectsRenameReplaceAndKeepsWatchingAfterwards() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto note = root / "note.md";
    writeFile(note, "original\n");

    omanotes::FileWatcher watcher(kQuiet);
    QSignalSpy reports(&watcher, &omanotes::FileWatcher::fileChanged);
    watcher.watch(note);

    // Neovim's default save: write a sibling, then rename it over the note.
    const auto staging = root / "note.md.tmp";
    writeFile(staging, "replaced\n");
    std::filesystem::rename(staging, note);
    QTRY_COMPARE(reports.count(), 1);

    // The new inode must be watched too, or the next in-place write would
    // vanish.
    QTest::qWait(kLongerThanQuiet);
    writeFile(note, "edited in place after replace\n");
    QTRY_COMPARE(reports.count(), 2);
}

void FileWatcherTest::detectsRemoval() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto note = pathFor(temporary.path()) / "note.md";
    writeFile(note, "doomed\n");

    omanotes::FileWatcher watcher(kQuiet);
    QSignalSpy reports(&watcher, &omanotes::FileWatcher::fileChanged);
    watcher.watch(note);

    QVERIFY(std::filesystem::remove(note));
    QTRY_COMPARE(reports.count(), 1);
    QCOMPARE(reports.first().first().value<std::filesystem::path>(), note);
}

void FileWatcherTest::ignoresNeighboursAndUnwatchedFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto note = root / "note.md";
    const auto neighbour = root / "other.md";
    writeFile(note, "mine\n");
    writeFile(neighbour, "theirs\n");

    omanotes::FileWatcher watcher(kQuiet);
    QSignalSpy reports(&watcher, &omanotes::FileWatcher::fileChanged);
    watcher.watch(note);

    writeFile(neighbour, "theirs, changed\n");
    QTest::qWait(kLongerThanQuiet);
    QCOMPARE(reports.count(), 0);

    watcher.unwatch(note);
    QVERIFY(!watcher.isWatching(note));
    writeFile(note, "mine, changed after unwatch\n");
    QTest::qWait(kLongerThanQuiet);
    QCOMPARE(reports.count(), 0);
}

QTEST_MAIN(FileWatcherTest)
#include "file_watcher_test.moc"
