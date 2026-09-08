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
        return {root / "state" / "omarchy" / "current", root / "config" / "omarchy"};
    }

  private slots:
    void readsTheSemanticQuattroSchema();
    void readsTheLegacyFlatSchema();
    void missingThemeYieldsAReadableFallback();
    void refusesAnUnreadableTextBackgroundPair();
    void ignoresMalformedValuesRoleByRole();
    void readsAndClampsTheTextScale();
    void refreshEmitsOnlyOnChange();
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

QTEST_MAIN(ThemeAdapterTest)
#include "theme_adapter_test.moc"
