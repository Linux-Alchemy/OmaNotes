#include "ui/theme_adapter.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path pathFor(const QString& text) {
    return std::filesystem::path(QFile::encodeName(text).toStdString());
}

void writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
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

constexpr auto kLegacyTheme = R"(accent = "#e68e0d"
cursor = "#eaeaea"
foreground = "#bebebe"
background = "#000000"
selection_foreground = "#bebebe"
selection_background = "#333333"

color4 = "#5b8bd6"
color8 = "#888888"
)";

} // namespace

class ThemeAdapterTest : public QObject {
    Q_OBJECT

  private:
    omanotes::ThemeSources sourcesIn(const QTemporaryDir& directory) {
        const auto root = pathFor(directory.path());
        return {root / "state" / "omarchy" / "current", root / "config" / "omarchy",
                root / "config" / "omanotes"};
    }

  private slots:
    void readsTheSemanticQuattroSchema();
    void readsTheLegacyFlatSchema();
    void missingThemeYieldsAReadableFallback();
    void refusesAnUnreadableTextBackgroundPair();
    void ignoresMalformedValuesRoleByRole();
    void readsAndClampsTheTextScale();
    void appConfigOverridesTheShellTextScale();
    void refreshEmitsOnlyOnChange();
    void representativeThemesStayReadable();
    void honoursALegacyLightSelectionInk();
};

void ThemeAdapterTest::readsTheSemanticQuattroSchema() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.stateDir / "theme" / "colors.toml", kSemanticTheme);
    writeFile(sources.stateDir / "theme.name", "ethereal-black\n");

    omanotes::ThemeAdapter adapter(sources);
    const auto& palette = adapter.currentPalette();
    QVERIFY(palette.dark);
    QCOMPARE(adapter.themeName(), QStringLiteral("ethereal-black"));
    QCOMPARE(palette.background, QColor(QStringLiteral("#000000")));
    QCOMPARE(palette.text, QColor(QStringLiteral("#ffcead")));
    QCOMPARE(palette.accent, QColor(QStringLiteral("#7d82d9")));
    QCOMPARE(palette.selection, QColor(QStringLiteral("#252e56")));
    QCOMPARE(palette.mutedText, QColor(QStringLiteral("#6d7db6")));
    QCOMPARE(palette.surface, QColor(QStringLiteral("#040816")));
    QCOMPARE(palette.border, QColor(QStringLiteral("#131a3a")));
    QCOMPARE(palette.link, QColor(QStringLiteral("#7d82d9")));
    // The dimmed selection sits between the selection and the ground.
    QVERIFY(palette.inactiveSelection != palette.selection);
    QVERIFY(palette.inactiveSelection != palette.background);
}

void ThemeAdapterTest::readsTheLegacyFlatSchema() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.stateDir / "theme" / "colors.toml", kLegacyTheme);

    omanotes::ThemeAdapter adapter(sources);
    const auto& palette = adapter.currentPalette();
    QVERIFY(palette.dark); // No mode key: judged from the background.
    QCOMPARE(palette.background, QColor(QStringLiteral("#000000")));
    QCOMPARE(palette.text, QColor(QStringLiteral("#bebebe")));
    QCOMPARE(palette.accent, QColor(QStringLiteral("#e68e0d")));
    QCOMPARE(palette.selection, QColor(QStringLiteral("#333333")));
    QCOMPARE(palette.mutedText, QColor(QStringLiteral("#888888")));
    QCOMPARE(palette.link, QColor(QStringLiteral("#5b8bd6")));
}

void ThemeAdapterTest::missingThemeYieldsAReadableFallback() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    omanotes::ThemeAdapter adapter(sourcesIn(directory));
    const auto& palette = adapter.currentPalette();
    QVERIFY(adapter.themeName().isEmpty());
    for (const auto& colour :
         {palette.background, palette.surface, palette.text, palette.mutedText, palette.accent,
          palette.selection, palette.inactiveSelection, palette.border, palette.link}) {
        QVERIFY(colour.isValid());
    }
    QVERIFY(palette.background != palette.text);
    QCOMPARE(palette.baseFontPointSize, 12.0);
}

void ThemeAdapterTest::refusesAnUnreadableTextBackgroundPair() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.stateDir / "theme" / "colors.toml",
              "background = \"#101010\"\nforeground = \"#181818\"\naccent = \"#e68e0d\"\n");

    omanotes::ThemeAdapter adapter(sources);
    const auto& palette = adapter.currentPalette();
    // Ground and ink come from the fallback pair; the accent still applies.
    QVERIFY(palette.background != QColor(QStringLiteral("#101010")));
    QVERIFY(palette.text != QColor(QStringLiteral("#181818")));
    QCOMPARE(palette.accent, QColor(QStringLiteral("#e68e0d")));
}

void ThemeAdapterTest::ignoresMalformedValuesRoleByRole() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.stateDir / "theme" / "colors.toml",
              "background = \"#000000\"\nforeground = \"#d0d0d0\"\n"
              "accent = \"not-a-colour\"\nselection = \"#252e56\"\n");

    omanotes::ThemeAdapter adapter(sources);
    const auto& palette = adapter.currentPalette();
    QCOMPARE(palette.background, QColor(QStringLiteral("#000000")));
    QCOMPARE(palette.selection, QColor(QStringLiteral("#252e56")));
    QVERIFY(palette.accent.isValid()); // The fallback accent, not garbage.
}

