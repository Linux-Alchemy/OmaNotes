# Architecture

> Block 8.3.2. How OmaNotes is put together, for someone who directs the design and reads
> the diffs but does not write C++. Every claim here is about the code as it is on `main`;
> where a decision has a record, the ADR is named so the reasoning can be read separately.

## The shape in one paragraph

OmaNotes is a single process with a single window. The shell tells it which directory is the
**workspace root**; everything the program will read, list, search, or write lives under that
root, and the program refuses to cross it. Inside the window, KDE's KTextEditor does the
editing in Vi mode, and OmaNotes wraps it with the things an editor component does not
provide: a file sidebar, a buffer strip, a leader-key command language, a read-only Markdown
view, workspace search, atomic saves, external-change detection, and session restore. Note
text lives in exactly one place at a time, the editor widget for that buffer; the rest of the
program handles identity, structure, and disk, never a second copy of the words. State that
survives a restart goes to the XDG state directory, owner-only, per root. Configuration is read
from the XDG config directory and never written. There is no daemon, no network, no plugin
system, and no database.

```
   shell: omanotes [path] [--fresh]
              │
              ▼
   ┌───────────────────┐   resolves root + optional file
   │  main / launch    │──────────────────────────────────────┐
   └───────────────────┘                                      │
              │ builds                                        ▼
              ▼                                   ┌────────────────────┐
   ┌───────────────────────────────────────────┐  │  WorkspaceRoot     │ the trust boundary:
   │  MainWindow (Qt)                          │  │  contains? resolve │ every path is checked
   │  ├─ Sidebar ── FileTreeModel ─────────────┼─▶│  against it        │
   │  ├─ BufferStrip ── BufferRegistry         │  └────────────────────┘
   │  ├─ editor pane ── KTextEditor (Vi mode)  │             ▲
   │  ├─ MarkdownView (read-only projection)   │             │ paths, never bytes
   │  ├─ SearchPalette ── FileIndex/TextSearch │             │
   │  ├─ PrefixRouter ── CommandRegistry       │   ┌─────────┴──────────┐
   │  └─ status line, HelpOverlay              │   │ persistence        │
   └───────────────────────────────────────────┘   │  NoteReader        │ read, capped
              │ owns the session on its behalf     │  DocumentStore     │ where a save may go
              ▼                                    │  AtomicFileWriter  │ how bytes land
   ┌───────────────────────────────────────────┐   │  ConflictDetector  │ did the disk move?
   │  ApplicationController                    │   │  FileWatcher       │ who else is writing?
   │   SessionStore   snapshot per root        │   └────────────────────┘
   │   RecoveryStore  unsaved text per buffer  │
   │   InstanceLock   one owner per root       │──▶ ~/.local/state/omanotes/sessions/<id>/
   │   SessionRestorer what comes back         │
   └───────────────────────────────────────────┘
              ▲
   ThemeAdapter reads ~/.local/state/omarchy (theme), ~/.config/omarchy (font),
   ~/.config/omanotes (config.toml, keymap.json). Reads only, never writes.
```

## The parts

Source lives under `src/`, one directory per concern. The table says what each owns and,
just as importantly, what it is forbidden from doing, because most of the design is in the
refusals.

| Directory | Owns | Never |
| --- | --- | --- |
| `app/` | Launch argument grammar (`LaunchRequest`); the session's lifecycle on the window's behalf (`ApplicationController`); the leader-key router (`PrefixRouter`); the keymap file's validation (`Keymap`) | Touches note text; writes configuration |
| `core/` | Buffer identity, order, and which one is active (`BufferRegistry`); the command table and the leader sequences that reach it (`CommandRegistry`); the per-buffer view mode | Stores document text; writes to disk |
| `editor/` | The boundary to KTextEditor (`EditorAdapter`, an interface, and its one implementation); the `:w` family, redirected to the application's save path (`SaveCommand`) | Lets the editor component write a file itself |
| `persistence/` | Reading a note with a size cap and no symlink following (`NoteReader`); deciding where a save may land (`DocumentStore`); landing it atomically (`AtomicFileWriter`); noticing the disk changed under a buffer (`ConflictDetector`); the unsaved-text records (`RecoveryStore`) | Decides *which* buffer to save; shows UI |
| `session/` | The structural snapshot format and its versioning (`SessionSnapshot`); where snapshots live per root (`SessionStore`); the per-root instance lock; the restore order (`SessionRestorer`) | Contains note text in a snapshot; restores into a different root |
| `ui/` | The window and its panes; the theme adapter; the Markdown projection with its resource policy; the search palette; the help overlay | Reaches disk except through `persistence/` |
| `workspace/` | The root itself (`WorkspaceRoot`); the file tree; the bounded file index; literal text search; the external-change watcher | Follows symlinks out of the root; lists hidden directories |

