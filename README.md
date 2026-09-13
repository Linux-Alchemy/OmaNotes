# OmaNotes

OmaNotes is a minimal markdown editor built in Omarchy *for* Omarchy. It's keyboard-first and take some influence from neovim bindings. Just enough for the nvim folks to appreciate, but not so much that a newcomer can't figure out how to quit the application. If you do get stuck though there is a handy help menu built in too. While it is meant to be driven by the keyboard, it's also fully functional using the mouse if you're into that sort of thing. 
Meant to blend in, OmaNotes accepts the global theme and styling of Omarchy.

It is built on Qt 6 Widgets and KDE's KTextEditor with its Vi mode, so editing is Vim and everything around the editor answers to a Space leader. Notes are ordinary `.md` files in ordinary directories. A workspace is a directory, not an Obsidian-like vault: there is no database, no index file, and nothing to migrate.


## Installing

Omarchy on Arch Linux under Wayland is the only supported platform. The GitHub repository is
the distribution; there is no AUR package and no prebuilt binary (ADR 0013).

```sh
git clone https://github.com/Linux-Alchemy/OmaNotes.git
cd OmaNotes/packaging/arch
makepkg -si
```

`makepkg -s` installs the build tools it needs (`git`, `cmake`, `ninja`, `clang`,
`extra-cmake-modules`); the runtime dependencies are `qt6-base`, `ktexteditor`, and
`hicolor-icon-theme`. The package clones the repository afresh and builds the tip of `main`,
runs the full test suite and the binary hardening check against the exact build it is about
to package, and then installs four files: the binary, a desktop entry, an icon, and the
licence. Nothing else.

**Upgrade:** `git pull` in the clone, then `makepkg -si` again in `packaging/arch`.

**Uninstall:** `sudo pacman -R omanotes-git`. Your notes are never touched. Session state
under `~/.local/state/omanotes/` and configuration under `~/.config/omanotes/` are yours,
not the package's, and stay until you delete them.

`docs/packaging.md` has the route without a package and the reasoning behind both.

## Launching

OmaNotes appears in the Omarchy launcher as **OmaNotes**. From a terminal, the shell sets
the context and OmaNotes respects it:

| Command | What happens |
| --- | --- |
| `omanotes` | The current directory is the workspace. Its last session is restored; a scratch buffer opens when there is none. |
| `omanotes ~/Notes` | `~/Notes` is the workspace, same rules. |
| `omanotes idea.md` | The current directory is the workspace; `idea.md` is opened or focused inside the restored session. |
| `omanotes projects/idea.md` | As above, for a nested file. |
| `omanotes ~/Notes/projects/idea.md` | The file's own directory is the workspace. |
| `omanotes --fresh [target]` | Open without restoring, and without overwriting the saved session on close. |

Files outside the workspace are refused, wherever they are met: sidebar, search, open,
save, reading-view images. A symlink that leads out of the workspace counts as outside.

## The keys

Editing is KTextEditor's Vi mode: motions, operators, text objects, visual modes,
registers, search, dot-repeat, and macros work as a Vim user expects. The application's
own commands live behind **Space**, pressed in Normal mode or in the sidebar. **Space ?**
shows every command available where you are, with its current keys; so does the `?`
button in the corner.

| Keys | Command |
| --- | --- |
| `Space e` | Show or hide the sidebar |
| `Ctrl+H`, `Ctrl+L` | Focus the sidebar, focus the editor |
| `Space f f` or `Space Space` | Find a file by fuzzy name |
| `Space /` | Search the text of every note in the workspace |
| `Space f n` | New scratch buffer |
| `Space b n`, `Space b p` | Next and previous buffer, as do `Shift+L` and `Shift+H` |
| `Space b d` | Close the buffer; refuses if it has unsaved changes |
| `Space b D` | Close the buffer and discard its changes |
| `Space m` | Toggle the reading view |
| `Ctrl+S` | Save, in any mode |
| `Ctrl+D`, `Ctrl+U` | Half a page down or up, in the editor and the reading view |
| `Space ?` | Help |

In the sidebar, `j` and `k` move, `h` and `l` collapse and expand a directory, and `Enter`
opens a file.

The Vim command line belongs to the application, so the verbs that touch disk go through
its atomic writer and its root checks:

| Command | What happens |
| --- | --- |
| `:w` | Save. A scratch buffer has no name, so `:w path.md` gives it one, relative to the workspace; `Ctrl+S` opens a naming field instead. |
| `:w!` | Save even when the note changed on disk since you opened it, or is read-only. |
| `:e` | Reload the note from disk. `:e!` discards your edits first. |
| `:q` | Quit. Unsaved work gets a Save / Discard / Cancel prompt. `:q!` discards. `:wq` and `:x` save first. |

Where it deliberately differs from Vim: `Shift+H` and `Shift+L` switch buffers when more
than one is open; `Ctrl+V` pastes in every mode because Omarchy's Super+V arrives as
`Ctrl+V`, so visual block is `Ctrl+Q`; `Ctrl+C` over a selection copies. The full list,
with the reasoning, is in `docs/limitations.md` and `docs/vim-acceptance.md`.

## Configuration

Two files, both under `~/.config/omanotes/`, both read once at launch. OmaNotes never
creates or writes them: a fresh install has no such directory, and anything you put there
survives every upgrade untouched. Restart after editing either.