void ThemeAdapterTest::readsAndClampsTheTextScale() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.configDir / "shell.toml", "[font]\nbase-size = 14\n");

    omanotes::ThemeAdapter adapter(sources);
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 14.0);

    writeFile(sources.configDir / "shell.toml", "[font]\nbase-size = 400\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 32.0);

    writeFile(sources.configDir / "shell.toml", "[font]\nbase-size = nonsense\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 12.0);
}

void ThemeAdapterTest::appConfigOverridesTheShellTextScale() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.configDir / "shell.toml", "[font]\nbase-size = 14\n");

    // No override: the desktop's size, as before.
    omanotes::ThemeAdapter adapter(sources);
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 14.0);

    // The override wins, in the same syntax as Omarchy's own file.
    writeFile(sources.appConfigDir / "config.toml", "# OmaNotes\n[font]\nbase-size = 10\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 10.0);

    // Clamped like the desktop's value.
    writeFile(sources.appConfigDir / "config.toml", "[font]\nbase-size = 2\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 6.0);

    // Unparseable: back to the desktop's size, never an error.
    writeFile(sources.appConfigDir / "config.toml", "[font]\nbase-size = large\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 14.0);

    // A file that says nothing about fonts changes nothing.
    writeFile(sources.appConfigDir / "config.toml", "[editor]\nwrap = true\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 14.0);

    // No desktop value at all: the override still applies.
    std::filesystem::remove(sources.configDir / "shell.toml");
    writeFile(sources.appConfigDir / "config.toml", "[font]\nbase-size = 9\n");
    adapter.refresh();
    QCOMPARE(adapter.currentPalette().baseFontPointSize, 9.0);
}

void ThemeAdapterTest::refreshEmitsOnlyOnChange() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    writeFile(sources.stateDir / "theme" / "colors.toml", kSemanticTheme);

    omanotes::ThemeAdapter adapter(sources);
    QSignalSpy changes(&adapter, &omanotes::ThemeAdapter::paletteChanged);

    adapter.refresh(); // Nothing changed on disk: no signal.
    QCOMPARE(changes.count(), 0);

    writeFile(sources.stateDir / "theme" / "colors.toml", kLegacyTheme);
    adapter.refresh();
    QCOMPARE(changes.count(), 1);
    QCOMPARE(adapter.currentPalette().accent, QColor(QStringLiteral("#e68e0d")));
}

void ThemeAdapterTest::representativeThemesStayReadable() {
    // Block 6.2.4: representative themes of both schemas and both modes must
    // yield readable text, muted text, selection inks, and a visible accent —
    // measured with the same contrast arithmetic the adapter guards with.
    const auto semanticLight = std::string("mode = \"light\"\n"
                                           "accent = \"#1a63c4\"\n"
                                           "selection = \"#cbdcf5\"\n"
                                           "muted = \"#6a6a60\"\n"
                                           "background = \"#f2f0e8\"\n"
                                           "dark_background = \"#e8e6dc\"\n"
                                           "foreground = \"#2a2a2a\"\n"
                                           "blue = \"#1a63c4\"\n");
    const auto legacyLightSelection = std::string("accent = \"#89b4fa\"\n"
                                                  "foreground = \"#cdd6f4\"\n"
                                                  "background = \"#010101\"\n"
                                                  "selection_foreground = \"#010101\"\n"
                                                  "selection_background = \"#f5e0dc\"\n"
                                                  "color4 = \"#89b4fa\"\n"
                                                  "color8 = \"#585b70\"\n");
    const std::pair<const char*, std::string> themes[] = {
        {"semantic dark", std::string(kSemanticTheme)},
        {"semantic light", semanticLight},
        {"legacy dark", std::string(kLegacyTheme)},
        {"legacy light selection", legacyLightSelection},
        {"absent theme (fallback)", std::string()},
    };
    for (const auto& [label, contents] : themes) {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto sources = sourcesIn(directory);
        if (!contents.empty()) {
            writeFile(sources.stateDir / "theme" / "colors.toml", contents);
        }
        omanotes::ThemeAdapter adapter(sources);
        const auto& palette = adapter.currentPalette();
        const auto check = [&](const QColor& ink, const QColor& ground, double bar,
                               const char* what) {
            const auto ratio = omanotes::contrastRatio(ink, ground);
            QVERIFY2(ratio >= bar, qPrintable(QStringLiteral("%1: %2 is %3:1 against %4")
                                                  .arg(QLatin1String(label), QLatin1String(what))
                                                  .arg(ratio, 0, 'f', 2)
                                                  .arg(ground.name())));
        };
        check(palette.text, palette.background, 3.0, "text on background");
        check(palette.mutedText, palette.background, 3.0, "muted text on background");
        check(palette.selectedText, palette.selection, 3.0, "selected text on selection");
        check(palette.inactiveSelectedText, palette.inactiveSelection, 3.0,
              "inactive selected text on dimmed selection");
        check(palette.accent, palette.background, 2.0, "accent on background");
        check(palette.link, palette.background, 2.0, "link on background");
    }
}

void ThemeAdapterTest::honoursALegacyLightSelectionInk() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sources = sourcesIn(directory);
    // Catppuccin's legacy schema pairs a cream selection ground with its own
    // near-black ink; painting the normal foreground there is unreadable.
    writeFile(sources.stateDir / "theme" / "colors.toml", "foreground = \"#cdd6f4\"\n"
                                                          "background = \"#010101\"\n"
                                                          "selection_foreground = \"#010101\"\n"
                                                          "selection_background = \"#f5e0dc\"\n");

    omanotes::ThemeAdapter adapter(sources);
    const auto& palette = adapter.currentPalette();
    QCOMPARE(palette.selection, QColor(QStringLiteral("#f5e0dc")));
    QCOMPARE(palette.selectedText, QColor(QStringLiteral("#010101")));
}

QTEST_MAIN(ThemeAdapterTest)
#include "theme_adapter_test.moc"
