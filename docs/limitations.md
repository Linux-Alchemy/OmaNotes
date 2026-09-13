# Known limitations

> Block 8.3.3. What OmaNotes does not do, does within a bound, or does differently from what
> a Vim or Obsidian user might expect. Three kinds are kept apart: things left out on purpose,
> things accepted with their cost recorded, and things still awaiting a ruling. Each item names
> its record so the reasoning can be read, not just the verdict.

## Left out on purpose

From the outline's non-goals for the first release, unchanged since 2026-08-28:

- Rich-text or WYSIWYG editing. Markdown is edited as text; the reading view is a projection.
- Any vault, database, index file, or proprietary format. A workspace is a directory of `.md`.
- Graph views, canvases, databases, spreadsheets, whiteboards.
- A plugin runtime, shell hooks, executable configuration, or shell execution from inside
  the application.
- Neovim plugin compatibility, or Neovim embedded as a subprocess.
- Cloud accounts, synchronisation, collaboration.
- Anything other than Omarchy on Arch Linux under Wayland.
- Replacing Neovim as a code editor.

And from the distribution decision (ADR 0013): no AUR package, no Omarchy Package Repository
submission, no companion plugin, and no prebuilt binaries. The repository is the release.

## Editing: where it is not Vim

The full acceptance record is `docs/vim-acceptance.md`. The deviations a Vim user will meet:

- **`H` and `L` switch buffers** when two or more are open (ADR 0004). With one buffer they
  are Vim's top-of-view and bottom-of-view motions.
- **`Ctrl+V` pastes in every mode; visual block is `Ctrl+Q`.** Omarchy's universal Super+V
  arrives as a plain `Ctrl+V` and cannot be told apart, so paste wins, as it does for Neovim
  in a terminal with bracketed paste. `"+p` and `"+y` remain the clipboard registers.
- **`Ctrl+C` over a selection copies** to the system clipboard. Without a selection it is
  Vim's abort, as expected.
- **A named yank does not also fill the unnamed register.** After `"ayy`, bare `p` pastes
  what it held before. Accepted rather than patch KTextEditor.
- **`:w`, `:e`, `:q` and their relatives are the application's**, not the editor
  component's, so writes go through the atomic writer inside the root and reloads respect
  the tracked revision (ADR 0005, ADR 0006). `:q` is the only way out; the compositor's
  close is a kill, answered by recovery.
- **A read-only note refuses a plain `:w`** with Vim's E45 wording, "'readonly' option is
  set (add ! to override)"; `:w!` writes it and the note keeps its permissions. Fixed on
  Matt's ruling of 2026-09-12 (threat model F-15).

## Files and the workspace

- **Nothing outside the root, ever.** A symlink that leads out of the workspace is refused
  wherever it is met: sidebar, search, open, reload, save, reading-view images. That is the
  design, not a limitation, but it means a workspace cannot borrow a directory from elsewhere
  by symlink.
- **Hidden entries are not listed** in the sidebar and hidden directories are not searched,
  but a hidden `.md` file at the top level is searchable. Symlink handling differs slightly
  by layer. Recorded, not harmonised (F-14).
- **A save is a rename-replace.** The note's owner, group, ACLs, extended attributes, and
  hard links are not preserved; the new file carries the process's defaults (F-14, ADR 0005).
- **Between the last content check and the rename there is an instant** in which another
  writer can win. Linux has no rename-if-unchanged; the window is accepted and recorded
  (ADR 0006).
- **A directory-flush failure after a successful rename is reported as a failed save**
  although the bytes are on disk, and the tracked revision then produces a false conflict on
  the next save (F-11).
- **Notes above 16 MiB are refused** on open, and buffers above 16 MiB are not checkpointed,
  with a status line saying so. The two limits are one on purpose.
- **Line endings are not normalised.** A CRLF file is edited as it comes and written back
  the same way; there is no test pinning that (F-25).
