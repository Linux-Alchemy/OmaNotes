#include "persistence/atomic_file_writer.hpp"
#include "workspace/workspace_root.hpp"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

#include <filesystem>

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

QByteArray readFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

/// Any temporary the writer leaves behind would show up here.
int strayTemporaries(const std::filesystem::path& directory) {
    int found = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().filename().string().starts_with(".omanotes-")) {
            ++found;
        }
    }
    return found;
}

} // namespace

class AtomicSaveTest final : public QObject {
    Q_OBJECT

  private slots:
    void writesANewFileAndLeavesNoTemporary();
    void replacesAnExistingFilePreservingItsPermissions();
    void writesThroughASymlinkKeepingTheLink();
    void refusesALinkThatLeavesTheRoot();
    void refusesTargetsOutsideTheRoot();
    void refusesADirectoryTarget();
    void leavesTheOriginalIntactWhenTheWriteCannotStart();
    void refusesToReplaceAFileThatChangedSinceItWasChecked();
};

void AtomicSaveTest::writesANewFileAndLeavesNoTemporary() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# New\n");
    const auto written = writer.write(root / "new.md", QByteArrayView(contents), *workspace);

    QVERIFY(written.has_value());
    QCOMPARE(readFile(root / "new.md"), contents);
    QCOMPARE(strayTemporaries(root), 0);
}

void AtomicSaveTest::replacesAnExistingFilePreservingItsPermissions() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto target = root / "note.md";
    writeFile(target, "# Original\n");
    QCOMPARE(::chmod(target.c_str(), 0640), 0);

    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# Replaced\n");
    const auto written = writer.write(target, QByteArrayView(contents), *workspace);

    QVERIFY(written.has_value());
    QCOMPARE(readFile(target), contents);
    QCOMPARE(strayTemporaries(root), 0);

    struct stat status{};
    QCOMPARE(::stat(target.c_str(), &status), 0);
    QCOMPARE(status.st_mode & 07777, static_cast<decltype(status.st_mode)>(0640));
}

void AtomicSaveTest::writesThroughASymlinkKeepingTheLink() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto real = root / "real.md";
    const auto link = root / "link.md";
    writeFile(real, "# Original\n");

    std::error_code error;
    std::filesystem::create_symlink(real, link, error);
    if (error) {
        QSKIP("The filesystem under test does not support symlinks");
    }

    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# Through the link\n");
    const auto written = writer.write(link, QByteArrayView(contents), *workspace);

    QVERIFY(written.has_value());
    QCOMPARE(*written, real);
    // The link must survive as a link, and the real file must hold the bytes.
    QVERIFY(std::filesystem::is_symlink(std::filesystem::symlink_status(link)));
    QCOMPARE(readFile(real), contents);
    QCOMPARE(strayTemporaries(root), 0);
}

void AtomicSaveTest::refusesALinkThatLeavesTheRoot() {
    QTemporaryDir workspaceDirectory;
    QTemporaryDir elsewhere;
    QVERIFY(workspaceDirectory.isValid());
    QVERIFY(elsewhere.isValid());
    const auto root = std::filesystem::canonical(pathFor(workspaceDirectory.path()));
    const auto outside = std::filesystem::canonical(pathFor(elsewhere.path())) / "outside.md";
    writeFile(outside, "# Outside\n");

    std::error_code error;
    std::filesystem::create_symlink(outside, root / "escape.md", error);
    if (error) {
        QSKIP("The filesystem under test does not support symlinks");
    }

    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# Should not land\n");
    const auto written = writer.write(root / "escape.md", QByteArrayView(contents), *workspace);

    QVERIFY(!written.has_value());
    QCOMPARE(written.error().code, omanotes::SaveErrorCode::OutsideRoot);
    QCOMPARE(readFile(outside), QByteArray("# Outside\n"));
    QCOMPARE(strayTemporaries(root), 0);
}

void AtomicSaveTest::refusesTargetsOutsideTheRoot() {
    QTemporaryDir workspaceDirectory;
    QTemporaryDir elsewhere;
    QVERIFY(workspaceDirectory.isValid());
    QVERIFY(elsewhere.isValid());
    const auto root = std::filesystem::canonical(pathFor(workspaceDirectory.path()));
    const auto outsideDirectory = std::filesystem::canonical(pathFor(elsewhere.path()));

    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# Should not land\n");

    const auto absolute =
        writer.write(outsideDirectory / "stray.md", QByteArrayView(contents), *workspace);
    QVERIFY(!absolute.has_value());
    QCOMPARE(absolute.error().code, omanotes::SaveErrorCode::OutsideRoot);

    const auto traversal =
        writer.write(root / ".." / "stray.md", QByteArrayView(contents), *workspace);
    QVERIFY(!traversal.has_value());
    QCOMPARE(traversal.error().code, omanotes::SaveErrorCode::OutsideRoot);

    QVERIFY(!std::filesystem::exists(outsideDirectory / "stray.md"));
    QCOMPARE(strayTemporaries(root), 0);
}

