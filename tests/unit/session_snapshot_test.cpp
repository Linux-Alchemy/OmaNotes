#include "session/session_snapshot.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

namespace {

QString fixture(const char* name) {
    return QStringLiteral(OMANOTES_FIXTURE_DIR "/session/") + QString::fromLatin1(name);
}

QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray digest(const QString& path) {
    return QCryptographicHash::hash(readAll(path), QCryptographicHash::Sha256);
}

void writeFile(const std::filesystem::path& path, const QByteArray& bytes) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

omanotes::SessionSnapshot sample() {
    omanotes::SessionSnapshot snapshot;
    snapshot.workspaceRoot = "/home/matt/notes";
    snapshot.window = {1100, 720, false};
    snapshot.sidebar = {true, 240, std::filesystem::path("journal/2026-09-09.md")};
    omanotes::BufferSnapshot clean;
    clean.id = QUuid::fromString(QStringLiteral("0d5b7d8e-1f6a-4c3b-9e2d-8a7f6b5c4d3e"));
    clean.path = "journal/2026-09-09.md";
    clean.cursor = {12, 4};
    clean.scrollLine = 8;
    omanotes::BufferSnapshot dirty;
    dirty.id = QUuid::fromString(QStringLiteral("7a1c2e3f-4b5d-4e6f-8a9b-0c1d2e3f4a5b"));
    dirty.path = "ideas.md";
    dirty.viewMode = omanotes::ViewMode::Reading;
    dirty.modified = true;
    dirty.recovery = QUuid::fromString(QStringLiteral("c0ffee00-1234-4abc-8def-000000000001"));
    omanotes::BufferSnapshot scratch;
    scratch.id = QUuid::fromString(QStringLiteral("9e8d7c6b-5a4f-4e3d-9c2b-1a0f9e8d7c6b"));
    scratch.cursor = {1, 3};
    scratch.modified = true;
    scratch.recovery = QUuid::fromString(QStringLiteral("c0ffee00-1234-4abc-8def-000000000002"));
    snapshot.buffers = {clean, dirty, scratch};
    snapshot.activeBuffer = clean.id;
    return snapshot;
}

} // namespace

class SessionSnapshotTest final : public QObject {
    Q_OBJECT

  private slots:
    void roundTripsTheFullFixtureByteForByte();
    void defaultsDocumentedOmissions();
    void serializationIsDeterministic();
    void refusesBadDocuments_data();
    void refusesBadDocuments();
    void reportsVersionBeforeUnknownFields();
    void refusesOversizedDocumentsWithoutReadingThemWhole();
    void neverModifiesTheFileItRefuses();
    void checksTheRootBeforeRestoring();
    void resolvesPathsThroughTheRootPolicy();
    void countsDirtyBuffersForTheParkedWorkNotice();
};

void SessionSnapshotTest::roundTripsTheFullFixtureByteForByte() {
    const auto bytes = readAll(fixture("valid-full.json"));
    QVERIFY(!bytes.isEmpty());
    const auto parsed = omanotes::parseSessionSnapshot(bytes);
    QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString() : parsed.error().describe()));
    QCOMPARE(*parsed, sample());
    QCOMPARE(omanotes::serializeSessionSnapshot(*parsed), bytes);
    const auto again = omanotes::parseSessionSnapshot(omanotes::serializeSessionSnapshot(sample()));
    QVERIFY(again.has_value());
    QCOMPARE(*again, sample());
}

void SessionSnapshotTest::defaultsDocumentedOmissions() {
    const auto parsed = omanotes::parseSessionSnapshot(readAll(fixture("valid-minimal.json")));
    QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString() : parsed.error().describe()));
    QCOMPARE(parsed->version, omanotes::kSessionFormatVersion);
    QCOMPARE(parsed->window, (omanotes::WindowSnapshot{800, 600, false}));
    QCOMPARE(parsed->sidebar, (omanotes::SidebarSnapshot{false, 0, std::nullopt}));
    QCOMPARE(parsed->buffers.size(), std::size_t{1});
    const auto& buffer = parsed->buffers.front();
    QCOMPARE(buffer.path, std::optional<std::filesystem::path>("todo.md"));
    QCOMPARE(buffer.viewMode, omanotes::ViewMode::Writing);
    QCOMPARE(buffer.cursor, (omanotes::CursorSnapshot{0, 0}));
    QCOMPARE(buffer.scrollLine, 0);
    QVERIFY(!buffer.modified);
    QVERIFY(!buffer.recovery.has_value());
    QVERIFY(!parsed->activeBuffer.has_value());
}

