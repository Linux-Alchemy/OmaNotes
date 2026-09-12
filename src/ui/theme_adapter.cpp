#include "ui/theme_adapter.hpp"

#include <QFile>
#include <QHash>
#include <QStandardPaths>
#include <QStringView>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <utility>

namespace omanotes {

namespace {

/// One key of a flat TOML file, qualified by its section: `[font]` followed
/// by `base-size = 12` yields "font.base-size". This is not a TOML parser —
/// Omarchy's colors.toml and shell.toml are flat key/value files, and only
/// that subset is read; anything unrecognised is ignored, never an error.
QHash<QString, QString> readFlatToml(const std::filesystem::path& file) {
    QHash<QString, QString> values;
    std::ifstream input(file);
    if (!input) {
        return values;
    }
    QString section;
    std::string rawLine;
    while (std::getline(input, rawLine)) {
        const auto line = QString::fromStdString(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const auto equals = line.indexOf(u'=');
        if (equals <= 0) {
            continue;
        }
        const auto key = line.first(equals).trimmed();
        auto value = line.sliced(equals + 1).trimmed();
        if (const auto comment = value.indexOf(u'#'); comment >= 0 && !value.startsWith(u'"')) {
            value = value.first(comment).trimmed();
        }
        if (value.size() >= 2 && value.startsWith(u'"') && value.endsWith(u'"')) {
            value = value.mid(1, value.size() - 2);
        }
        if (key.isEmpty() || value.isEmpty()) {
            continue;
        }
        values.insert(section.isEmpty() ? key : section + u'.' + key, value);
    }
    return values;
}

double channel(float srgbF) {
    const auto srgb = static_cast<double>(srgbF);
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& colour) {
    return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF()) +
           0.0722 * channel(colour.blueF());
}

/// `amount` of `over` mixed into `base`, in sRGB.
QColor blend(const QColor& base, const QColor& over, double amount) {
    const auto mix = [amount](float a, float b) {
        const auto from = static_cast<double>(a);
        const auto to = static_cast<double>(b);
        return static_cast<float>(from + (to - from) * amount);
    };
    return QColor::fromRgbF(mix(base.redF(), over.redF()), mix(base.greenF(), over.greenF()),
                            mix(base.blueF(), over.blueF()));
}

/// The palette used when the theme is absent, unreadable, or refused: plain
/// readable colours in the spirit of Qt's stock dark and light looks.
ThemePalette fallbackPalette(bool dark) {
    ThemePalette palette;
    palette.dark = dark;
    if (dark) {
        palette.background = QColor(0x1e, 0x1e, 0x1e);
        palette.text = QColor(0xd4, 0xd4, 0xd4);
        palette.accent = QColor(0x4f, 0x9c, 0xf5);
        palette.selection = QColor(0x26, 0x4f, 0x78);
        palette.link = QColor(0x6f, 0xb1, 0xf5);
    } else {
        palette.background = QColor(0xfa, 0xfa, 0xfa);
        palette.text = QColor(0x20, 0x20, 0x20);
        palette.accent = QColor(0x1a, 0x63, 0xc4);
        palette.selection = QColor(0xbc, 0xd5, 0xf5);
        palette.link = QColor(0x15, 0x53, 0xaa);
    }
    palette.mutedText = blend(palette.text, palette.background, 0.4);
    palette.surface = blend(palette.background, palette.text, 0.05);
    palette.border = blend(palette.background, palette.text, 0.15);
    palette.inactiveSelection = blend(palette.selection, palette.background, 0.5);
    palette.selectedText = palette.text;
    palette.inactiveSelectedText = palette.text;
    return palette;
}

QColor parsedColour(const QHash<QString, QString>& values, const QString& key) {
    const auto found = values.constFind(key);
    if (found == values.constEnd()) {
        return {};
    }
    const QColor colour(*found);
    return colour.isValid() ? colour : QColor();
}

/// The first valid colour among the named keys — how one role reads both the
/// semantic Quattro schema and the legacy flat one.
QColor firstColour(const QHash<QString, QString>& values, std::initializer_list<QString> keys) {
    for (const auto& key : keys) {
        if (const auto colour = parsedColour(values, key); colour.isValid()) {
            return colour;
        }
    }
    return {};
}

/// Text on its background must reach 3.0:1 — below that the theme pair is
/// refused as a pair and the fallback's ground and ink are kept instead.
constexpr double kMinimumTextContrast = 3.0;
constexpr qreal kMinimumFontPoints = 6.0;
constexpr qreal kMaximumFontPoints = 32.0;

/// The first candidate readable on `ground`; when none reaches the bar,
/// plain black or white, whichever the ground's luminance calls for.
QColor readableOn(const QColor& ground, std::initializer_list<QColor> candidates) {
    for (const auto& candidate : candidates) {
        if (candidate.isValid() && contrastRatio(candidate, ground) >= kMinimumTextContrast) {
            return candidate;
        }
    }
    return relativeLuminance(ground) < 0.35 ? QColor(Qt::white) : QColor(Qt::black);
}

} // namespace

double contrastRatio(const QColor& a, const QColor& b) {
    const auto lighter = std::max(relativeLuminance(a), relativeLuminance(b));
    const auto darker = std::min(relativeLuminance(a), relativeLuminance(b));
    return (lighter + 0.05) / (darker + 0.05);
}

ThemeAdapter::ThemeAdapter(ThemeSources sources, QObject* parent)
    : QObject(parent), sources_(std::move(sources)), palette_(readPalette()) {}

ThemeAdapter::ThemeAdapter(QObject* parent) : ThemeAdapter(systemSources(), parent) {}

ThemeSources ThemeAdapter::systemSources() {
    const auto directory = [](QStandardPaths::StandardLocation location) {
        return std::filesystem::path(
            QFile::encodeName(QStandardPaths::writableLocation(location)).toStdString());
    };
    return {directory(QStandardPaths::GenericStateLocation) / "omarchy" / "current",
            directory(QStandardPaths::GenericConfigLocation) / "omarchy",
            directory(QStandardPaths::GenericConfigLocation) / "omanotes"};
}

const ThemePalette& ThemeAdapter::currentPalette() const noexcept { return palette_; }

QString ThemeAdapter::themeName() const {
    std::ifstream input(sources_.stateDir / "theme.name");
    std::string name;
    if (!input || !std::getline(input, name)) {
        return {};
    }
    return QString::fromStdString(name).trimmed();
}

void ThemeAdapter::refresh() {
    const auto next = readPalette();
    if (next == palette_) {
        return;
    }
    palette_ = next;
    emit paletteChanged(palette_);
}

ThemePalette ThemeAdapter::readPalette() const {
    const auto colours = readFlatToml(sources_.stateDir / "theme" / "colors.toml");
    const auto shell = readFlatToml(sources_.configDir / "shell.toml");
    const auto app = readFlatToml(sources_.appConfigDir / "config.toml");

    // Mode: stated by the semantic schema; otherwise judged from the
    // background's own luminance; dark when there is nothing to judge.
    const auto themeBackground = parsedColour(colours, QStringLiteral("background"));
    bool dark = true;
    if (const auto mode = colours.constFind(QStringLiteral("mode")); mode != colours.constEnd()) {
        dark = *mode != QStringLiteral("light");
    } else if (themeBackground.isValid()) {
        dark = relativeLuminance(themeBackground) < 0.35;
    }

    auto palette = fallbackPalette(dark);

    // Ground and ink are accepted only as a readable pair.
    const auto themeText = parsedColour(colours, QStringLiteral("foreground"));
    if (themeBackground.isValid() && themeText.isValid() &&
        contrastRatio(themeText, themeBackground) >= kMinimumTextContrast) {
        palette.background = themeBackground;
        palette.text = themeText;
        palette.mutedText = blend(palette.text, palette.background, 0.4);
        palette.surface = blend(palette.background, palette.text, 0.05);
        palette.border = blend(palette.background, palette.text, 0.15);
    }

    if (const auto accent = parsedColour(colours, QStringLiteral("accent")); accent.isValid()) {
        palette.accent = accent;
    }
    if (const auto selection = firstColour(
            colours, {QStringLiteral("selection"), QStringLiteral("selection_background")});
        selection.isValid()) {
        palette.selection = selection;
    }
    // A legacy theme may pair a light selection ground with its own dark ink
    // (catppuccin does); honour it, and never paint unreadable selected text.
    palette.selectedText = readableOn(
        palette.selection, {parsedColour(colours, QStringLiteral("selection_foreground")),
                            palette.text, palette.background});
    if (const auto muted =
            firstColour(colours, {QStringLiteral("muted"), QStringLiteral("dark_foreground"),
                                  QStringLiteral("color8")});
        muted.isValid() && contrastRatio(muted, palette.background) >= kMinimumTextContrast) {
        palette.mutedText = muted;
    }
    // The derived blend can dip below the bar when ground and ink barely
    // clear it themselves; step back toward full ink until it reads.
    palette.mutedText = readableOn(
        palette.background,
        {palette.mutedText, blend(palette.text, palette.background, 0.15), palette.text});
    if (const auto surface = parsedColour(colours, QStringLiteral("dark_background"));
        surface.isValid()) {
        palette.surface = surface;
    }
    if (const auto border = parsedColour(colours, QStringLiteral("lighter_background"));
        border.isValid()) {
        palette.border = border;
    }
    if (const auto link = firstColour(colours, {QStringLiteral("blue"), QStringLiteral("color4")});
        link.isValid()) {
        palette.link = link;
    } else {
        palette.link = palette.accent;
    }
    palette.inactiveSelection = blend(palette.selection, palette.background, 0.5);
    palette.inactiveSelectedText =
        readableOn(palette.inactiveSelection,
                   {palette.mutedText, palette.selectedText, palette.text, palette.background});

    // The desktop's size first, then the application's own override (ADR
    // 0017): the same key in the same syntax, in OmaNotes' config directory.
    // An absent or unparseable override changes nothing, as everywhere here.
    for (const auto* values : {&shell, &app}) {
        if (const auto size = values->constFind(QStringLiteral("font.base-size"));
            size != values->constEnd()) {
            bool numeric = false;
            const auto points = size->toDouble(&numeric);
            if (numeric) {
                palette.baseFontPointSize =
                    std::clamp(points, kMinimumFontPoints, kMaximumFontPoints);
            }
        }
    }
    return palette;
}

} // namespace omanotes
