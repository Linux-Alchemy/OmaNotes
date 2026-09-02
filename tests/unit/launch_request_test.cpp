#include "app/launch_request.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents = "# note\n") {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

std::expected<omanotes::LaunchRequest, omanotes::LaunchError>
resolve(std::initializer_list<std::string_view> arguments,
        const std::filesystem::path& currentDirectory) {
    const std::vector<std::string_view> copiedArguments(arguments);
    return omanotes::resolveLaunchRequest(std::span<const std::string_view>(copiedArguments),
                                          currentDirectory);
}

} // namespace

class LaunchRequestTest final : public QObject {
    Q_OBJECT

  private slots:
    void resolvesApprovedLaunchTable();
    void parsesFreshWithoutChangingResolution();
    void rejectsInvalidArgumentsAndMissingPaths();
    void canonicalizesRootSymlinksAndRejectsFileEscapes();
    void rejectsUnreadableRootsAndFiles();
};

void LaunchRequestTest::resolvesApprovedLaunchTable() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto notes = root / "Notes";
    const auto projects = root / "projects";
    std::filesystem::create_directories(notes);
    std::filesystem::create_directories(projects);
    writeFile(root / "idea.md");
    writeFile(projects / "nested.md");
    writeFile(notes / "absolute.md");

    const auto noArguments = resolve({}, root);
    QVERIFY(noArguments.has_value());
    QCOMPARE(noArguments->root, std::filesystem::canonical(root));
    QVERIFY(!noArguments->requestedFile.has_value());

    const auto relativeDirectory = resolve({"projects"}, root);
    QVERIFY(relativeDirectory.has_value());
    QCOMPARE(relativeDirectory->root, std::filesystem::canonical(projects));
    QVERIFY(!relativeDirectory->requestedFile.has_value());

    const auto absoluteDirectory = resolve({notes.string()}, root);
    QVERIFY(absoluteDirectory.has_value());
    QCOMPARE(absoluteDirectory->root, std::filesystem::canonical(notes));

    const auto relativeFile = resolve({"idea.md"}, root);
    QVERIFY(relativeFile.has_value());
    QVERIFY(relativeFile->requestedFile.has_value());
    QCOMPARE(relativeFile->root, std::filesystem::canonical(root));
    QCOMPARE(relativeFile->requestedFile.value_or(std::filesystem::path{}),
             std::filesystem::canonical(root / "idea.md"));

    const auto nestedRelativeFile = resolve({"projects/nested.md"}, root);
    QVERIFY(nestedRelativeFile.has_value());
    QVERIFY(nestedRelativeFile->requestedFile.has_value());
    QCOMPARE(nestedRelativeFile->root, std::filesystem::canonical(root));
    QCOMPARE(nestedRelativeFile->requestedFile.value_or(std::filesystem::path{}),
             std::filesystem::canonical(projects / "nested.md"));

    const auto absoluteFile = resolve({(notes / "absolute.md").string()}, root);
    QVERIFY(absoluteFile.has_value());
    QVERIFY(absoluteFile->requestedFile.has_value());
    QCOMPARE(absoluteFile->root, std::filesystem::canonical(notes));
    QCOMPARE(absoluteFile->requestedFile.value_or(std::filesystem::path{}),
             std::filesystem::canonical(notes / "absolute.md"));
}

void LaunchRequestTest::parsesFreshWithoutChangingResolution() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "note.md");

    const auto beforePath = resolve({"--fresh", "note.md"}, root);
    QVERIFY(beforePath.has_value());
    QVERIFY(beforePath->bypassRestore);
    QVERIFY(beforePath->requestedFile.has_value());
    QCOMPARE(beforePath->requestedFile.value_or(std::filesystem::path{}),
             std::filesystem::canonical(root / "note.md"));

    const auto afterPath = resolve({"note.md", "--fresh"}, root);
    QVERIFY(afterPath.has_value());
    QVERIFY(afterPath->bypassRestore);
    QCOMPARE(afterPath->root, beforePath->root);
}

void LaunchRequestTest::rejectsInvalidArgumentsAndMissingPaths() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "one.md");
    writeFile(root / "two.md");

    const auto unknownOption = resolve({"--wat"}, root);
    QVERIFY(!unknownOption.has_value());
    QCOMPARE(unknownOption.error().code, omanotes::LaunchErrorCode::InvalidArguments);

    const auto tooManyPaths = resolve({"one.md", "two.md"}, root);
    QVERIFY(!tooManyPaths.has_value());
    QCOMPARE(tooManyPaths.error().code, omanotes::LaunchErrorCode::InvalidArguments);

    const auto missing = resolve({"missing.md"}, root);
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, omanotes::LaunchErrorCode::InvalidFile);

    const auto missingCurrentDirectory = resolve({}, root / "missing");
    QVERIFY(!missingCurrentDirectory.has_value());
    QCOMPARE(missingCurrentDirectory.error().code, omanotes::LaunchErrorCode::InvalidWorkspace);
}

void LaunchRequestTest::canonicalizesRootSymlinksAndRejectsFileEscapes() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto base = pathFor(temporary.path());
    const auto root = base / "root";
    const auto alias = base / "alias";
    std::filesystem::create_directory(root);
    writeFile(base / "outside.md");

    std::error_code error;
    std::filesystem::create_directory_symlink(root, alias, error);
    QVERIFY2(!error, error.message().c_str());
    std::filesystem::create_symlink(base / "outside.md", root / "escape.md", error);
    QVERIFY2(!error, error.message().c_str());

    const auto linkedRoot = resolve({alias.string()}, base);
    QVERIFY(linkedRoot.has_value());
    QCOMPARE(linkedRoot->root, std::filesystem::canonical(root));

    const auto escapedFile = resolve({"escape.md"}, root);
    QVERIFY(!escapedFile.has_value());
    QCOMPARE(escapedFile.error().code, omanotes::LaunchErrorCode::InvalidFile);

    const auto traversal = resolve({"../outside.md"}, root);
    QVERIFY(!traversal.has_value());
    QCOMPARE(traversal.error().code, omanotes::LaunchErrorCode::InvalidFile);
}

void LaunchRequestTest::rejectsUnreadableRootsAndFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto base = pathFor(temporary.path());
    const auto root = base / "root";
    const auto file = root / "private.md";
    std::filesystem::create_directory(root);
    writeFile(file);

    std::filesystem::permissions(file, std::filesystem::perms::none);
    const auto unreadableFile = resolve({"private.md"}, root);
    std::filesystem::permissions(file, std::filesystem::perms::owner_read |
                                           std::filesystem::perms::owner_write);
    QVERIFY(!unreadableFile.has_value());
    QCOMPARE(unreadableFile.error().code, omanotes::LaunchErrorCode::InvalidFile);

    std::filesystem::permissions(root, std::filesystem::perms::none);
    const auto unreadableRoot = resolve({}, root);
    std::filesystem::permissions(root, std::filesystem::perms::owner_all);
    QVERIFY(!unreadableRoot.has_value());
    QCOMPARE(unreadableRoot.error().code, omanotes::LaunchErrorCode::InvalidWorkspace);
}

QTEST_MAIN(LaunchRequestTest)
#include "launch_request_test.moc"
