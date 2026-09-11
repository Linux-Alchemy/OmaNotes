// Property tests for every parser that takes untrusted bytes (block 8.1.2).
//
// Each case starts from valid input, mutates it thousands of ways with a
// fixed seed, and checks the properties that matter rather than specific
// outputs: the parser returns, it either accepts something that re-serializes
// to itself or refuses with a located message, and it never accepts what the
// documented limits forbid. Sanitizers do the rest: a crash here is a finding.

#include "app/keymap.hpp"
#include "core/command_registry.hpp"
#include "persistence/recovery_store.hpp"
#include "session/session_snapshot.hpp"
#include "ui/markdown_view.hpp"
#include "ui/theme_adapter.hpp"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kSeed = 0x0000'2026'0911'a7e5ULL;
constexpr int kRounds = 3000;

class Mutator {
  public:
    explicit Mutator(std::uint64_t seed) : engine_(seed) {}

    [[nodiscard]] int below(int bound) {
        return std::uniform_int_distribution<int>(0, bound - 1)(engine_);
    }
    [[nodiscard]] bool coin() { return below(2) == 0; }

    /// One random edit: flip, insert, delete, truncate, duplicate, or splice
    /// in something the parsers have to treat as hostile.
    [[nodiscard]] QByteArray mutate(QByteArray bytes) {
        static const QByteArray splices[] = {
            "\0",
            "\xff\xfe",
            "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[",
            "{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":{\"a\":1}}}}}}}}}}",
            "99999999999999999999999999999",
            "-1",
            "1e400",
            "\"../../etc/passwd\"",
            "\"/etc/passwd\"",
            "\"\\u0000\"",
            "\"\xf0\x9f\x92\xa9\"",
            "null",
            "true",
            "\"version\":2",
            "\"version\":0",
            "\\",
            "\"",
            ",",
            "}",
            "{",
            "\n\n\n",
            "\"path\":\"a/../../b\"",
            "\"recovery\":\"not-a-uuid\""};
        const int size = static_cast<int>(bytes.size());
        switch (below(7)) {
        case 0:
            if (size > 0) {
                const int at = below(size);
                bytes[at] = static_cast<char>(below(256));
            }
            return bytes;
        case 1:
            bytes.insert(size > 0 ? below(size + 1) : 0, static_cast<char>(below(256)));
            return bytes;
        case 2:
            if (size > 0) {
                bytes.remove(below(size), 1 + below(std::min(8, size)));
            }
            return bytes;
        case 3:
            return size > 0 ? bytes.left(below(size + 1)) : bytes;
        case 4:
            if (size > 1) {
                const int from = below(size);
                const int length = 1 + below(std::min(32, size - from));
                bytes.insert(below(size + 1), bytes.mid(from, length));
            }
            return bytes;
        case 5:
            bytes.insert(size > 0 ? below(size + 1) : 0,
                         splices[static_cast<std::size_t>(below(std::size(splices)))]);
            return bytes;
        default:
            // A couple of edits at once.
            return mutate(mutate(std::move(bytes)));
        }
    }

  private:
    std::mt19937_64 engine_;
};

std::filesystem::path pathFor(const QString& path) { return path.toStdString(); }

/// The parsers' rule is component-wise: no "." or ".." segment. A name that
/// merely contains dots ("notes../idea.md") is an ordinary relative path,
/// which the fuzz harness first got wrong (2026-09-11).
bool noDotComponents(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

void writeBytes(const std::filesystem::path& path, const QByteArray& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.constData(), bytes.size());
}

omanotes::SessionSnapshot sampleSnapshot(Mutator& random, const std::filesystem::path& root) {
    omanotes::SessionSnapshot snapshot;
    snapshot.workspaceRoot = root;
    snapshot.window = {640 + random.below(2000), 400 + random.below(1500), random.coin()};
    snapshot.sidebar.visible = random.coin();
    snapshot.sidebar.width = random.below(600);
    if (random.coin()) {
        snapshot.sidebar.selectedPath = "notes/selected.md";
    }
    const int count = random.below(5);
    for (int i = 0; i < count; ++i) {
        omanotes::BufferSnapshot buffer;
        buffer.id = QUuid::createUuid();
        if (random.coin()) {
            buffer.path = std::filesystem::path("folder") /
                          (QStringLiteral("note-%1.md").arg(i).toStdString());
        }
        buffer.viewMode = random.coin() ? omanotes::ViewMode::Writing : omanotes::ViewMode::Reading;
        buffer.cursor = {random.below(1000), random.below(200)};
        buffer.scrollLine = random.below(1000);
        buffer.modified = random.coin();
        if (buffer.modified) {
            buffer.recovery = QUuid::createUuid();
        }
        snapshot.buffers.push_back(buffer);
    }
    if (!snapshot.buffers.empty() && random.coin()) {
        snapshot.activeBuffer = snapshot.buffers[static_cast<std::size_t>(random.below(count))].id;
    }
    return snapshot;
}

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
                                  {"/", "search.text"},
                                  {"b n", "buffer.next"}}) {
        const auto bound = commands.bind({QString::fromLatin1(key)}, QString::fromLatin1(id));
        Q_ASSERT(bound.has_value());
    }
    return commands;
}