void SessionSnapshotTest::serializationIsDeterministic() {
    // A parsed minimal document and a hand-built struct with the same
    // defaults must serialize to the same bytes: output depends only on
    // content, and every default is written out explicitly.
    const auto parsed = omanotes::parseSessionSnapshot(readAll(fixture("valid-minimal.json")));
    QVERIFY(parsed.has_value());
    omanotes::SessionSnapshot built;
    built.workspaceRoot = "/home/matt/notes";
    built.window = {800, 600, false};
    omanotes::BufferSnapshot buffer;
    buffer.id = QUuid::fromString(QStringLiteral("0d5b7d8e-1f6a-4c3b-9e2d-8a7f6b5c4d3e"));
    buffer.path = "todo.md";
    built.buffers = {buffer};
    const auto first = omanotes::serializeSessionSnapshot(*parsed);
    QCOMPARE(first, omanotes::serializeSessionSnapshot(built));
    QVERIFY(first.endsWith('\n'));
    QVERIFY(first.contains("\"maximized\": false"));
    QVERIFY(first.contains("\"scrollLine\": 0"));
}

void SessionSnapshotTest::refusesBadDocuments_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<omanotes::SessionErrorCode>("code");
    QTest::addColumn<QString>("location");
    using Code = omanotes::SessionErrorCode;
    QTest::newRow("truncated") << "corrupt-truncated.json" << Code::Malformed << "byte 96";
    QTest::newRow("not-object") << "corrupt-not-object.json" << Code::Malformed << "";
    QTest::newRow("missing-window")
        << "missing-field-window.json" << Code::InvalidField << "window";
    QTest::newRow("missing-buffer-id")
        << "missing-field-buffer-id.json" << Code::InvalidField << "buffers[0].id";
    QTest::newRow("future") << "future-version.json" << Code::FutureVersion << "version";
    QTest::newRow("old") << "old-version.json" << Code::UnsupportedVersion << "version";
    QTest::newRow("unknown-field") << "unknown-field.json" << Code::InvalidField << "contents";
    QTest::newRow("escape-parent")
        << "escape-parent.json" << Code::InvalidField << "buffers[0].path";
    QTest::newRow("escape-absolute")
        << "escape-absolute.json" << Code::InvalidField << "sidebar.selectedPath";
    QTest::newRow("dirty-without-recovery")
        << "dirty-without-recovery.json" << Code::InvalidField << "buffers[0].recovery";
    QTest::newRow("active-not-listed")
        << "active-not-listed.json" << Code::InvalidField << "activeBuffer";
    QTest::newRow("out-of-range") << "out-of-range.json" << Code::InvalidField << "window.width";
}

void SessionSnapshotTest::refusesBadDocuments() {
    QFETCH(QString, file);
    QFETCH(omanotes::SessionErrorCode, code);
    QFETCH(QString, location);
    const auto bytes = readAll(fixture(qPrintable(file)));
    QVERIFY(!bytes.isEmpty());
    const auto parsed = omanotes::parseSessionSnapshot(bytes);
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, code);
    QCOMPARE(parsed.error().location, location);
    QVERIFY(!parsed.error().message.isEmpty());
    QVERIFY(parsed.error().describe().contains(parsed.error().message));
}

void SessionSnapshotTest::reportsVersionBeforeUnknownFields() {
    // A future document carries fields this build cannot know. The user
    // must be told it is newer, not that "tabs" is an unknown field.
    const auto parsed = omanotes::parseSessionSnapshot(readAll(fixture("future-version.json")));
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, omanotes::SessionErrorCode::FutureVersion);
    QVERIFY(parsed.error().message.contains(QStringLiteral("newer")));
}

void SessionSnapshotTest::refusesOversizedDocumentsWithoutReadingThemWhole() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto file = std::filesystem::path(directory.path().toStdString()) / "session.json";
    auto bytes = omanotes::serializeSessionSnapshot(sample());
    bytes.chop(2); // drop "}\n"
    bytes += ",\n    \"padding\": \"";
    bytes += QByteArray(static_cast<qsizetype>(omanotes::kSessionMaxBytes), 'x');
    bytes += "\"\n}\n";
    writeFile(file, bytes);
    QVERIFY(static_cast<std::size_t>(bytes.size()) > omanotes::kSessionMaxBytes);

    const auto parsed = omanotes::readSessionSnapshot(file);
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, omanotes::SessionErrorCode::Oversized);
    QVERIFY(parsed.error().location.contains(QStringLiteral("session.json")));
    QCOMPARE(readAll(QString::fromStdString(file.string())), bytes);

    // The in-memory parser applies the same ceiling.
    const auto direct = omanotes::parseSessionSnapshot(bytes);
    QVERIFY(!direct.has_value());
    QCOMPARE(direct.error().code, omanotes::SessionErrorCode::Oversized);
}

