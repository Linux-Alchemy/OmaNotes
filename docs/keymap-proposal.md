# P5.3 keymap proposal — implemented

The proposed file is `$XDG_CONFIG_HOME/omanotes/keymap.json`, with
`~/.config/omanotes/keymap.json` as the standard fallback. Omanotes would only
read this file; it would not create or rewrite user configuration.

```json
{
  "leaderBindings": {
    "search.files": ["f f", "Space"]
  },
  "shortcuts": {
    "file.save": "Ctrl+S"
  }
}
```

`leaderBindings` maps a known command id to its complete replacement list of
keys after Space. Omitted command ids retain their existing bindings. `Space`
is the literal second space key. The default `Space ?` help route stays
reserved and reachable.

`shortcuts` maps an application command id to a direct key sequence. Validation
must reject duplicate, invalid, unreachable and editor-conflicting mappings.
Existing Vim grammar (`:w`, `:e`, etc.) remains owned by the editor integration.
No value invokes a program or evaluates code.

An invalid configuration would report the file and offending property (or JSON
parse location), retain all default bindings, and allow launch. Partial updates
would be validated as a complete candidate keymap before becoming active.

This proposal was carried forward after PR #10 was reviewed and merged and
Matt requested continuation. The implemented contract is documented in
[configuration.md](configuration.md) and [ADR 0008](decisions/0008-keymap-format-and-safety.md).
Phase 5 hands-on acceptance remains pending.