constexpr auto kSemanticTheme = R"(mode = "dark"

accent = "#7d82d9"
selection = "#252e56"
muted = "#6d7db6"

background = "#000000"
dark_background = "#040816"
lighter_background = "#131a3a"

foreground = "#ffcead"
blue = "#7d82d9"
)";

} // namespace

class ParserPropertyTest final : public QObject {
    Q_OBJECT

  private slots:
    void snapshotRoundTripsAndRefusesEveryBadMutation();
    void recoveryRecordRoundTripsAndRefusesEveryBadMutation();
    void keymapNeverBindsOutsideTheSafeSet();
    void themeReaderAlwaysYieldsAReadablePalette();
    void linkClassificationIsTotalAndNeverLeavesTheRoot();
};

void ParserPropertyTest::snapshotRoundTripsAndRefusesEveryBadMutation() {
    Mutator random(kSeed);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));

    int accepted = 0;
    int refused = 0;
    for (int round = 0; round < kRounds; ++round) {
        const auto original = sampleSnapshot(random, root);
        const auto bytes = omanotes::serializeSessionSnapshot(original);

        // Property 1: what the writer produces, the reader accepts, and it
        // comes back equal and re-serializes byte for byte.
        const auto parsed = omanotes::parseSessionSnapshot(bytes);
        QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString() : parsed.error().describe()));
        QCOMPARE(*parsed, original);
        QCOMPARE(omanotes::serializeSessionSnapshot(*parsed), bytes);

        // Property 2: a mutation is either refused with a located message,
        // or accepted as something that is itself stable under round trip
        // and within every documented limit. Never anything else.
        const auto mutated = random.mutate(bytes);
        const auto result = omanotes::parseSessionSnapshot(mutated);
        if (!result) {
            ++refused;
            QVERIFY(!result.error().message.isEmpty());
            continue;
        }
        ++accepted;
        const auto again = omanotes::serializeSessionSnapshot(*result);
        const auto reparsed = omanotes::parseSessionSnapshot(again);
        QVERIFY(reparsed.has_value());
        QCOMPARE(*reparsed, *result);
        QVERIFY(result->buffers.size() <= omanotes::kSessionMaxBuffers);
        QVERIFY(result->window.width >= 1 &&
                result->window.width <= omanotes::kSessionMaxDimension);
        QVERIFY(result->window.height >= 1 &&
                result->window.height <= omanotes::kSessionMaxDimension);
        for (const auto& buffer : result->buffers) {
            QVERIFY(buffer.modified == buffer.recovery.has_value());
            if (buffer.path) {
                QVERIFY(buffer.path->is_relative());
                QVERIFY(noDotComponents(*buffer.path));
            }
        }
    }
    // Both branches were exercised; a mutator that only ever breaks JSON
    // would prove nothing about the validators.
    QVERIFY2(accepted > 0, "no mutation was ever accepted");
    QVERIFY2(refused > 0, "no mutation was ever refused");

    // Property 3: the size limit is a wall, not a suggestion.
    const auto huge = QByteArray(static_cast<qsizetype>(omanotes::kSessionMaxBytes) + 1, ' ');
    const auto oversized = omanotes::parseSessionSnapshot(huge);
    QVERIFY(!oversized.has_value());
    QCOMPARE(oversized.error().code, omanotes::SessionErrorCode::Oversized);
}

