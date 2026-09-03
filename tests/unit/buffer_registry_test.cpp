#include "core/buffer_registry.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

namespace {

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class BufferRegistryTest final : public QObject {
    Q_OBJECT

  private slots:
    void scratchBuffersHaveDistinctIdentity();
    void opensFilesAndKeepsInsertionOrder();
    void duplicateOpenActivatesTheExistingBuffer();
    void duplicateOpenThroughSymlinkSharesOneBuffer();
    void refusesToCloseModifiedBufferSilently();
    void closingTheFinalBufferLeavesAScratchBuffer();
    void reportsMissingBuffersInsteadOfGuessing();
    void closingTheActiveBufferActivatesANeighbour();
    void switchesNextAndPreviousWithWraparound();
};

void BufferRegistryTest::scratchBuffersHaveDistinctIdentity() {
    omanotes::BufferRegistry registry;

    const auto first = registry.createScratch();
    const auto second = registry.createScratch();

    QVERIFY(first != second);
    QCOMPARE(registry.count(), std::size_t{2});
    QCOMPARE(registry.activeId(), std::optional{second});
    QCOMPARE(registry.find(first)->displayName, QStringLiteral("[No Name]"));
    QVERIFY(!registry.find(first)->path.has_value());
}

void BufferRegistryTest::opensFilesAndKeepsInsertionOrder() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "alpha.md", "# Alpha\n");
    writeFile(root / "beta.md", "# Beta\n");

    omanotes::BufferRegistry registry;
    const auto scratch = registry.createScratch();
    const auto alpha = registry.open(root / "alpha.md");
    const auto beta = registry.open(root / "beta.md");

    QVERIFY(alpha.has_value());
    QVERIFY(beta.has_value());
    QCOMPARE(registry.count(), std::size_t{3});
    QCOMPARE(registry.indexOf(scratch), std::optional{std::size_t{0}});
    QCOMPARE(registry.indexOf(*alpha), std::optional{std::size_t{1}});
    QCOMPARE(registry.indexOf(*beta), std::optional{std::size_t{2}});
    QCOMPARE(registry.find(*alpha)->displayName, QStringLiteral("alpha.md"));
    QCOMPARE(registry.activeId(), std::optional{*beta});
}

void BufferRegistryTest::duplicateOpenActivatesTheExistingBuffer() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "note.md", "# Note\n");

    omanotes::BufferRegistry registry;
    const auto first = registry.open(root / "note.md");
    const auto scratch = registry.createScratch();
    QCOMPARE(registry.activeId(), std::optional{scratch});

    const auto again = registry.open(root / "subdir" / ".." / "note.md");

    QVERIFY(first.has_value());
    QVERIFY(again.has_value());
    QCOMPARE(*again, *first);
    QCOMPARE(registry.count(), std::size_t{2});
    QCOMPARE(registry.activeId(), std::optional{*first});
}

void BufferRegistryTest::duplicateOpenThroughSymlinkSharesOneBuffer() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "note.md", "# Note\n");

    std::error_code error;
    std::filesystem::create_symlink(root / "note.md", root / "link.md", error);
    if (error) {
        QSKIP("The filesystem under test does not support symlinks");
    }

    omanotes::BufferRegistry registry;
    const auto direct = registry.open(root / "note.md");
    const auto viaLink = registry.open(root / "link.md");

    QVERIFY(direct.has_value());
    QVERIFY(viaLink.has_value());
    QCOMPARE(*viaLink, *direct);
    QCOMPARE(registry.count(), std::size_t{1});
}

void BufferRegistryTest::refusesToCloseModifiedBufferSilently() {
    omanotes::BufferRegistry registry;
    const auto keep = registry.createScratch();
    const auto dirty = registry.createScratch();
    QVERIFY(registry.setModified(dirty, true));

    const auto refused = registry.close(dirty);

    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, omanotes::BufferErrorCode::Modified);
    QCOMPARE(registry.count(), std::size_t{2});

    const auto discarded = registry.closeDiscardingChanges(dirty);

    QVERIFY(discarded.has_value());
    QCOMPARE(registry.count(), std::size_t{1});
    QCOMPARE(registry.activeId(), std::optional{keep});
}

void BufferRegistryTest::closingTheFinalBufferLeavesAScratchBuffer() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = pathFor(temporary.path());
    writeFile(root / "only.md", "# Only\n");

    omanotes::BufferRegistry registry;
    const auto only = registry.open(root / "only.md");
    QVERIFY(only.has_value());

    QVERIFY(registry.close(*only).has_value());

    QCOMPARE(registry.count(), std::size_t{1});
    const auto replacement = registry.activeId();
    QVERIFY(replacement.has_value());
    if (!replacement.has_value()) {
        return;
    }
    const auto* state = registry.find(*replacement);
    QVERIFY(state != nullptr);
    if (state == nullptr) {
        return;
    }

    QVERIFY(*replacement != *only);
    QVERIFY(!state->path.has_value());
    QCOMPARE(state->displayName, QStringLiteral("[No Name]"));
}

void BufferRegistryTest::reportsMissingBuffersInsteadOfGuessing() {
    omanotes::BufferRegistry registry;
    const auto known = registry.createScratch();
    const auto unknown = QUuid::createUuid();

    QVERIFY(!registry.activate(unknown));
    QCOMPARE(registry.activeId(), std::optional{known});
    QVERIFY(!registry.setModified(unknown, true));
    QCOMPARE(registry.find(unknown), nullptr);
    QCOMPARE(registry.indexOf(unknown), std::nullopt);

    const auto closed = registry.close(unknown);
    QVERIFY(!closed.has_value());
    QCOMPARE(closed.error().code, omanotes::BufferErrorCode::NotFound);
    QCOMPARE(registry.count(), std::size_t{1});

    const auto empty = registry.open({});
    QVERIFY(!empty.has_value());
    QCOMPARE(empty.error().code, omanotes::BufferErrorCode::InvalidPath);
}

void BufferRegistryTest::closingTheActiveBufferActivatesANeighbour() {
    omanotes::BufferRegistry registry;
    const auto first = registry.createScratch();
    const auto second = registry.createScratch();
    const auto third = registry.createScratch();

    QVERIFY(registry.activate(second));
    QVERIFY(registry.close(second).has_value());
    QCOMPARE(registry.activeId(), std::optional{third});

    QVERIFY(registry.close(third).has_value());
    QCOMPARE(registry.activeId(), std::optional{first});
}

void BufferRegistryTest::switchesNextAndPreviousWithWraparound() {
    omanotes::BufferRegistry registry;
    const auto first = registry.createScratch();
    const auto second = registry.createScratch();
    const auto third = registry.createScratch();
    QVERIFY(registry.activate(first));

    QCOMPARE(registry.activateNext(), std::optional{second});
    QCOMPARE(registry.activateNext(), std::optional{third});
    QCOMPARE(registry.activateNext(), std::optional{first});
    QCOMPARE(registry.activatePrevious(), std::optional{third});
    QCOMPARE(registry.activatePrevious(), std::optional{second});

    omanotes::BufferRegistry empty;
    QCOMPARE(empty.activateNext(), std::nullopt);
    QCOMPARE(empty.activatePrevious(), std::nullopt);
}

QTEST_MAIN(BufferRegistryTest)

#include "buffer_registry_test.moc"