**Font size**, in `config.toml`. When the file is absent, OmaNotes uses the desktop's own
size from Omarchy's `shell.toml`. To choose your own:

```sh
mkdir -p ~/.config/omanotes
printf '[font]\nbase-size = 14\n' > ~/.config/omanotes/config.toml
```

The value is in points, for the editor, sidebar, status line, and reading view, clamped to
6 to 32.

**Keys**, in `keymap.json`. Leader routes and direct shortcuts can each be rebound; omitted
commands keep their defaults, and `Space ?` always shows what is in effect:

```json
{
  "leaderBindings": { "search.files": ["p"] },
  "shortcuts": { "file.save": "Ctrl+Alt+Shift+S" }
}
```

A file that fails validation is rejected as a whole, the defaults stay, and a warning at
launch names the problem. Every field, rule, and command id is in `docs/configuration.md`.

## Reading view

`Space m` renders the current note as a document. The source is untouched; it is a
projection, toggled back with the same key. Images must live inside the workspace, under a
size budget. Nothing is fetched from the network, ever, and there is no scripting. The
rules are ADR 0009.

## Sessions and recovery

Closing the window records the workspace's desk: open buffers and their order, the active
one, cursor and scroll positions, sidebar state, the reading view. The next launch on the
same directory puts it all back before the window is shown.

Unsaved text is kept separately, as recovery records, checkpointed two seconds after the
last change, on focus loss, and on close. So:

- **A crash, a kill, or the compositor's close** loses at most two seconds of typing. The
  next launch restores the buffers, still modified, exactly as they were. Unsaved is not
  the same as lost.
- **A restored buffer is never written to disk for you.** It comes back dirty; `:w` when
  you are ready. If the note changed on disk in the meantime, it comes back in conflict and
  `:w` refuses until you choose `:w!` or `:e!`.
- **Nothing is written into the workspace** except the notes you save. All state lives under
  `~/.local/state/omanotes/`, owner-only, one directory per workspace root.
- **State follows the root.** Each directory announces its own unsaved work when opened and
  nothing else does. State for a directory that no longer exists is removed after seven
  days.

`docs/session-format.md` describes the files and every fail-safe rule.

## When a note changes under you

OmaNotes watches every open note. If a clean buffer's file changes on disk, the buffer
reloads. If a modified buffer's file changes, your edits are kept and the status line says
so, with the two ways out: `:w!` overwrites the disk version, `:e!` takes it and drops
yours. A note deleted on disk keeps its buffer, and `:w` writes it again. This is what makes
it safe to let an agent or a script edit the same directory (ADR 0006).

## Troubleshooting

**It is not in the launcher.** `pacman -Ql omanotes-git` should list
`/usr/share/applications/OmaNotes.desktop` and the icon. If it does, log out and back in so
the launcher re-reads its entries.

**A warning about `keymap.json` at launch.** The file was rejected as a whole and the
defaults are in use; the warning names the file, the property, or the byte position of the
parse error. Fix it and restart. No partial keymap is ever installed.

**"No file name; try :w path.md".** A scratch buffer needs a name before it can be saved.
The path is relative to the workspace; `Ctrl+S` offers a field to type it in.

**"'readonly' option is set (add ! to override)".** The note is not writable by you. `:w!`
writes it anyway, keeping its permissions.

**"changed on disk; your edits are kept".** See the section above. Nothing has been lost.

**A second window says unsaved changes are held by another OmaNotes.** Two windows on the
same workspace share the layout but not the unsaved text: the first window owns it until it
closes (ADR 0012).

**A note refuses to open.** Notes above 16 MiB are refused, as is anything outside the
workspace or reached through a symlink that leaves it.

**An image is missing in the reading view.** It is outside the workspace, over the size
budget, or remote. Only workspace-local images are shown.

**The theme or text size looks wrong.** OmaNotes reads Omarchy's active theme and
`shell.toml` font size and follows them live. A theme whose foreground and background do
not reach a readable contrast is refused as a pair and the built-in palette is used. Your
own size goes in `config.toml`; see Configuration above.

Errors and the keymap warning also go to Qt's log: stderr when launched from a terminal,
the journal when launched from the desktop.

## Building from source

```sh
cmake --preset release && cmake --build --preset release
./build/release/src/omanotes .
```

Requires Qt 6.8+, KF6 TextEditor 6.9+, Extra CMake Modules, CMake 3.28+, Ninja, and
Clang. `docs/testing.md` covers the three presets, the nine-command gate every change
passes before merge, and what each check proves.

## Documentation

| Page | What it answers |
| --- | --- |
| `docs/architecture.md` | How the pieces fit, for someone who does not read C++ |
| `docs/configuration.md` | `config.toml` and `keymap.json`, every field |
| `docs/session-format.md` | What is stored, where, and how restore fails safely |
| `docs/limitations.md` | What it does not do, and where it is not Vim |
| `docs/threat-model.md` | What it defends against, and what was accepted |
| `docs/testing.md` | Building, the gate, the test suites |
| `docs/packaging.md` | The package and the route without one |
| `docs/release.md` | The version, and how a release is cut |
| `docs/decisions/` | Every design decision, numbered, with the alternatives rejected |
| `OUTLINE.md`, `PLAN.md` | What was promised, and how it was built |

## Licence

MIT. See `LICENSE`.
