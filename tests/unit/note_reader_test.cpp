#include "persistence/note_reader.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

using omanotes::kNoteMaxBytes;
using omanotes::NoteReadErrorCode;
using omanotes::readNoteFile;

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class NoteReaderTest final : public QObject {
    Q_OBJECT

  private slots:
    void readsARegularFileUpToTheLimit();
    void refusesAFileOverTheLimit();
    void refusesASymlinkEvenToAReadableFile();
    void refusesADirectoryAndAPipeWithoutBlocking();
    void reportsMissingAndUnreadableDistinctly();
};

void NoteReaderTest::readsARegularFileUpToTheLimit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto note = pathFor(temporary.path()) / "note.md";
    writeFile(note, "# Note\n");

    const auto small = readNoteFile(note);
    QVERIFY(small.has_value());
    QCOMPARE(*small, QByteArray("# Note\n"));

    // Exactly the limit is still a note; the limit is inclusive.
    const auto exact = pathFor(temporary.path()) / "exact.md";
    writeFile(exact, QByteArray(64, 'x'));
    const auto atLimit = readNoteFile(exact, 64);
    QVERIFY(atLimit.has_value());
    QCOMPARE(atLimit->size(), 64);
    QCOMPARE(kNoteMaxBytes, qint64{16} * 1024 * 1024);
}

void NoteReaderTest::refusesAFileOverTheLimit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto big = pathFor(temporary.path()) / "big.md";
    writeFile(big, QByteArray(65, 'x'));

    const auto refused = readNoteFile(big, 64);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, NoteReadErrorCode::Oversized);
    QVERIFY2(refused.error().message.contains(QStringLiteral("big.md")),
             qPrintable(refused.error().message));
    QVERIFY(refused.error().message.contains(QStringLiteral("larger than")));
}

void NoteReaderTest::refusesASymlinkEvenToAReadableFile() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "target.md", "# Target\n");
    std::filesystem::create_symlink(root / "target.md", root / "link.md");

    // The caller resolves links before reading; by the time a path reaches
    // the reader, a symlink means it was swapped in afterwards.
    const auto refused = readNoteFile(root / "link.md");
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, NoteReadErrorCode::NotRegular);
    QVERIFY(refused.error().message.contains(QStringLiteral("symlink")));
    // The target itself still reads.
    QVERIFY(readNoteFile(root / "target.md").has_value());
}

void NoteReaderTest::refusesADirectoryAndAPipeWithoutBlocking() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    std::filesystem::create_directory(root / "folder.md");
    const auto directory = readNoteFile(root / "folder.md");
    QVERIFY(!directory.has_value());
    QCOMPARE(directory.error().code, NoteReadErrorCode::NotRegular);

    // A pipe with no writer would block a naive open forever; this returns.
    const auto pipe = root / "pipe.md";
    QCOMPARE(::mkfifo(pipe.c_str(), 0600), 0);
    QElapsedTimer clock;
    clock.start();
    const auto refused = readNoteFile(pipe);
    QVERIFY(clock.elapsed() < 2000);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, NoteReadErrorCode::NotRegular);
}

void NoteReaderTest::reportsMissingAndUnreadableDistinctly() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());

    const auto missing = readNoteFile(root / "absent.md");
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, NoteReadErrorCode::Missing);

    if (::geteuid() == 0) {
        QSKIP("root can read anything; the permission case is not observable");
    }
    const auto sealed = root / "sealed.md";
    writeFile(sealed, "# Sealed\n");
    QCOMPARE(::chmod(sealed.c_str(), 0000), 0);
    const auto unreadable = readNoteFile(sealed);
    QCOMPARE(::chmod(sealed.c_str(), 0600), 0);
    QVERIFY(!unreadable.has_value());
    QCOMPARE(unreadable.error().code, NoteReadErrorCode::Unreadable);
    QVERIFY(unreadable.error().message.contains(QStringLiteral("sealed.md")));
}

QTEST_GUILESS_MAIN(NoteReaderTest)
#include "note_reader_test.moc"
