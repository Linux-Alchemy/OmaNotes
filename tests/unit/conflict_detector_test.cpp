#include "persistence/conflict_detector.hpp"
#include "persistence/note_reader.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

using omanotes::classifyExternalChange;
using omanotes::DiskRevision;
using omanotes::ExternalChangeAction;
using omanotes::SavedRevision;

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class ConflictDetectorTest final : public QObject {
    Q_OBJECT

  private slots:
    void identicalContentIsUnchangedRegardlessOfModifiedFlag();
    void changedDiskReloadsOnlyACleanBuffer();
    void missingFileIsReportedNotReloaded();
    void unreadableFileNeverReplacesTheBuffer();
    void readsRevisionsFromDisk();
    void touchWithoutContentChangeIsUnchanged();
    void symlinkedOrOversizedFileIsUnreadableNotReloaded();
};

void ConflictDetectorTest::identicalContentIsUnchangedRegardlessOfModifiedFlag() {
    const auto known = SavedRevision::of("# Note\n");
    const DiskRevision same{DiskRevision::State::Present, known.contentHash};

    QCOMPARE(classifyExternalChange(known, same, false), ExternalChangeAction::Unchanged);
    QCOMPARE(classifyExternalChange(known, same, true), ExternalChangeAction::Unchanged);
}

void ConflictDetectorTest::changedDiskReloadsOnlyACleanBuffer() {
    const auto known = SavedRevision::of("# Note\n");
    const DiskRevision changed{DiskRevision::State::Present,
                               SavedRevision::of("# Note\n\nAn agent wrote this.\n").contentHash};

    QCOMPARE(classifyExternalChange(known, changed, false), ExternalChangeAction::ReloadClean);
    QCOMPARE(classifyExternalChange(known, changed, true), ExternalChangeAction::PromptConflict);
}

void ConflictDetectorTest::missingFileIsReportedNotReloaded() {
    const auto known = SavedRevision::of("# Note\n");
    const DiskRevision gone{DiskRevision::State::Missing, {}};

    QCOMPARE(classifyExternalChange(known, gone, false), ExternalChangeAction::FileRemoved);
    QCOMPARE(classifyExternalChange(known, gone, true), ExternalChangeAction::FileRemoved);
}

void ConflictDetectorTest::unreadableFileNeverReplacesTheBuffer() {
    const auto known = SavedRevision::of("# Note\n");
    const DiskRevision unreadable{DiskRevision::State::Unreadable, {}};

    QCOMPARE(classifyExternalChange(known, unreadable, false),
             ExternalChangeAction::PromptConflict);
    QCOMPARE(classifyExternalChange(known, unreadable, true), ExternalChangeAction::PromptConflict);
}

void ConflictDetectorTest::readsRevisionsFromDisk() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto note = pathFor(temporary.path()) / "note.md";

    QCOMPARE(DiskRevision::read(note).state, DiskRevision::State::Missing);

    writeFile(note, "# Note\n");
    const auto present = DiskRevision::read(note);
    QCOMPARE(present.state, DiskRevision::State::Present);
    QCOMPARE(present.contentHash, SavedRevision::of("# Note\n").contentHash);

    writeFile(note, "# Different\n");
    QVERIFY(DiskRevision::read(note).contentHash != present.contentHash);
}

void ConflictDetectorTest::touchWithoutContentChangeIsUnchanged() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto note = pathFor(temporary.path()) / "note.md";
    writeFile(note, "# Note\n");
    const auto known = SavedRevision::of("# Note\n");

    // A rewrite of identical bytes changes the timestamp and nothing else;
    // that must not count as an external change.
    writeFile(note, "# Note\n");
    QCOMPARE(classifyExternalChange(known, DiskRevision::read(note), true),
             ExternalChangeAction::Unchanged);
}

void ConflictDetectorTest::symlinkedOrOversizedFileIsUnreadableNotReloaded() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto known = SavedRevision::of("# Note\n");

    // A note swapped for a symlink after it was opened is not followed by
    // the hash either: the disk is "unreadable", so a clean buffer is kept
    // and the user is told, rather than the link's target being loaded.
    writeFile(root / "target.md", "# Elsewhere\n");
    std::filesystem::create_symlink(root / "target.md", root / "note.md");
    const auto linked = DiskRevision::read(root / "note.md");
    QCOMPARE(linked.state, DiskRevision::State::Unreadable);
    QCOMPARE(classifyExternalChange(known, linked, false), ExternalChangeAction::PromptConflict);

    // Past the note limit the same applies: nothing is read into memory.
    writeFile(root / "huge.md",
              QByteArray(static_cast<qsizetype>(omanotes::kNoteMaxBytes) + 1, 'x'));
    const auto huge = DiskRevision::read(root / "huge.md");
    QCOMPARE(huge.state, DiskRevision::State::Unreadable);
    QCOMPARE(classifyExternalChange(known, huge, false), ExternalChangeAction::PromptConflict);
}

QTEST_MAIN(ConflictDetectorTest)
#include "conflict_detector_test.moc"