Sizes, for a sense of weight: the window is the largest single file at about two thousand
lines, because it is where every pane, key route, and prompt meets; the session and
persistence layers together are about the same again; everything else is small.

## Who owns which data

This is the part worth internalising, because every bug class the project guards against is a
violation of one of these lines.

- **Note text** is owned by the editor widget of its buffer, and by the recovery record that
  shadows it while it is dirty. `BufferRegistry` knows a buffer's identity, display name,
  path, and view mode, and deliberately not its words, so there can never be two divergent
  copies in memory. The reading view is a projection rendered from the editor's text on
  demand, not a store.
- **The file on disk** is the source of truth for a saved note (outline principle 4). The
  program remembers a content hash of what it last read or wrote, so it can tell whether
  someone else has written since. Hashes, not timestamps: timestamps lie in exactly the cases
  that matter (a `touch`, a rename-replace save, two writes in one clock tick).
- **Structure** (which buffers are open, in what order, cursor and scroll positions, sidebar
  width, window geometry, which buffers are dirty) is owned by the session snapshot, a small
  versioned JSON document per root. It never contains a byte of note text, a search term, or
  a clipboard. See `docs/session-format.md`.
- **Unsaved text** is owned by recovery records: one file per dirty buffer, plaintext,
  owner-only, written two seconds after the last change, removed the moment the buffer is
  saved or discarded. The threat model accepts the plaintext with reasons (T-P4).
- **Configuration** (`keymap.json`, `config.toml`) is the user's. Read at startup, validated
  whole, never created or rewritten by the program (ADR 0008, ADR 0017).
- **The desktop's theme and font** are Omarchy's. Read from its state and config
  directories, overlaid onto a complete readable fallback so a missing or partial theme can
  never produce unreadable text. See `docs/omarchy-integration.md`.

## The trust boundary

The workspace root is the one boundary the whole program is built around. `WorkspaceRoot`
resolves the directory once at launch, canonically, and after that every path the program
handles is asked two questions: is it inside the root, and does it still resolve to itself
inside the root right now. Anything that fails is refused with a message, never silently
redirected.

What that buys, concretely:

- The sidebar and search never list or read outside the root, never follow a symlink out of
  it, and never descend into hidden directories.
- A save can only land inside the root (`DocumentStore`), and the final rename is checked
  again just before it happens, so a path swapped underneath the program between the check
  and the write is caught (ADR 0005).
- A note reopened or reloaded is re-validated first, and read through a descriptor that
  refuses symlinks and stops at 16 MiB, the same ceiling as recovery records, so no note the
  editor accepts is one recovery could not protect.
- The reading view's images must resolve inside the root and under a size budget; there is
  no remote fetching of any kind (ADR 0009).
- A session snapshot belongs to the root it was written in and is restored only there. A
  recovery record whose target path no longer lies under the root is reported and not
  restored (ADR 0010). Text from one workspace never appears while another is on screen.
- A launch on `/` or on the home directory is allowed, since Vim would allow it, but says so
  once on the status line, because everything beneath is then in scope.

The threat model (`docs/threat-model.md`) walks each of these with the code lines that
enforce them and the tests that pin them.

## Five journeys

### 1. Launch

`main` resolves the arguments into a `LaunchRequest`: a root (the current directory, the
directory given, or the parent of the file given) and possibly one file to focus, plus
`--fresh` if asked. A path that does not exist is exit code 2 with a message; the window is
never shown for a bad launch. The window is built next: theme read, sidebar populated, one
scratch buffer opened. Then, **before the window is shown**, `ApplicationController::start`
takes the root's instance lock, loads the root's snapshot, asks the window to reopen each
buffer in order, and brings back any dirty text from recovery records as modified buffers.
Only then is the window shown, so the user never watches an empty window rearrange itself.
The file named on the command line is opened or focused last, whatever the session had to
say about focus. Nothing is said about other roots (ADR 0016); state for roots that no longer
exist is swept quietly after seven days (ADR 0015).

