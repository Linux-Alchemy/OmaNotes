#include "persistence/document_store.hpp"
#include "workspace/workspace_root.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

QByteArray readFile(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

class DocumentStoreTest final : public QObject {
    Q_OBJECT

  private slots:
    void savesRelativeAndNestedTargets();
    void savesTextAsUtf8();
    void refusesNonMarkdownTargets();
    void refusesMissingDirectories();
    void refusesTargetsOutsideTheRoot();
};

void DocumentStoreTest::savesRelativeAndNestedTargets() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    std::filesystem::create_directory(root / "projects");
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::DocumentStore store(*workspace);

    const auto plain = store.save("idea.md", QStringLiteral("# Idea\n"));
    QVERIFY(plain.has_value());
    QCOMPARE(*plain, root / "idea.md");
    QCOMPARE(readFile(root / "idea.md"), QByteArray("# Idea\n"));

    const auto nested = store.save("projects/deep.md", QStringLiteral("# Deep\n"));
    QVERIFY(nested.has_value());
    QCOMPARE(readFile(root / "projects" / "deep.md"), QByteArray("# Deep\n"));

    const auto absolute = store.save(root / "absolute.md", QStringLiteral("# Absolute\n"));
    QVERIFY(absolute.has_value());
    QCOMPARE(readFile(root / "absolute.md"), QByteArray("# Absolute\n"));
}

void DocumentStoreTest::savesTextAsUtf8() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::DocumentStore store(*workspace);
    const auto written = store.save("unicode.md", QStringLiteral("# Café — naïve\n"));

    QVERIFY(written.has_value());
    QCOMPARE(readFile(root / "unicode.md"), QString(QStringLiteral("# Café — naïve\n")).toUtf8());
}

void DocumentStoreTest::refusesNonMarkdownTargets() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::DocumentStore store(*workspace);
    const auto written = store.save("notes.txt", QStringLiteral("plain\n"));

    QVERIFY(!written.has_value());
    QCOMPARE(written.error().code, omanotes::SaveErrorCode::InvalidTarget);
    // The refusal has to name the target that would have worked.
    QCOMPARE(QString::fromStdString(written.error().message),
             QStringLiteral("Only Markdown (.md) files can be saved; try notes.md"));
    QVERIFY(!std::filesystem::exists(root / "notes.txt"));

    const auto bare = store.save("user_test_file", QStringLiteral("# Bare\n"));
    QVERIFY(!bare.has_value());
    QCOMPARE(QString::fromStdString(bare.error().message),
             QStringLiteral("Only Markdown (.md) files can be saved; try user_test_file.md"));

    const auto nested = store.save("projects/idea", QStringLiteral("# Nested\n"));
    QVERIFY(!nested.has_value());
    QCOMPARE(QString::fromStdString(nested.error().message),
             QStringLiteral("Only Markdown (.md) files can be saved; try projects/idea.md"));

    // The check is on the extension, not its spelling.
    const auto shouting = store.save("LOUD.MD", QStringLiteral("# Loud\n"));
    QVERIFY(shouting.has_value());
}

void DocumentStoreTest::refusesMissingDirectories() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::DocumentStore store(*workspace);
    const auto written = store.save("no/such/place.md", QStringLiteral("# Nowhere\n"));

    QVERIFY(!written.has_value());
    QCOMPARE(written.error().code, omanotes::SaveErrorCode::InvalidTarget);
    QVERIFY(!std::filesystem::exists(root / "no"));
}

void DocumentStoreTest::refusesTargetsOutsideTheRoot() {
    QTemporaryDir workspaceDirectory;
    QTemporaryDir elsewhere;
    QVERIFY(workspaceDirectory.isValid());
    QVERIFY(elsewhere.isValid());
    const auto root = std::filesystem::canonical(pathFor(workspaceDirectory.path()));
    const auto outside = std::filesystem::canonical(pathFor(elsewhere.path()));
    const auto workspace = omanotes::WorkspaceRoot::resolve(root);
    QVERIFY(workspace.has_value());

    const omanotes::DocumentStore store(*workspace);

    const auto traversal = store.save("../escape.md", QStringLiteral("# Escape\n"));
    QVERIFY(!traversal.has_value());
    QCOMPARE(traversal.error().code, omanotes::SaveErrorCode::OutsideRoot);

    const auto absolute = store.save(outside / "escape.md", QStringLiteral("# Escape\n"));
    QVERIFY(!absolute.has_value());
    QCOMPARE(absolute.error().code, omanotes::SaveErrorCode::OutsideRoot);

    QVERIFY(!std::filesystem::exists(outside / "escape.md"));
}

QTEST_MAIN(DocumentStoreTest)

#include "document_store_test.moc"
