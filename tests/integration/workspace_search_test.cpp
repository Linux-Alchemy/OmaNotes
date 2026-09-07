#include "ui/search_palette.hpp"
#include "workspace/file_index.hpp"
#include "workspace/text_search.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <filesystem>
#include <stop_token>
#include <thread>

namespace {
void writeFile(const std::filesystem::path& path, const QByteArray& bytes) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
} // namespace

class WorkspaceSearchTest final : public QObject {
    Q_OBJECT
  private slots:
    void scoresNamesAndOrdersTies();
    void searchesOnlySafeMarkdownText();
    void boundsResultsAndCancelsLargeWorkspaces();
    void paletteNavigatesPreviewsAndCancels();
    void newerQueryWinsAndUiKeepsRunning();
};

void WorkspaceSearchTest::scoresNamesAndOrdersTies() {
    QVERIFY(omanotes::scoreFile(QStringLiteral("notes/alpha.md"), QStringLiteral("alp")) >
            omanotes::scoreFile(QStringLiteral("notes/a-long-path.md"), QStringLiteral("alp")));
    QVERIFY(!omanotes::scoreFile(QStringLiteral("alpha.md"), QStringLiteral("zzz")));
    QTemporaryDir temporary;
    const std::filesystem::path root(temporary.path().toStdString());
    writeFile(root / "b.md", "b");
    writeFile(root / "a.md", "a");
    const omanotes::WorkspaceSearch search(root);
    const auto result = search.findFiles(QString{});
    QCOMPARE(result.matches.size(), std::size_t{2});
    QCOMPARE(result.matches[0].relativeName, QStringLiteral("a.md"));
    QCOMPARE(result.matches[1].relativeName, QStringLiteral("b.md"));
    QCOMPARE(search.findFiles(QStringLiteral("A.MD")).matches.size(), std::size_t{1});
}

void WorkspaceSearchTest::searchesOnlySafeMarkdownText() {
    QTemporaryDir temporary;
    QTemporaryDir outside;
    const std::filesystem::path root(temporary.path().toStdString());
    const std::filesystem::path elsewhere(outside.path().toStdString());
    std::filesystem::create_directories(root / "nested");
    std::filesystem::create_directories(root / ".hidden");
    writeFile(root / "nested" / "good.md", "first\nAn Agent writes notes\n");
    writeFile(root / "ignored.txt", "agent");
    writeFile(root / ".hidden" / "ignored.md", "agent");
    writeFile(root / "binary.md", QByteArray("agent\0binary", 12));
    writeFile(root / "invalid.md", QByteArray("agent\xff", 6));
    writeFile(root / "long-line.md", QByteArray(5000, 'x') + "agent");
    writeFile(root / "large.md", QByteArray(4 * 1024 * 1024 + 1, 'a') + "agent");
    writeFile(elsewhere / "outside.md", "agent");
    std::filesystem::create_symlink(elsewhere / "outside.md", root / "escape.md");
    std::filesystem::create_directory_symlink(root, root / "cycle");
    const omanotes::WorkspaceSearch search(root);
    const auto result = search.findText(QStringLiteral("AGENT"));
    QCOMPARE(result.matches.size(), std::size_t{1});
    QCOMPARE(result.matches[0].relativeName, QStringLiteral("nested/good.md"));
    QCOMPARE(result.matches[0].line, 1);
    QCOMPARE(result.matches[0].column, 3);
    QVERIFY(!result.cancelled);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(search.findFiles(QStringLiteral("escape")).matches.size(), std::size_t{0});
    QVERIFY(
        !omanotes::WorkspaceSearch(root / "missing").findText(QStringLiteral("a")).error.isEmpty());
}

void WorkspaceSearchTest::boundsResultsAndCancelsLargeWorkspaces() {
    QTemporaryDir temporary;
    const std::filesystem::path root(temporary.path().toStdString());
    for (int file = 0; file < 3000; ++file) {
        writeFile(root / (std::to_string(file) + ".md"), "needle\n");
    }
    const omanotes::WorkspaceSearch search(root);
    QElapsedTimer elapsed;
    elapsed.start();
    const auto result = search.findText(QStringLiteral("needle"));
    QCOMPARE(result.matches.size(), std::size_t{200});
    QVERIFY(result.truncated);
    qInfo() << "3000-file search:" << elapsed.elapsed() << "ms";
    QVERIFY(elapsed.elapsed() < 10000);
    std::stop_source cancelled;
    cancelled.request_stop();
    QVERIFY(search.findFiles(QStringLiteral("n"), cancelled.get_token()).cancelled);
    std::stop_source during;
    std::jthread cancel([&during] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        during.request_stop();
    });
    const auto partial = search.findText(QStringLiteral("not present"), during.get_token());
    QVERIFY(partial.cancelled);
}

void WorkspaceSearchTest::paletteNavigatesPreviewsAndCancels() {
    QTemporaryDir temporary;
    const std::filesystem::path root(temporary.path().toStdString());
    writeFile(root / "a.md", "Alpha preview");
    writeFile(root / "b.md", "Beta preview");
    omanotes::SearchPalette palette(root);
    QSignalSpy chosen(&palette, &omanotes::SearchPalette::matchChosen);
    palette.begin(omanotes::SearchKind::Files);
    auto* query = palette.findChild<QLineEdit*>(QStringLiteral("searchQuery"));
    auto* list = palette.findChild<QListWidget*>(QStringLiteral("searchResults"));
    auto* preview = palette.findChild<QPlainTextEdit*>(QStringLiteral("searchPreview"));
    QTRY_COMPARE(list->count(), 2);
    QCOMPARE(preview->toPlainText(), QStringLiteral("Alpha preview"));
    QTest::keyClick(query, Qt::Key_J, Qt::ControlModifier);
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(preview->toPlainText(), QStringLiteral("Beta preview"));
    QTest::keyClick(query, Qt::Key_Return);
    QCOMPARE(chosen.count(), 1);
    QVERIFY(!palette.isVisible());
    palette.begin(omanotes::SearchKind::Text);
    QTest::keyClicks(query, QStringLiteral("Alpha"));
    QTRY_COMPARE(list->count(), 1);
    QTest::keyClick(query, Qt::Key_Escape);
    QVERIFY(!palette.isVisible());
    QCOMPARE(chosen.count(), 1);
    palette.begin(omanotes::SearchKind::Files);
    QTRY_COMPARE(list->count(), 2);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                       list->visualItemRect(list->item(0)).center());
    QTRY_COMPARE(chosen.count(), 2);
}

void WorkspaceSearchTest::newerQueryWinsAndUiKeepsRunning() {
    QTemporaryDir temporary;
    const std::filesystem::path root(temporary.path().toStdString());
    for (int file = 0; file < 1000; ++file) {
        writeFile(root / (std::to_string(file) + ".md"), "ordinary");
    }
    writeFile(root / "latest.md", "latest result");
    omanotes::SearchPalette palette(root);
    auto* query = palette.findChild<QLineEdit*>(QStringLiteral("searchQuery"));
    auto* list = palette.findChild<QListWidget*>(QStringLiteral("searchResults"));
    int heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    connect(&heartbeat, &QTimer::timeout, &palette, [&heartbeats] { ++heartbeats; });
    heartbeat.start();
    palette.begin(omanotes::SearchKind::Files);
    QTest::qWait(110);
    query->setText(QStringLiteral("does-not-exist"));
    query->setText(QStringLiteral("latest"));
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), QStringLiteral("latest.md"));
    QVERIFY(heartbeats > 5);
    palette.reject();
}

QTEST_MAIN(WorkspaceSearchTest)
#include "workspace_search_test.moc"