### 2. A keystroke

Every key reaches the window's event filter first. If the leader (Space, ADR 0003) is
pressed in Normal mode, `PrefixRouter` collects the keys that follow, asking `CommandRegistry`
after each whether the sequence so far is complete, the start of something, or nothing, and
reporting on the status line so a half-typed sequence is never a silent wait. A complete
sequence runs exactly one command from the table; every keyboard, mouse, and leader route
ends in that table and nowhere else, which is how the help overlay can always show the truth.
Any other key goes to KTextEditor's Vi mode untouched. A handful of control keys the window
would otherwise claim are handed back to Vi synthetically so it sees them as its own. The
`:` line is the editor's, except that `:w`, `:e`, `:q` and their relatives are intercepted
and routed through the application (`SaveCommand`), so the editor component never writes a
file itself.

### 3. A save

`:w` asks the window for the active buffer's text, `DocumentStore` decides whether the target
path is a place a save may go (inside the root, a regular file or a new one, not a symlink to
elsewhere), and `ConflictDetector` compares the file's current content hash with the one the
buffer last read or wrote. A mismatch means someone else wrote it since; the plain save
refuses and says so, `:w!` overrides (ADR 0006). The bytes then go through
`AtomicFileWriter`: a temporary file beside the target, written and flushed, then renamed
over the target, then the directory flushed, so a crash at any point leaves either the old
file or the new one, never a torn one (ADR 0005). On success the buffer is marked clean, its
recovery record is deleted, and the snapshot is scheduled.

### 4. Someone else edits the file

Agents and other editors are first-class here (outline principle 6). `FileWatcher` watches
both the file and its directory, because an in-place write shows up on the file while a
rename-replace save, which is what Neovim does, swaps the inode and only the directory
notices. Events are coalesced, so an agent rewriting a note twenty times in a second produces
one notification carrying the final content. A clean buffer reloads silently with its cursor
kept; a dirty one is told, marked, and keeps its edits until the user chooses; a file deleted
under a buffer keeps the buffer, marked, and the next save writes it again (ADR 0006). The
path is re-validated before any re-read, so a watched file that has been replaced by a
symlink out of the root is refused.

### 5. A crash, and the launch after

While the window runs, `ApplicationController` checkpoints: two seconds after the desk last
changed, when the window loses focus, and on close, it writes every dirty buffer's text to
its recovery record and rewrites the snapshot. The two-second cadence is a ceiling on loss,
not a promise of none. If the process dies, the kernel drops the instance lock with it. The
next launch in that root takes the lock, reads the snapshot, and restores the buffers: clean
ones from disk, dirty ones from their records, marked modified, never silently written to the
file. A record the snapshot has lost track of (a crash between checkpoint and snapshot) is
still restored, as an orphan, and reported. A second instance on the same root while the
first is alive gets the structure only and leaves the records alone (ADR 0012).

## What is deliberately not here

- **No second copy of the text**, anywhere in memory. Everything that needs the words asks
  the editor.
- **No daemon, socket, or service.** One process per window; two windows on one root are
  arbitrated by a file lock, not a server.
- **No network.** The reading view fetches nothing remote; search is local disk; there is no
  telemetry and no update check. `docs/dependency-review.md` lists what the binary links.
- **No index files, database, or vault.** The file index is rebuilt from the directory when
  asked, bounded at twenty thousand entries; search reads the files. The workspace remains
  understandable as a directory of Markdown.
- **No plugin system and no shell execution** from inside the application.
- **No writes to configuration.** The program can be run for a year and leave nothing in
  `~/.config` it did not find there.

## Where to read next

| Question | Document |
| --- | --- |
| What exactly is in the snapshot and the recovery records, and their retention | `docs/session-format.md` |
| What is read from Omarchy, and what happens when it is missing | `docs/omarchy-integration.md` |
| The two configuration files | `docs/configuration.md` |
| Search limits and I/O budgets | `docs/search.md` |
| Every threat considered, with the enforcing code and tests | `docs/threat-model.md` |
| What Vim behaviour is accepted, configured around, or a known deviation | `docs/vim-acceptance.md` |
| How it is built, tested, packaged, and installed | `docs/packaging.md` |
| Why each of the above is the way it is | `docs/decisions/` (ADRs 0001 to 0017) |
