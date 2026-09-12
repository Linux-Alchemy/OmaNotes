#ifndef OMANOTES_UI_THEME_ADAPTER_HPP
#define OMANOTES_UI_THEME_ADAPTER_HPP

#include <QColor>
#include <QObject>
#include <QString>

#include <filesystem>

namespace omanotes {

/// The semantic colour roles Omanotes paints with, plus the base text size.
/// Every colour is always valid: theme values are overlaid onto a complete
/// readable fallback, never trusted to be complete themselves.
struct ThemePalette {
    bool dark = true;
    QColor background;           ///< Editor and window ground.
    QColor surface;              ///< Sidebar, status area, buffer strip ground.
    QColor text;                 ///< Primary foreground.
    QColor mutedText;            ///< Secondary foreground: hints, pending keys.
    QColor accent;               ///< The theme's accent; focus marking.
    QColor selection;            ///< Selection ground in the focused pane.
    QColor selectedText;         ///< Ink readable on that selection ground.
    QColor inactiveSelection;    ///< Dimmed selection ground elsewhere.
    QColor inactiveSelectedText; ///< Ink readable on the dimmed ground.
    QColor border;               ///< Pane separators.
    QColor link;                 ///< Links in the reading view and help.
    qreal baseFontPointSize = 12.0;

    [[nodiscard]] bool operator==(const ThemePalette& other) const = default;
};

/// Where the desktop's theme and the application's own configuration are
/// read from. Injectable so tests point at fixtures; the defaults resolve
/// through XDG, never hard-coded paths.
struct ThemeSources {
    std::filesystem::path stateDir;  ///< …/omarchy/current — theme.name, theme/colors.toml
    std::filesystem::path configDir; ///< …/omarchy — shell.toml with [font] base-size
    /// …/omanotes — config.toml, whose [font] base-size overrides the shell's
    /// when present and valid (ADR 0017).
    std::filesystem::path appConfigDir;
};

/// WCAG contrast ratio between two colours, 1.0 (identical) to 21.0 (black
/// on white). Exposed so the verification suite measures with the same
/// arithmetic the adapter guards with.
[[nodiscard]] double contrastRatio(const QColor& a, const QColor& b);

/// Derives a complete palette from Omarchy Quattro's materialized current
/// theme (documented in docs/omarchy-integration.md), reading and never
/// writing. Understands both the semantic Quattro colors.toml schema and the
/// legacy flat one; anything missing, malformed, or unreadable falls back to
/// built-in readable colours, and a text/background pair without workable
/// contrast is refused as a pair.
class ThemeAdapter final : public QObject {
    Q_OBJECT

  public:
    explicit ThemeAdapter(ThemeSources sources, QObject* parent = nullptr);
    explicit ThemeAdapter(QObject* parent = nullptr);

    /// The Omarchy directories for this session, resolved through XDG.
    [[nodiscard]] static ThemeSources systemSources();

    [[nodiscard]] const ThemePalette& currentPalette() const noexcept;
    /// The active theme's name, or empty when none is recorded.
    [[nodiscard]] QString themeName() const;

    /// Re-read the sources; emits paletteChanged only when the result differs.
    void refresh();

  signals:
    void paletteChanged(const ThemePalette& palette);

  private:
    [[nodiscard]] ThemePalette readPalette() const;

    ThemeSources sources_;
    ThemePalette palette_;
};

} // namespace omanotes

#endif // OMANOTES_UI_THEME_ADAPTER_HPP