void ParserPropertyTest::recoveryRecordRoundTripsAndRefusesEveryBadMutation() {
    Mutator random(kSeed + 1);
    int accepted = 0;
    int refused = 0;
    for (int round = 0; round < kRounds; ++round) {
        omanotes::BufferRecovery original;
        if (random.coin()) {
            original.path = std::filesystem::path("notes") /
                            (QStringLiteral("idea-%1.md").arg(round % 7).toStdString());
        }
        QString text;
        const int lines = random.below(6);
        for (int i = 0; i < lines; ++i) {
            text += QStringLiteral("line %1 \xf0\x9f\x92\xa9 \"quoted\" \\ back\n").arg(i);
        }
        original.contents = text;
        if (random.coin()) {
            original.baseRevision = omanotes::SavedRevision::of(text.toUtf8());
        }

        const auto bytes = omanotes::serializeBufferRecovery(original);
        const auto parsed = omanotes::parseBufferRecovery(bytes);
        QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString() : parsed.error().describe()));
        QCOMPARE(*parsed, original);
        QCOMPARE(omanotes::serializeBufferRecovery(*parsed), bytes);

        const auto mutated = random.mutate(bytes);
        const auto result = omanotes::parseBufferRecovery(mutated);
        if (!result) {
            ++refused;
            QVERIFY(!result.error().message.isEmpty());
            continue;
        }
        ++accepted;
        QCOMPARE(omanotes::parseBufferRecovery(omanotes::serializeBufferRecovery(*result)).value(),
                 *result);
        if (result->path) {
            QVERIFY(result->path->is_relative());
            QVERIFY(noDotComponents(*result->path));
        }
        QVERIFY(static_cast<std::size_t>(result->contents.toUtf8().size()) <=
                omanotes::kRecoveryMaxContentBytes);
    }
    QVERIFY2(accepted > 0, "no mutation was ever accepted");
    QVERIFY2(refused > 0, "no mutation was ever refused");
}

