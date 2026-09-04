# ADR 0007: Application Command Language

- **Status:** Accepted
- **Date:** 2026-09-04

## Context

Phase 5 makes the application keyboard-complete. Before Task 5.1, each action was wired where it
was needed: `Ctrl+S` called the save routine, `Shift+H`/`Shift+L` called the registry, a tab click
activated a buffer, and the leader recognised `?` and `m` only far enough to say they were not
available. Nothing listed what the application could do, so the help overlay (Task 5.3) had
nothing to render, the keymap (Task 5.3) had nothing to rebind, and a second route to an action
could quietly grow a second implementation.

Matt's LazyVim configuration is the reference vocabulary, as ADR 0004 established for buffer
switching. LazyVim binds `<leader>e` to toggle the explorer, `<leader>bd` and `<leader>bD` to buffer
delete and force delete, `<leader>ff` and `<leader><space>` to find files, `<leader>fn` to a new file, and `<leader>/`
to grep. The plan assigns `Leader+?` to help and `Leader+m` to the reading view.

## Decision

One `CommandRegistry` holds every application action as a `CommandDescriptor` with a stable id,
a label, a category, an `enabled` predicate over an `AppContext`, and one `execute`. Every
keyboard shortcut, leader sequence, mouse route, and editor signal runs a command by id through
the registry, which refuses unknown and disabled commands with a message for the status line.

Leader sequences are multi-key, resolved by the registry, and reported at each step
(`Space+b …`). The registry refuses a sequence that is a prefix of another, or shadowed by one,
because such a sequence could never be typed. The vocabulary:

| Sequence | Command | Also reached by |
| --- | --- | --- |
| `Space e` | Show or hide the sidebar; showing it moves focus there | |
| | Focus sidebar, showing it first if hidden | `Ctrl+H` in Normal mode |
| `Space b d` | Close buffer, refusing unsaved work | |
| `Space b D` | Close buffer, discarding changes | |
| `Space b n` / `Space b p` | Next / previous buffer | `Shift+L` / `Shift+H` |
| `Space f n` | New buffer | |
| `Space f f`, `Space Space` | Find files | not available until Task 5.2 |
| `Space /` | Search text | not available until Task 5.2 |
| `Space ?` | Help | not available until Task 5.3 |
| `Space m` | Reading view | not available until Phase 6 |
| | Save | `Ctrl+S`, and `:w` through the Vi command line |
| | Open file, Show buffer, Focus editor | tree activation, tab click, `Ctrl+L` in the tree |

Future features are registered now, disabled, with a hint naming the block that delivers them.
Typing their sequence tells the user what is coming rather than "unknown command".

An audit (`MainWindow::auditCommands`) reports any command with no route and any label that
repeats within a category. A test keeps it empty.

## Consequences

- Task 5.3's help overlay renders `CommandRegistry::available(context)`; its keymap rebinds by
  command id. Neither needs to know how a command is implemented.
- Vi command-line verbs (`:w`, `:w path`, `:e`) remain intercepted as before (ADR 0005, 0006) and
  call the same save and reload routines, but they are not registry commands: they take
  arguments and belong to Vim's grammar, not the application's. This is the one deliberate second
  route, and it is a route into the same function, not a second implementation.
- Closing the last buffer leaves an unnamed buffer, as Vim does. A dirty buffer refuses
  `Space b d` and names the discard route, read from the registry so a rebinding cannot leave the
  message stale.
- `Ctrl+H` outside Normal mode stays a silent no-op rather than a refusal, because in Insert mode
  it is KTextEditor's backspace.
- `CommandDescriptor` carries a `disabledHint` beyond the plan's skeleton, so a refusal can say
  why rather than only that.

- `Space e` toggles rather than focuses, at Matt's gate: the plan never gave sidebar visibility a
  key, though the outline lists it as session state for Phase 7 to restore. `Ctrl+H` remains the
  way to move focus into an open sidebar, matching Neovim window navigation. A second Space is
  spelt `Space` in a sequence, because a bare space is the separator between keys.

- The sidebar starts hidden, at Matt's gate. This overrides the outline's "sidebar visible on the
  left": the text is the point of the application, and `Space e` or `Ctrl+H` brings the tree
  when it is wanted. Phase 7's session restore remembers whichever way it was left, so this is
  the first-launch default only. A launch from a directory with no file therefore opens on an
  empty unnamed buffer with no tree showing; the help overlay (Task 5.3) is what tells a new user
  the tree exists.

## Alternatives rejected

- **Qt `QAction` as the registry.** Actions carry shortcuts, icons, and menu state, and they
  execute through Qt's shortcut system, which is exactly what the leader must bypass. The
  descriptor is smaller and testable without a window.
- **Registering only what exists today.** Unknown-command feedback for `Space f f` would tell
  Matt nothing. A disabled command with a hint is honest and costs one line.
- **Single-key leader sequences only.** LazyVim's mnemonics are two keys, and Matt's hands know
  them.