void SessionSnapshotTest::neverModifiesTheFileItRefuses() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto root = std::filesystem::path(directory.path().toStdString());
    const auto names = {"corrupt-truncated.json", "future-version.json", "escape-parent.json"};
    for (const auto* name : names) {
        const auto copy = root / name;
        const auto original = readAll(fixture(name));
        writeFile(copy, original);
        const auto path = QString::fromStdString(copy.string());
        const auto before = digest(path);
        const auto modifiedBefore = QFileInfo(path).lastModified();
        QFile::setPermissions(path, QFileDevice::ReadOwner);

        const auto parsed = omanotes::readSessionSnapshot(copy);
        QVERIFY2(!parsed.has_value(), name);
        QVERIFY(parsed.error().location.startsWith(path));

        QCOMPARE(digest(path), before);
        QCOMPARE(QFileInfo(path).lastModified(), modifiedBefore);
        QVERIFY(QFileInfo(path).exists());
    }

    const auto missing = omanotes::readSessionSnapshot(root / "absent.json");
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, omanotes::SessionErrorCode::Unreadable);
    QVERIFY(!QFileInfo(QString::fromStdString((root / "absent.json").string())).exists());

    const auto asDirectory = omanotes::readSessionSnapshot(root);
    QVERIFY(!asDirectory.has_value());
    QCOMPARE(asDirectory.error().code, omanotes::SessionErrorCode::Unreadable);
}

void SessionSnapshotTest::checksTheRootBeforeRestoring() {
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid() && second.isValid());
    const auto firstRoot =
        omanotes::WorkspaceRoot::resolve(std::filesystem::path(first.path().toStdString()));
    const auto secondRoot =
        omanotes::WorkspaceRoot::resolve(std::filesystem::path(second.path().toStdString()));
    QVERIFY(firstRoot.has_value() && secondRoot.has_value());

    auto snapshot = sample();
    snapshot.workspaceRoot = firstRoot->path();
    QVERIFY(omanotes::checkSessionRoot(snapshot, *firstRoot).has_value());

    const auto mismatch = omanotes::checkSessionRoot(snapshot, *secondRoot);
    QVERIFY(!mismatch.has_value());
    QCOMPARE(mismatch.error().code, omanotes::SessionErrorCode::RootMismatch);
    QVERIFY(mismatch.error().message.contains(
        QString::fromStdString(firstRoot->path().generic_string())));

    // The stored root may be a non-canonical spelling of the same directory.
    snapshot.workspaceRoot = std::filesystem::path(first.path().toStdString()) / ".";
    QVERIFY(omanotes::checkSessionRoot(snapshot, *firstRoot).has_value());

    // A root that no longer exists cannot match anything.
    snapshot.workspaceRoot = "/nonexistent/omanotes/root";
    QVERIFY(!omanotes::checkSessionRoot(snapshot, *firstRoot).has_value());
}

void SessionSnapshotTest::resolvesPathsThroughTheRootPolicy() {
    QTemporaryDir directory;
    QTemporaryDir outside;
    QVERIFY(directory.isValid() && outside.isValid());
    const auto rootPath = std::filesystem::path(directory.path().toStdString());
    const auto outsidePath = std::filesystem::path(outside.path().toStdString());
    QVERIFY(std::filesystem::create_directories(rootPath / "journal"));
    writeFile(rootPath / "journal" / "today.md", "# today\n");
    writeFile(outsidePath / "secret.md", "# secret\n");
    std::filesystem::create_symlink(outsidePath / "secret.md", rootPath / "linked.md");
    const auto root = omanotes::WorkspaceRoot::resolve(rootPath);
    QVERIFY(root.has_value());

    const auto inside = omanotes::resolveSessionPath(*root, "journal/today.md");
    QVERIFY(inside.has_value());
    QVERIFY(root->contains(*inside));

    const auto missing = omanotes::resolveSessionPath(*root, "journal/yesterday.md");
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, omanotes::WorkspaceErrorCode::Missing);

    const auto escaped = omanotes::resolveSessionPath(*root, "linked.md");
    QVERIFY(!escaped.has_value());
    QCOMPARE(escaped.error().code, omanotes::WorkspaceErrorCode::OutsideRoot);

    const auto absolute = omanotes::resolveSessionPath(*root, outsidePath / "secret.md");
    QVERIFY(!absolute.has_value());
    QCOMPARE(absolute.error().code, omanotes::WorkspaceErrorCode::OutsideRoot);

    const auto folder = omanotes::resolveSessionPath(*root, "journal");
    QVERIFY(!folder.has_value());
    QCOMPARE(folder.error().code, omanotes::WorkspaceErrorCode::NotRegularFile);
}

void SessionSnapshotTest::countsDirtyBuffersForTheParkedWorkNotice() {
    QCOMPARE(sample().dirtyBufferCount(), std::size_t{2});
    omanotes::SessionSnapshot empty;
    QCOMPARE(empty.dirtyBufferCount(), std::size_t{0});
    const auto parsed = omanotes::parseSessionSnapshot(readAll(fixture("valid-full.json")));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->dirtyBufferCount(), std::size_t{2});
}

QTEST_GUILESS_MAIN(SessionSnapshotTest)
#include "session_snapshot_test.moc"