void ParserPropertyTest::keymapNeverBindsOutsideTheSafeSet() {
    Mutator random(kSeed + 2);
    const auto commands = registry();
    const QByteArray valid = R"({
        "leaderBindings": {"f o": "file.save", "b b": "buffer.next"},
        "shortcuts": {"file.save": "Ctrl+Alt+S", "help.show": "Ctrl+Alt+H"}
    })";
    static const char* const commandIds[] = {"file.save",   "file.open",   "buffer.show",
                                             "buffer.new",  "pane.editor", "help.show",
                                             "search.text", "shell.run",   "nope"};
    static const char* const keys[] = {"Ctrl+Alt+S",
                                       "Ctrl+S",
                                       "S",
                                       "Ctrl+Alt+Shift+9",
                                       "Ctrl+Alt+F1",
                                       "Meta+X",
                                       "Ctrl+Alt+S, Ctrl+Alt+T",
                                       "f o",
                                       "Space",
                                       "?",
                                       "",
                                       "Ctrl+Alt+\xf0\x9f\x92\xa9"};

    int accepted = 0;
    int refused = 0;
    for (int round = 0; round < kRounds; ++round) {
        // Half the rounds mutate the bytes; half assemble a structurally
        // plausible document from the pools, which reaches the validators
        // that byte mutation mostly never gets past.
        QByteArray candidate;
        if (random.coin()) {
            candidate = random.mutate(valid);
        } else {
            QJsonObject leaders;
            QJsonObject shortcuts;
            const int entries = random.below(4);
            for (int i = 0; i < entries; ++i) {
                leaders.insert(QString::fromUtf8(keys[random.below(std::size(keys))]),
                               QString::fromUtf8(commandIds[random.below(std::size(commandIds))]));
                shortcuts.insert(QString::fromUtf8(commandIds[random.below(std::size(commandIds))]),
                                 QString::fromUtf8(keys[random.below(std::size(keys))]));
            }
            QJsonObject document;
            document.insert(QStringLiteral("leaderBindings"), leaders);
            document.insert(QStringLiteral("shortcuts"), shortcuts);
            if (random.below(10) == 0) {
                document.insert(QStringLiteral("hooks"), QStringLiteral("sh -c true"));
            }
            candidate = QJsonDocument(document).toJson();
        }

        const auto values = QJsonDocument::fromJson(candidate).object().toVariantMap();
        const auto keymap = omanotes::Keymap::fromConfig(values, commands);
        if (!keymap) {
            ++refused;
            QVERIFY(!keymap.error().message.isEmpty());
            continue;
        }
        ++accepted;
        // Whatever was accepted binds only registered commands, only to
        // sequences the policy allows, and applying it to a copy succeeds
        // without touching the original registry.
        const auto builtIn = omanotes::Keymap::defaults(commands).shortcutLabels();
        for (const auto& [id, label] : keymap->shortcutLabels()) {
            // A label is either for a registered command or one of the
            // documented built-in editor routes the defaults always carry.
            QVERIFY2(commands.find(id) != nullptr || builtIn.contains(id), qPrintable(id));
            if (commands.find(id) == nullptr) {
                QCOMPARE(keymap->sequenceFor(id),
                         omanotes::Keymap::defaults(commands).sequenceFor(id));
                continue;
            }
            const auto sequence = keymap->sequenceFor(id);
            if (sequence.isEmpty()) {
                continue;
            }
            // ADR 0008's set, and nothing else: Ctrl+Alt[+Shift]+letter or
            // digit, or one of the four documented legacy routes bound only
            // to its own command.
            QCOMPARE(sequence.count(), 1);
            const auto key = sequence[0].key();
            const auto modifiers = sequence[0].keyboardModifiers();
            const bool chord =
                (modifiers == (Qt::ControlModifier | Qt::AltModifier) ||
                 modifiers == (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) &&
                ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9));
            const bool legacy =
                (modifiers == Qt::ShiftModifier && (key == Qt::Key_H || key == Qt::Key_L) &&
                 (id == QStringLiteral("buffer.next") ||
                  id == QStringLiteral("buffer.previous"))) ||
                (modifiers == Qt::ControlModifier && key == Qt::Key_H &&
                 id == QStringLiteral("pane.sidebar")) ||
                (modifiers == Qt::ControlModifier && key == Qt::Key_L &&
                 id == QStringLiteral("pane.editor")) ||
                ((modifiers == Qt::ControlModifier ||
                  modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) &&
                 key == Qt::Key_S && id == QStringLiteral("file.save"));
            QVERIFY2(chord || legacy, qPrintable(id + QStringLiteral(" = ") + sequence.toString()));
        }
        auto copy = commands;
        const auto applied = keymap->applyTo(copy);
        QVERIFY(applied.has_value());
        QCOMPARE(commands.bindings(), registry().bindings());
    }
    QVERIFY2(accepted > 0, "no candidate was ever accepted");
    QVERIFY2(refused > 0, "no candidate was ever refused");
}

void ParserPropertyTest::themeReaderAlwaysYieldsAReadablePalette() {
    Mutator random(kSeed + 3);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto base = pathFor(temporary.path());
    const omanotes::ThemeSources sources{base / "state" / "omarchy" / "current",
                                         base / "config" / "omarchy"};
    const QByteArray theme(kSemanticTheme);
    const QByteArray shell("[font]\nbase-size = 14\n");

    for (int round = 0; round < 400; ++round) {
        writeBytes(sources.stateDir / "theme" / "colors.toml",
                   random.coin() ? random.mutate(theme) : random.mutate(random.mutate(theme)));
        writeBytes(sources.configDir / "shell.toml", random.mutate(shell));
        writeBytes(sources.stateDir / "theme.name", random.mutate("ethereal-black\n"));

        const omanotes::ThemeAdapter adapter(sources);
        const auto& palette = adapter.currentPalette();
        // Whatever the file said, the palette is complete and readable: the
        // guards that the dedicated suite checks one role at a time hold for
        // every mutation at once.
        QVERIFY(palette.background.isValid() && palette.text.isValid());
        QVERIFY2(omanotes::contrastRatio(palette.text, palette.background) >= 3.0,
                 qPrintable(QStringLiteral("round %1: %2 on %3")
                                .arg(round)
                                .arg(palette.text.name(), palette.background.name())));
        QVERIFY(omanotes::contrastRatio(palette.mutedText, palette.background) >= 3.0);
        QVERIFY(omanotes::contrastRatio(palette.selectedText, palette.selection) >= 3.0);
        QVERIFY(palette.baseFontPointSize >= 6.0 && palette.baseFontPointSize <= 32.0);
        QVERIFY(palette.baseFontPointSize == palette.baseFontPointSize); // not NaN
    }
}

