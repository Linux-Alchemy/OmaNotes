# ADR 0017: Application configuration lives in `~/.config/omanotes/config.toml`

- **Status:** Accepted (2026-09-12, Matt's ruling during the Phase 8 daily-use trial)
- **Resolves:** the font-size question raised in the trial; the plan-protocol requirement that a
  new configuration format be ruled on before code (PLAN.md protocol item 4)
- **Related:** ADR 0008 (keymap format), `docs/omarchy-integration.md` (what is read from the
  desktop)

## Context

OmaNotes takes its text size from Omarchy's global `[font] base-size` in
`~/.config/omarchy/shell.toml`, which on Matt's machine is 12. That value sizes the bar and
menus, and 12 is right for them; in the editor it is too large next to his terminals at 9 and
10 points. Lowering the desktop value shrinks the bar; reading the terminal's size raises the
question of which terminal. What is wanted is an application-specific setting that defers to
the desktop when it is not given.

OmaNotes already has one configuration file, `~/.config/omanotes/keymap.json` (ADR 0008), in
the directory the XDG base specification assigns the application. Omarchy's own convention is a
per-application directory under `~/.config` holding flat TOML files, read at startup.

## Decision

**A flat TOML file, `~/.config/omanotes/config.toml`, read once at startup, in the same
syntax as Omarchy's own files.** Its first and so far only setting:

```toml
[font]
base-size = 10
```

- Precedence: this file's value, else the desktop's `shell.toml` value, else 12. Each is
  clamped to 6 to 32 points, as the desktop's value already was.
- An absent file, an absent key, or an unparseable value changes nothing and raises no error,
  exactly as the theme adapter treats every other input.
- The reader is the existing flat-TOML reader in `theme_adapter.cpp`: `[section]` headers and
  `key = value` lines, comments and quoted strings understood, nothing else. It is not a TOML
  parser and does not claim to be.
- The file is never created or rewritten by the program. Restart to apply, as with the keymap.

The keymap stays in `keymap.json`. Moving it would break every existing keymap for no gain.

## Consequences

- The text size can be tuned per application without touching the desktop. Matt's stated
  reason for wanting a file rather than a flag: it is the Omarchy way, and it gives later
  settings a home should the application grow.
- `ThemeSources` gains a third path, the application's config directory, so tests inject it as
  they inject the desktop's.
- Anything added to `config.toml` later must fit the flat subset, or the reader must grow
  first. That is a deliberate brake on configuration sprawl.
- Live reload and an in-app zoom are not part of this decision. Either would be a new
  feature and Phase 8 is not the phase.

## Rejected alternatives

- **Lower the desktop's `base-size`.** Shrinks the bar and menus, which are sized correctly.
- **Read the terminal's font size.** Four terminals on the machine, three sizes; no answer.
- **A command-line flag or environment variable.** Works, but every launch from the desktop
  entry would miss it, and it is not how Omarchy applications are configured.
- **Put the setting in `keymap.json`.** Wrong file by name, and JSON is not the desktop's
  configuration syntax.
