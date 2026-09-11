# Key configuration

Omanotes reads `$XDG_CONFIG_HOME/omanotes/keymap.json` at startup. If
`XDG_CONFIG_HOME` is unset, the standard location is
`~/.config/omanotes/keymap.json`. It never creates or rewrites this file.
Restart Omanotes after changing it.

```json
{
  "leaderBindings": {
    "search.files": ["p"],
    "buffer.new": ["n"]
  },
  "shortcuts": {
    "file.save": "Ctrl+Alt+Shift+S"
  }
}
```

This example replaces the file finder routes with `Space p`, creates a new
buffer with `Space n`, and moves Save to `Ctrl+Alt+Shift+S`. Omitted commands keep
their defaults. `Space ?` and the `?` button show the effective keys.

## Leader bindings

The `leaderBindings` object maps a command id to its replacement list of
sequences after Space. Each list contains 1–8 sequences; each sequence contains
1–4 printable ASCII keys separated by single spaces. Case matters: `b d` and
`b D` are different. Spell the space key `Space`.

Commands cannot share a sequence, and a complete sequence cannot also be a
prefix of another. For example, `f` and `f f` cannot both be bound. Override
both affected commands in the same file when swapping their keys. Empty lists
are rejected so an override cannot silently remove all of a command's routes.
`help.show` must retain `?`; it may gain additional routes.

The Space leader works in editor Normal mode and in the sidebar. It does not
intercept text in Insert/Visual/Replace modes or in a command line, search
field, naming prompt or dialog.

## Direct shortcuts

The `shortcuts` object maps a command id to one direct key combination, using
[Qt's portable key-sequence spelling](https://doc.qt.io/qt-6/qkeysequence.html#SequenceFormat-enum).
New combinations use `Ctrl+Alt+letter/digit` or
`Ctrl+Alt+Shift+letter/digit`. Omanotes also supports its existing exceptions:

| Command | Default | Other supported exception |
| --- | --- | --- |
| `file.save` | `Ctrl+S` | `Ctrl+Shift+S` |
| `edit.paste` | `Ctrl+V` (any mode; identical to Omarchy's Super+V) | — |
| `edit.copy` | `Ctrl+C` (acts only over a selection) | — |
| `editor.visual-block` | `Ctrl+Q` (Normal mode; Vi's visual block) | — |
| `view.half-page-down` | `Ctrl+D` (Normal mode and the reading view; Vi's half page) | — |
| `view.half-page-up` | `Ctrl+U` (Normal mode and the reading view; Vi's half page) | — |
| `pane.sidebar` | `Ctrl+H` | — |
| `pane.editor` | `Ctrl+L` in the sidebar | — |
| `buffer.next` | `Shift+L` | `Shift+H` |
| `buffer.previous` | `Shift+H` | `Shift+L` |

The normal collision checks still apply when swapping exceptions. New direct
shortcuts cannot use bare typing keys, canonical Vim Ctrl commands such as
`Ctrl+B`, Super/Meta, Alt-only dialog accelerators or multi-key chords. They
also cannot collide with, or prefix, an existing KTextEditor action shortcut.

Direct application shortcuts are inactive in dialogs and text-entry controls.
In the editor, Save works in every editing mode; other application shortcuts
work in Normal mode. `Shift+H/L` remains editor-only and goes to Vim when only
one buffer exists. Default `Ctrl+L` remains sidebar-only. The new mapped pane
shortcut replaces the old one; it does not leave a second hardcoded route.

Desktop-level shortcuts and non-US keyboard layouts can affect which physical
combinations reach Omanotes. Their configuration is not changed by this file.

## Command ids

| Id | Action |
| --- | --- |
| `file.save` | Save the active buffer |
| `buffer.new` | New scratch buffer |
| `buffer.close` | Close, refusing unsaved changes |
| `buffer.close.discard` | Deliberately discard and close |
| `buffer.next`, `buffer.previous` | Cycle buffers |
| `pane.sidebar` | Focus the sidebar |
| `pane.sidebar.toggle` | Show or hide the sidebar |
| `pane.editor` | Focus the editor |
| `search.files` | Find Markdown files |
| `search.text` | Search saved Markdown text |
| `help.show` | Show contextual commands |
| `view.half-page-down`, `view.half-page-up` | Scroll half a screen; Vi's `Ctrl+D`/`Ctrl+U`, also in the reading view |
| `view.reading` | Reserved; disabled until Phase 6 |

The registry's `file.open` and `buffer.show` require targets supplied by mouse
or result selection and cannot be bound directly. Unknown or disabled commands
never execute. Vim's `:w`, `:w!`, `:e` and other intercepted verbs keep their
existing grammar; this file does not configure them.

## Invalid configuration

The file must be a JSON object no larger than 64 KiB. Unknown fields, wrong
value types, unknown commands, duplicate/shadowed mappings, unsafe shortcuts,
editor collisions and loss of `Space ?` reject the whole candidate. No partial
keymap is installed.

Omanotes launches with all defaults and displays a persistent, plain-text
warning identifying the file and property, or the JSON parse byte position.
The same warning goes to Qt's log (stderr or the desktop journal, depending on
the environment). Correct the file and restart to clear it.
A missing file simply uses the defaults without creating anything.

No configuration value is executed as a command, script, expression or shell
hook. No dependency, plugin system or hot-reload mechanism is involved.
