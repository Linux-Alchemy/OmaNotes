#include "app/keymap.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
omanotes::CommandRegistry registry() {
    omanotes::CommandRegistry commands;
    for (const auto* id :
         {"file.save", "file.open", "buffer.show", "buffer.next", "buffer.previous", "buffer.new",
          "pane.sidebar", "pane.editor", "help.show", "search.files", "search.text"}) {
        const auto added = commands.add({QString::fromLatin1(id),
                                         QString::fromLatin1(id),
                                         QStringLiteral("test"),
                                         {},
                                         [](omanotes::AppContext&) {},
                                         {}});
        Q_ASSERT(added.has_value());
    }
    for (const auto& [key, id] : {std::pair{"?", "help.show"},
                                  {"f f", "search.files"},
                                  {"Space", "search.files"},
                                  {"/", "search.text"},
                                  {"f n", "buffer.new"},
                                  {"b n", "buffer.next"},
                                  {"b p", "buffer.previous"}}) {
        const auto bound = commands.bind({QString::fromLatin1(key)}, QString::fromLatin1(id));
        Q_ASSERT(bound.has_value());
    }
    return commands;
}
QVariantMap config(const QByteArray& json) {
    return QJsonDocument::fromJson(json).object().toVariantMap();
}
void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
} // namespace

class KeymapTest final : public QObject {
    Q_OBJECT
  private slots:
    void partialOverridesKeepDefaultsAndSupportSwaps();
    void refusesBadMappings_data();
    void refusesBadMappings();
    void refusesEditorActionCollisions();
    void loadsMissingAndMalformedFilesSafely();
    void labelsShowTheUnshiftedLetterInLowerCase();
};

