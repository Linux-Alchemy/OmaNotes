#include "workspace/file_tree_model.hpp"

#include <QAbstractItemModelTester>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents = "# note\n") {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
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

class FileTreeModelTest final : public QObject {
    Q_OBJECT

  private slots:
    void isLazyAndShowsOnlyDirectoriesAndMarkdownByDefault();
    void skipsHiddenEntriesSymlinksAndUnreadableChildrenSafely();
    void preservesLargeDirectoryLazinessAndNonUtf8Paths();
};

void FileTreeModelTest::isLazyAndShowsOnlyDirectoriesAndMarkdownByDefault() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    std::filesystem::create_directory(root / "folder");
    writeFile(root / "alpha.md");
    writeFile(root / "BETA.MD");
    writeFile(root / "ignored.txt");

    omanotes::FileTreeModel model(root);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.canFetchMore({}));
    model.fetchMore({});

    QCOMPARE(model.rowCount(), 3);
    QVERIFY(findIndex(model, QStringLiteral("folder")).isValid());
    QVERIFY(findIndex(model, QStringLiteral("alpha.md")).isValid());
    QVERIFY(findIndex(model, QStringLiteral("BETA.MD")).isValid());
    QVERIFY(!findIndex(model, QStringLiteral("ignored.txt")).isValid());

    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
}

void FileTreeModelTest::skipsHiddenEntriesSymlinksAndUnreadableChildrenSafely() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto privateDirectory = root / "private";
    std::filesystem::create_directory(privateDirectory);
    writeFile(privateDirectory / "secret.md");
    writeFile(root / ".hidden.md");
    writeFile(root / "visible.md");

    std::error_code error;
    std::filesystem::create_directory_symlink(root, root / "loop", error);
    QVERIFY2(!error, error.message().c_str());
    writeFile(root.parent_path() / "outside.md");
    std::filesystem::create_symlink(root.parent_path() / "outside.md", root / "escape.md", error);
    QVERIFY2(!error, error.message().c_str());
    std::filesystem::permissions(privateDirectory, std::filesystem::perms::none);

    omanotes::FileTreeModel model(root);
    model.fetchMore({});
    const auto privateIndex = findIndex(model, QStringLiteral("private"));
    QVERIFY(privateIndex.isValid());
    QVERIFY(findIndex(model, QStringLiteral("visible.md")).isValid());
    QVERIFY(!findIndex(model, QStringLiteral(".hidden.md")).isValid());
    QVERIFY(!findIndex(model, QStringLiteral("loop")).isValid());
    QVERIFY(!findIndex(model, QStringLiteral("escape.md")).isValid());

    QVERIFY(model.canFetchMore(privateIndex));
    model.fetchMore(privateIndex);
    QCOMPARE(model.rowCount(privateIndex), 0);

    std::filesystem::permissions(privateDirectory, std::filesystem::perms::owner_all);
}

void FileTreeModelTest::preservesLargeDirectoryLazinessAndNonUtf8Paths() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    const auto large = root / "large";
    std::filesystem::create_directory(large);
    for (int index = 0; index < 500; ++index) {
        writeFile(large / ("note-" + std::to_string(index) + ".md"));
    }

    const auto invalidName = std::string("bad-") + static_cast<char>(0xFF) + ".md";
    const auto invalidPath = root / std::filesystem::path(invalidName);
    std::ofstream invalidFile(invalidPath);
    invalidFile << "# byte-preserving name\n";
    invalidFile.close();
    QVERIFY(std::filesystem::exists(invalidPath));

    omanotes::FileTreeModel model(root);
    model.fetchMore({});
    const auto largeIndex = findIndex(model, QStringLiteral("large"));
    QVERIFY(largeIndex.isValid());
    QCOMPARE(model.rowCount(largeIndex), 0);
    QVERIFY(model.canFetchMore(largeIndex));

    bool foundInvalidPath = false;
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row, 0);
        if (model.pathForIndex(index) == std::filesystem::canonical(invalidPath)) {
            foundInvalidPath = true;
        }
    }
    QVERIFY(foundInvalidPath);

    model.fetchMore(largeIndex);
    QCOMPARE(model.rowCount(largeIndex), 500);
}

QTEST_MAIN(FileTreeModelTest)
#include "file_tree_model_test.moc"