void AtomicSaveTest::refusesADirectoryTarget() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    std::filesystem::create_directory(root / "folder.md");

    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# Should not land\n");
    const auto written = writer.write(root / "folder.md", QByteArrayView(contents), *workspace);

    QVERIFY(!written.has_value());
    QCOMPARE(written.error().code, omanotes::SaveErrorCode::InvalidTarget);
    QVERIFY(std::filesystem::is_directory(root / "folder.md"));
    QCOMPARE(strayTemporaries(root), 0);
}

void AtomicSaveTest::leavesTheOriginalIntactWhenTheWriteCannotStart() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto directory = root / "locked";
    std::filesystem::create_directory(directory);
    const auto target = directory / "note.md";
    writeFile(target, "# Original\n");

    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    // A read-only directory makes the temporary file impossible to create,
    // which is the point at which the writer must give up without damage.
    QCOMPARE(::chmod(directory.c_str(), 0500), 0);

    const omanotes::AtomicFileWriter writer;
    const QByteArray contents("# Should not land\n");
    const auto written = writer.write(target, QByteArrayView(contents), *workspace);

    const auto restored = ::chmod(directory.c_str(), 0700);

    QVERIFY(!written.has_value());
    QCOMPARE(written.error().code, omanotes::SaveErrorCode::TemporaryFailed);
    QCOMPARE(restored, 0);
    QCOMPARE(readFile(target), QByteArray("# Original\n"));
    QCOMPARE(strayTemporaries(directory), 0);
}

void AtomicSaveTest::refusesToReplaceAFileThatChangedSinceItWasChecked() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto note = root / "note.md";
    writeFile(note, "# Checked\n");
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());
    const omanotes::AtomicFileWriter writer;
    const auto hashOf = [](const QByteArray& bytes) {
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    };

    // The caller compared against "# Checked" and decided to save. Someone
    // else wrote in between. The precondition is re-checked after the new
    // bytes are durable and before the rename, and the replace is refused.
    writeFile(note, "# Someone else\n");
    const auto stale = writer.write(note, QByteArrayView("# Mine\n"), *workspace,
                                    omanotes::WritePrecondition::matches(hashOf("# Checked\n")));
    QVERIFY(!stale.has_value());
    QCOMPARE(stale.error().code, omanotes::SaveErrorCode::ChangedSinceRead);
    QVERIFY(QString::fromStdString(stale.error().message).contains(QStringLiteral("changed")));
    QCOMPARE(readFile(note), QByteArray("# Someone else\n"));
    QCOMPARE(strayTemporaries(root), 0);

    // With the hash the disk actually holds, the same write goes through.
    const auto fresh =
        writer.write(note, QByteArrayView("# Mine\n"), *workspace,
                     omanotes::WritePrecondition::matches(hashOf("# Someone else\n")));
    QVERIFY(fresh.has_value());
    QCOMPARE(readFile(note), QByteArray("# Mine\n"));

    // A save that expected no file finds one: refused, the file kept.
    const auto appeared = writer.write(note, QByteArrayView("# New\n"), *workspace,
                                       omanotes::WritePrecondition::absent());
    QVERIFY(!appeared.has_value());
    QCOMPARE(appeared.error().code, omanotes::SaveErrorCode::ChangedSinceRead);
    QCOMPARE(readFile(note), QByteArray("# Mine\n"));

    // A save that expected the file finds it gone: refused, not recreated.
    std::filesystem::remove(note);
    const auto vanished = writer.write(note, QByteArrayView("# Mine\n"), *workspace,
                                       omanotes::WritePrecondition::matches(hashOf("# Mine\n")));
    QVERIFY(!vanished.has_value());
    QCOMPARE(vanished.error().code, omanotes::SaveErrorCode::ChangedSinceRead);
    QVERIFY(!std::filesystem::exists(note));

    // `:w!` asks for none of this and always replaces.
    writeFile(note, "# Whatever\n");
    const auto forced = writer.write(note, QByteArrayView("# Forced\n"), *workspace,
                                     omanotes::WritePrecondition::any());
    QVERIFY(forced.has_value());
    QCOMPARE(readFile(note), QByteArray("# Forced\n"));
    QCOMPARE(strayTemporaries(root), 0);
}

QTEST_MAIN(AtomicSaveTest)

#include "atomic_save_test.moc"