void KeymapTest::partialOverridesKeepDefaultsAndSupportSwaps() {
    auto commands = registry();
    const auto keymap = omanotes::Keymap::fromConfig(
        config(
            R"({"leaderBindings":{"search.files":["p"],"buffer.next":["b p"],"buffer.previous":["b n"]},"shortcuts":{"file.save":"Ctrl+Alt+S"}})"),
        commands);
    QVERIFY(keymap.has_value());
    QVERIFY(keymap->applyTo(commands).has_value());
    QCOMPARE(commands.lookup(QStringLiteral("p")).commandId, QStringLiteral("search.files"));
    QCOMPARE(commands.lookup(QStringLiteral("f f")).match, omanotes::SequenceMatch::None);
    QCOMPARE(commands.lookup(QStringLiteral("b n")).commandId, QStringLiteral("buffer.previous"));
    QCOMPARE(commands.lookup(QStringLiteral("?")).commandId, QStringLiteral("help.show"));
    QCOMPARE(commands.lookup(QStringLiteral("/")).commandId, QStringLiteral("search.text"));
    QCOMPARE(keymap->sequenceFor(QStringLiteral("file.save")),
             QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    QCOMPARE(keymap->sequenceFor(QStringLiteral("buffer.next")),
             QKeySequence(QStringLiteral("Shift+L")));
    QCOMPARE(keymap->commandFor(QKeySequence(QStringLiteral("Ctrl+S"))), QString{});
}

void KeymapTest::refusesBadMappings_data() {
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("location");
    QTest::newRow("unknown field")
        << QByteArray(R"({"execute":"anything"})") << QStringLiteral("execute");
    QTest::newRow("wrong section type")
        << QByteArray(R"({"leaderBindings":[]})") << QStringLiteral("leaderBindings");
    QTest::newRow("unknown command")
        << QByteArray(R"({"leaderBindings":{"shell.run":["r"]}})") << QStringLiteral("shell.run");
    QTest::newRow("argument required")
        << QByteArray(R"({"leaderBindings":{"file.open":["o"]}})") << QStringLiteral("file.open");
    QTest::newRow("duplicate leader") << QByteArray(R"({"leaderBindings":{"search.files":["/"]}})")
                                      << QStringLiteral("search.files");
    QTest::newRow("duplicate within command")
        << QByteArray(R"({"leaderBindings":{"search.files":["p","p"]}})")
        << QStringLiteral("search.files");
    QTest::newRow("shadowed") << QByteArray(R"({"leaderBindings":{"search.files":["f"]}})")
                              << QStringLiteral("leaderBindings");
    QTest::newRow("longer shadowed") << QByteArray(R"({"leaderBindings":{"search.files":["/ f"]}})")
                                     << QStringLiteral("leaderBindings");
    QTest::newRow("unreachable key")
        << QByteArray(R"({"leaderBindings":{"search.files":["Ctrl+P"]}})")
        << QStringLiteral("search.files");
    QTest::newRow("empty binding") << QByteArray(R"({"leaderBindings":{"search.files":[]}})")
                                   << QStringLiteral("search.files");
    QTest::newRow("help reserved")
        << QByteArray(R"({"leaderBindings":{"help.show":["h"]}})") << QStringLiteral("help.show");
    QTest::newRow("wrong value type")
        << QByteArray(R"({"shortcuts":{"file.save":12}})") << QStringLiteral("file.save");
    QTest::newRow("duplicate shortcut")
        << QByteArray(R"({"shortcuts":{"search.files":"Ctrl+Alt+P","search.text":"Ctrl+Alt+P"}})")
        << QStringLiteral("shortcuts");
    QTest::newRow("Vim control key")
        << QByteArray(R"({"shortcuts":{"file.save":"Ctrl+B"}})") << QStringLiteral("file.save");
    QTest::newRow("typing key") << QByteArray(R"({"shortcuts":{"file.save":"S"}})")
                                << QStringLiteral("file.save");
    QTest::newRow("unknown key") << QByteArray(R"({"shortcuts":{"file.save":"Ctrl+Alt+Nonsense"}})")
                                 << QStringLiteral("file.save");
    QTest::newRow("chord") << QByteArray(R"({"shortcuts":{"file.save":"Ctrl+Alt+S, Ctrl+Alt+P"}})")
                           << QStringLiteral("file.save");
    QTest::newRow("program string")
        << QByteArray(R"({"shortcuts":{"file.save":"sh -c something"}})")
        << QStringLiteral("file.save");
}

void KeymapTest::refusesBadMappings() {
    QFETCH(QByteArray, json);
    QFETCH(QString, location);
    auto commands = registry();
    const auto original = registry();
    const auto result = omanotes::Keymap::fromConfig(config(json), commands);
    QVERIFY(!result.has_value());
    QVERIFY2(result.error().location.contains(location), qPrintable(result.error().location));
    QCOMPARE(commands.bindings(), original.bindings());
}

void KeymapTest::refusesEditorActionCollisions() {
    const auto result =
        omanotes::Keymap::fromConfig(config(R"({"shortcuts":{"search.files":"Ctrl+Alt+P"}})"),
                                     registry(), {QKeySequence(QStringLiteral("Ctrl+Alt+P"))});
    QVERIFY(!result.has_value());
    QVERIFY(result.error().message.contains(QStringLiteral("editor")));
    const auto prefix = omanotes::Keymap::fromConfig(
        config(R"({"shortcuts":{"search.files":"Ctrl+Alt+P"}})"), registry(),
        {QKeySequence(QStringLiteral("Ctrl+Alt+P, Ctrl+Alt+Q"))});
    QVERIFY(!prefix.has_value());
}

void KeymapTest::loadsMissingAndMalformedFilesSafely() {
    QTemporaryDir temporary;
    auto commands = registry();
    const auto path = temporary.path() + QStringLiteral("/keymap.json");
    QVERIFY(omanotes::Keymap::load(path, commands).has_value());
    QVERIFY(!QFile::exists(path));
    writeFile(path, "{broken");
    const auto malformed = omanotes::Keymap::load(path, commands);
    QVERIFY(!malformed.has_value());
    QVERIFY(malformed.error().location.contains(QStringLiteral("byte")));
    writeFile(path, "[]");
    QVERIFY(!omanotes::Keymap::load(path, commands).has_value());
    writeFile(path, QByteArray(65537, ' '));
    QVERIFY(!omanotes::Keymap::load(path, commands).has_value());
    writeFile(path, "{}");
    QVERIFY(omanotes::Keymap::load(path, commands).has_value());
    QVERIFY(!omanotes::Keymap::load(temporary.path(), commands).has_value());
}

void KeymapTest::labelsShowTheUnshiftedLetterInLowerCase() {
    auto commands = registry();
    const auto keymap = omanotes::Keymap::fromConfig(
        config(R"({"shortcuts":{"file.save":"Ctrl+Alt+Shift+S","search.files":"Ctrl+Alt+P"}})"),
        commands);
    QVERIFY(keymap.has_value());
    const auto labels = keymap->shortcutLabels();
    // Ctrl+s is the unshifted key; a capital would read as Ctrl+Shift+S.
    QCOMPARE(labels.at(QStringLiteral("edit.copy")), QStringLiteral("Ctrl+c"));
    QCOMPARE(labels.at(QStringLiteral("pane.sidebar")), QStringLiteral("Ctrl+h"));
    QCOMPARE(labels.at(QStringLiteral("search.files")), QStringLiteral("Ctrl+Alt+p"));
    // Shift really pressed keeps its capital.
    QCOMPARE(labels.at(QStringLiteral("file.save")), QStringLiteral("Ctrl+Alt+Shift+S"));
    QCOMPARE(labels.at(QStringLiteral("buffer.next")), QStringLiteral("Shift+L"));
    QCOMPARE(labels.at(QStringLiteral("buffer.previous")), QStringLiteral("Shift+H"));
}

QTEST_MAIN(KeymapTest)
#include "keymap_test.moc"
