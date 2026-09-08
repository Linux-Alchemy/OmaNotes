# Omarchy Theme and Scaling Integration

Block 6.2.1's survey of what Omarchy Quattro actually exposes, verified on the
target machine (omarchy 4.0.2, 2026-09-08), and the contract `ThemeAdapter`
implements against it. Omanotes only ever **reads** these files; theme
activation, generation, and every file below belong to Omarchy.

## Supported inputs

| Input | Path (XDG-resolved, never hard-coded) | What it holds |
| --- | --- | --- |
| Active theme name | `$XDG_STATE_HOME/omarchy/current/theme.name` | One line, e.g. `ethereal-black`. |
| Active theme palette | `$XDG_STATE_HOME/omarchy/current/theme/colors.toml` | Flat key/value colours (two schemas below). |
| Text scale | `$XDG_CONFIG_HOME/omarchy/shell.toml` → `[font] base-size` | Base font size in points. |

`$XDG_STATE_HOME` defaults to `~/.local/state`, `$XDG_CONFIG_HOME` to
`~/.config`; both are resolved through `QStandardPaths`, which honours the
environment variables — that is also how the tests point the adapter at
fixtures. Quattro **materializes** the active theme into
`state/omarchy/current/theme/` (it is a real directory, regenerated on theme
switch, not a symlink as in Omarchy 3.x) — which makes `colors.toml` a
watchable file for live theme changes (block 6.2.3).

## The two colors.toml schemas

Themes installed on Quattro carry one of two shapes, and the adapter reads
both:

- **Semantic (Quattro-native)** — e.g. `ethereal-black`, `catppuccin-dark`:
  `mode`, `accent`, `selection`, `muted`, layered `background` /
  `dark_background` / `lighter_background`, layered foregrounds, and named
  colours (`blue`, `red`, …).
- **Legacy flat** — e.g. `black`: `accent`, `cursor`, `foreground`,
  `background`, `selection_foreground` / `selection_background`, and
  `color0`–`color15`. No `mode` key.

Neither file uses nested TOML: both are flat key/value with at most a
`[section]` header (`shell.toml`'s `[font]`). The adapter deliberately reads
only that subset rather than pulling in a TOML dependency; unrecognised lines
are ignored, never an error.

## Role mapping

| Palette role | Semantic schema | Legacy schema | Fallback when absent/invalid |
| --- | --- | --- | --- |
| `dark` | `mode` | — | judged from `background` luminance; dark if unjudgeable |
| `background` | `background` | `background` | built-in ground |
| `text` | `foreground` | `foreground` | built-in ink |
| `accent` | `accent` | `accent` | built-in accent |
| `selection` | `selection` | `selection_background` | built-in selection |
| `mutedText` | `muted`, `dark_foreground` | `color8` | text blended 40% toward background |
| `surface` | `dark_background` | — | background blended 5% toward text |
| `border` | `lighter_background` | — | background blended 15% toward text |
| `link` | `blue` | `color4` | the accent |
| `inactiveSelection` | *always derived*: selection blended 50% toward background | | |

## Safety rules

- **Complete by construction:** theme values are overlaid onto a full readable
  fallback palette; a missing theme, missing key, or unparseable colour costs
  only that role, never a crash or an unstyled widget.
- **Contrast guard:** `foreground` on `background` must reach a 3.0:1 WCAG
  contrast ratio or the pair is refused *as a pair* (fallback ground and ink
  are kept; independent roles such as the accent still apply). The same bar
  applies to `mutedText` against the final background.
- **Text scale** is clamped to 6–32 points; non-numeric values fall back
  to 12.
- **Never write:** the adapter opens every file read-only and touches nothing
  under Omarchy's directories.

## Live change vs. restart (6.2.3 boundary)

`ThemeAdapter::refresh()` re-reads the sources and emits `paletteChanged`
only when the result differs. Wiring that to a file watch on
`current/theme/colors.toml` and `shell.toml` (the existing `FileWatcher`
already coalesces rename-replace bursts, which is exactly how Quattro
regenerates the theme directory) is block 6.2.3's decision: live-apply, or a
documented restart boundary.