void ParserPropertyTest::linkClassificationIsTotalAndNeverLeavesTheRoot() {
    Mutator random(kSeed + 4);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::canonical(pathFor(temporary.path()));
    std::filesystem::create_directories(root / "sub");
    writeBytes(root / "real.md", "# real\n");
    writeBytes(root / "sub" / "deep.md", "# deep\n");
    writeBytes(root / "pic.png", "\x89PNG\r\n\x1a\n");
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    writeBytes(pathFor(outside.path()) / "secret.md", "# secret\n");
    writeBytes(pathFor(outside.path()) / "secret.png", "\x89PNG");

    omanotes::ResourcePolicy policy;
    policy.root = root;
    policy.noteDirectory = root / "sub";

    static const char* const pieces[] = {"https://",
                                         "http://",
                                         "javascript:",
                                         "data:",
                                         "file://",
                                         "//",
                                         "/",
                                         "../",
                                         "./",
                                         "..",
                                         "%2e%2e/",
                                         "%2f",
                                         "sub/",
                                         "real.md",
                                         "deep.md",
                                         "pic.png",
                                         "secret.md",
                                         "secret.png",
                                         "#",
                                         "#frag",
                                         "?q=1",
                                         "\\",
                                         "\xf0\x9f\x92\xa9",
                                         " ",
                                         "java script:",
                                         "omanotes-evil://",
                                         "mailto:",
                                         "x",
                                         "MD",
                                         ".md",
                                         "%00",
                                         "\t",
                                         "~/",
                                         "$HOME/"};
    const auto outsideRoot = std::filesystem::canonical(pathFor(outside.path()));

    int notes = 0;
    int externals = 0;
    for (int round = 0; round < kRounds; ++round) {
        QString url;
        const int parts = 1 + random.below(6);
        for (int i = 0; i < parts; ++i) {
            url += QString::fromUtf8(pieces[random.below(std::size(pieces))]);
        }
        if (random.below(5) == 0) {
            url += QString::fromUtf8(outside.path().toUtf8());
        }

        const auto action = omanotes::classifyLink(QUrl(url), policy);
        switch (action.kind) {
        case omanotes::LinkActionKind::OpenExternal:
            ++externals;
            QVERIFY2(action.external.scheme() == QStringLiteral("http") ||
                         action.external.scheme() == QStringLiteral("https"),
                     qPrintable(url));
            break;
        case omanotes::LinkActionKind::OpenNote: {
            ++notes;
            // A note action always names an existing .md file whose canonical
            // path is inside the root; never the outside directory.
            std::error_code error;
            const auto canonical = std::filesystem::canonical(action.note, error);
            QVERIFY2(!error, qPrintable(url));
            QVERIFY2(canonical.string().rfind(root.string() + "/", 0) == 0, qPrintable(url));
            QVERIFY2(canonical.string().rfind(outsideRoot.string(), 0) != 0, qPrintable(url));
            QVERIFY(canonical.extension() == ".md");
            break;
        }
        case omanotes::LinkActionKind::ScrollToAnchor:
            // A bare `#` scrolls to the top with an empty anchor; either way
            // the URL had a fragment and nothing else.
            QVERIFY2(QUrl(url).hasFragment(), qPrintable(url));
            break;
        case omanotes::LinkActionKind::Refuse:
            QVERIFY2(!action.reason.isEmpty(), qPrintable(url));
            break;
        }

        const auto image = omanotes::resolveImageSource(QUrl(url), policy);
        if (image) {
            std::error_code error;
            const auto canonical = std::filesystem::canonical(*image, error);
            QVERIFY2(!error, qPrintable(url));
            QVERIFY2(canonical.string().rfind(root.string() + "/", 0) == 0, qPrintable(url));
            const auto extension = QString::fromStdString(canonical.extension().string()).toLower();
            QVERIFY2(extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                         extension == ".gif" || extension == ".webp",
                     qPrintable(url));
        } else {
            QVERIFY2(!image.error().isEmpty(), qPrintable(url));
        }
    }
    QVERIFY2(notes > 0, "no generated link ever opened a note");
    QVERIFY2(externals > 0, "no generated link was ever external");
}

QTEST_GUILESS_MAIN(ParserPropertyTest)
#include "parser_property_test.moc"
