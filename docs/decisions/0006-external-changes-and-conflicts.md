# ADR 0006: External Changes and Conflicts

- **Status:** Accepted
- **Date:** 2026-09-04

## Context

Task 4.3 makes Omanotes safe to use alongside other writers of the same files: a sync client, a
script, an agent, or Neovim in the next window. Until now a save overwrote whatever was on disk,
and a file edited elsewhere stayed stale in the editor until relaunch.

The plan's contract is: reload clean buffers, preserve dirty buffers, and require an explicit
decision before replacing either version. It leaves three things open: what "explicit decision"
looks like in a keyboard-driven app with no command language yet, how a change is recognised,
and what happens when a file disappears.

## Decision

### Behaviour follows Matt's Neovim configuration

Matt's `init.lua` sets `autoread` with a `checktime` autocmd, and maps `<leader>wr` to confirm
before discarding. The same shape applies here:

| Situation | Omanotes |
| --- | --- |
| File changes on disk, buffer unmodified | Reloaded silently, cursor kept, one status line: `Reloaded note.md from disk` |
| File changes on disk, buffer has edits | Nothing replaced. Status says so and stays marked `[changed on disk]` until resolved |
| Plain `:w` / `Ctrl+S` on a buffer whose file changed | Refused with directions. `:w!` overwrites deliberately; `:e!` discards the edits and takes the disk |
| `:e` on a modified buffer | `No write since last change (add ! to override)`, exactly as Vim |
| `:w other.md` where `other.md` exists | `File exists (add ! to override)`, exactly as Vim's E13 |
| File deleted on disk | Buffer kept and marked `[deleted on disk]`; the next `:w` writes it again |

`:w!`, `:e`, `:e!`, and `:e <path>` are intercepted before KTextEditor's Vi mode sees them, for
the same reason `:w` already is (ADR 0005): the editor's own implementations know nothing about
the workspace root, the atomic writer, or the revision the application is tracking.

### The save is the guarantee; the watcher is a courtesy

Every save compares the disk against the content the buffer last loaded or wrote, twice: once
when the save is decided, and once more inside the atomic writer after the new bytes are durable
and immediately before the rename (`WritePrecondition`, added 2026-09-11 for threat-model finding
F-2). A change that lands between those two points is refused with "changed on disk while the
save was in progress" and nothing is replaced. What remains open is the instant between the
second comparison and the rename itself; Linux offers no rename-if-unchanged primitive, so that
window is accepted and recorded rather than claimed away. Neither check depends on the file
watcher having fired. The watcher exists so that changes are noticed promptly and clean buffers
refresh on their own, but a watcher can lag or be starved, and the safety property must not rest
on it.

### Identity is a content hash, not a timestamp

The plan said hashes should "supplement timestamps where ambiguity matters". In practice every
case that matters is an ambiguous one: `touch`, a sync client rewriting identical bytes, a
rename-replace save that preserves the modification time, two writes inside one clock tick. A
note is small enough that a SHA-256 of it costs microseconds, so the hash is the only identity
used. Timestamps are not consulted at all. This keeps the classifier a pure function of two
hashes and a modified flag, which is what makes it testable.

A file that exists but cannot be read is classified as a conflict, never as unchanged or
removed: the buffer is kept and the user is told.

### Watch the directory as well as the file

Neovim's default `backupcopy=auto` saves by writing a sibling and renaming it over the original.
That swaps the inode out from under a plain file watch, which Qt then silently drops. Omanotes
watches the containing directory too, re-arms the file watch whenever the file reappears, and
coalesces every event within a quiet period into one report. Twenty agent writes in a second
become one reload carrying the final content.

## Consequences

- No conflict path overwrites editor or disk content without a `!`. This is the property Task
  4.3.4 verifies, and it is covered by tests that change the disk both before and after the
  watcher can react.
- The `[changed on disk]` and `[deleted on disk]` markers live in the status line, which
  describes the active buffer. A conflict on a background buffer is announced once when it
  happens and shows its marker when that buffer becomes active. Putting the marker on the tab
  itself is left to the buffer strip's later polish.
- `:wq`, `:x`, and the other combined verbs remain refused until the Phase 5 command language.
- The `ExternalChangeAction` enumeration gained an `Unchanged` value beyond the plan's
  skeleton. Without it the classifier could not express "our own save just landed", which is
  the most common event the watcher reports.