- **The sidebar has no depth or entry cap.** A pathological tree is slow, not refused (F-17).
  The file index behind search and the fuzzy finder stops at 20,000 entries per query and
  says so.
- **Search is literal and case-insensitive**, no regular expressions, at most 4 MiB read per
  file and 64 MiB per query, with previews cut at 2,000 characters (`docs/search.md`).
- **A failure to register a file watch is silent**; the buffer is simply not told about
  external changes until reopened (F-23).
- **Error messages can name absolute paths** and reveal whether a path exists (F-12).

## Sessions and recovery

Full detail in `docs/session-format.md`.

- **Recovery records are plaintext**, owner-only, like Neovim's swap files and for the same
  reason (threat model T-P4). Encryption at rest is not implemented and not claimed.
- **Up to two seconds of typing can be lost** on a power cut: checkpoints run two seconds
  after the last change, on focus loss, and on close.
- **KTextEditor keeps yanked text and macros** in `~/.config/katevirc`, outside the
  application's state directory. Same user, same mode, said out loud (F-5).
- **Work parked in another workspace is never mentioned.** A root announces its own unsaved
  work when it is opened, and nothing else does (ADR 0016). If you leave a buffer dirty in a
  directory and never return, it waits there, unannounced.
- **State for a directory that no longer exists is removed after seven days**, silently
  (ADR 0015). Unsaved text in a deleted workspace has a week.
- **A restored buffer carries no "recovered" mark** beyond being modified (F-15b).
- **A second window on the same workspace gets the structure only** and never adopts the
  first's unsaved text while it is alive (ADR 0012).
- **The snapshot writer does not enforce its own upper bounds**; the reader does (F-26).

## Reading view

- **No remote resources of any kind.** Images must live inside the root and under a size
  budget; everything else in a note is text (ADR 0009).
- **Copying from the reading view puts HTML on the clipboard** with workspace-relative image
  paths, which other applications may not resolve (F-27). Terminals and editors take the
  plain-text flavour and see no difference. Accepted by Matt on 2026-09-12.

## Configuration and appearance

Both files are described in `docs/configuration.md`.

- **Read once at startup.** Changes to `config.toml` and `keymap.json` take effect on the
  next launch. No live reload, no in-app zoom (ADR 0017). The Omarchy theme and the desktop
  font size in `shell.toml` are watched and applied live (block 6.2.3); a `config.toml`
  font size, once set, holds until the next launch.
- **`config.toml` is a flat subset of TOML**: `[section]` headers and `key = value`. Anything
  cleverer needs the reader to grow first, deliberately.
- **Theme files are read without a size cap** on the UI thread (F-16).

## Build and platform

- **No continuous integration.** Every gate is run by hand and its output recorded in the
  pull request; Matt ruled that sufficient for a single-maintainer project (F-9).
- **Dependency advisories are watched, not automated**: Arch's security tracker for the
  named packages, plus Omarchy's update cadence and a re-run of the gates after each update
  (`docs/dependency-review.md`, F-20).
- **Three startup failures exit without a checkpoint** (`qFatal` paths). They are reachable
  only before a window exists, so there is nothing to checkpoint (F-21).
- **`--smoke-test` ships in the release binary**, and Qt strips its own options before the
  launch grammar sees them (F-24). Accepted by Matt on 2026-09-12: the flag makes the
  application quit as soon as its window is shown, and the package's check step uses it to
  prove the exact binary being packaged starts; removing it would blind that check.
- **`cert-*` clang-tidy checks and automated leak checks are not enabled** (F-19 residue).

## Ruled on 2026-09-12

The three findings that still said "decide" when this document was written:

| Finding | Question | Ruling |
| --- | --- | --- |
| F-15 | A read-only note was overwritten by a plain `:w` | Refuse like Vim. Fixed in block 8.4.2 |
| F-24 | The smoke-test hook ships in release builds | Accepted: it validates the packaged binary |
| F-27 | Reading-view copy is HTML with relative image paths | Accepted: plain text is what matters |

Nothing is awaiting a ruling.
