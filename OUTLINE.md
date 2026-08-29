# Omanotes — Project Outline

> **Status:** Approved on 2026-08-28
>
> **Name:** `Omanotes` is a working title; availability has not been checked.

## One-sentence brief

Omanotes is an Omarchy-first, native Markdown writing application that combines a restrained graphical workspace with genuine Vim-style editing and complete keyboard navigation.

## The problem

Omawrite offers an appealingly simple place to write, but deliberately omits the workspace navigation and editing depth needed for a larger collection of notes. Obsidian makes Markdown collections easy to organise, but its Vim behaviour is concentrated in the editor and does not govern the whole application. Neovim provides the desired editing language and speed, but it is a code editor rather than a dedicated graphical writing space.

Omanotes should occupy the useful ground between them without attempting to become a word processor, an IDE, or a sprawling knowledge-management platform.

## Primary user

The first user is Matt: an Omarchy user who writes in Markdown, works extensively from the keyboard, uses Neovim and Herdr, and wants to understand and direct the system design without personally implementing the C++ code.

The eventual audience is the wider Omarchy community, particularly people attracted to simple tools, plain files, Vim navigation, and keyboard-first workflows. Public distribution is intended once the application is useful and dependable enough for its creator to use daily.

## Product principles

1. **Omarchy first.** Design, package, theme, and validate for current Omarchy Quattro on Arch Linux and Wayland before considering other platforms.
2. **Vim is foundational.** Modal editing is not an optional keymap bolted onto one pane. Vim's navigation language should shape the editor, while a consistent command vocabulary governs the surrounding application.
3. **Keyboard complete, mouse friendly.** Every core workflow must be possible from the keyboard. Visible controls should still respond naturally to mouse input.
4. **Plain Markdown is the source of truth.** Notes remain ordinary `.md` files in ordinary directories. No proprietary database is required to read, edit, move, back up, or inspect them.
5. **Simple means composable, not barren.** The interface should use a small set of predictable behaviours rather than accumulating toolbars, panels, modes, and overlapping ways to do the same thing.
6. **Agents are first-class collaborators.** External edits by agents and other tools must be detected safely. The workspace should remain understandable through files, with structured command-line operations added only where they materially improve safe automation.
7. **Reliability before cleverness.** File boundaries, recovery, validation, security, and tests are designed into each phase rather than postponed until the application is feature-rich.
8. **The user remains the orchestrator.** No phase advances until its automated checkpoint passes and Matt has run, inspected, and accepted the resulting application.

## Core experience

### Launch contract

| Invocation | Behaviour |
| --- | --- |
| `omanotes` | Use the current directory as the root and restore its last session; open a scratch buffer when none exists. |
| `omanotes ~/Notes` | Use `~/Notes` as the root and restore its last session; open a scratch buffer when none exists. |
| `omanotes idea.md` | Keep the current directory as the root, restore its session, and open or focus the relative file. |
| `omanotes projects/idea.md` | Keep the current directory as the root, restore its session, and open or focus the nested relative file. |
| `omanotes ~/Notes/projects/idea.md` | Use the absolute file's parent as the root, restore its session, and open or focus the file. |

The shell establishes context; Omanotes respects it. A workspace is a directory, not a proprietary vault.

### Default window

The application opens with:

- A Markdown-focused filesystem sidebar visible on the left.
- A blank, unnamed scratch buffer in the main editing pane unless a file was supplied.
- A restrained buffer/tab strip for open documents.
- A small status area that communicates editor mode, file state, and contextual commands without becoming decorative furniture.

The scratch buffer is not written into the workspace until explicitly named and saved, but abnormal-exit recovery must prevent unsaved writing from disappearing.

### Session continuity

Omanotes borrows Herdr's session-snapshot idea: closing the application records its structural state so reopening returns the user to the same working context rather than merely the same directory.

The restorable session includes:

- Workspace root and window geometry.
- Open buffers, their order, and the active buffer.
- Cursor, selection, and scroll position for each buffer.
- Sidebar visibility, width, and expanded directories.
- The active writing or reading view.
- Enough dirty-buffer recovery data to restore unsaved work without modifying workspace files.

Session shape and unsaved document contents are stored separately under the user's XDG state directory. Snapshots are versioned, written atomically, private to the user, and treated as untrusted input when restored. Missing files, corrupt state, incompatible future versions, and paths that no longer belong to the workspace must fail safely without preventing a clean launch.

An explicit fresh-start option bypasses restoration without silently deleting the saved session. Clean shutdown creates a final snapshot; guarded runtime checkpoints protect against crashes.

### Navigation

- The sidebar supports Vim-like list movement, directory expansion, and file opening.
- Fuzzy file finding and workspace text search provide direct navigation without requiring the sidebar.
- Open buffers can be cycled from the keyboard.
- Pane focus, dialogs, search results, tabs, and command menus are keyboard navigable.
- A contextual help overlay, inspired by Herdr, explains commands available in the current location.
- Application commands use a configurable leader or prefix. The default remains an explicit design decision because `Ctrl+B` conflicts with Vim's canonical page-backward command.

### Writing and reading

- Writing mode displays honest Markdown source with restrained syntax styling.
- Reading mode renders a polished document without changing the underlying source.
- The two modes toggle through an application command; `Leader+m` is the provisional binding.
- A permanently split preview is not part of the initial product.
- WYSIWYG editing is not a goal.

### Files and agents

- File operations are constrained to the active root unless the user explicitly chooses another location.
- Saves are atomic where the platform permits.
- Clean buffers reload safely after external changes.
- Modified buffers never silently lose local work when the disk version changes.
- Recoverable state is clearly separated from user-authored Markdown.
- Search indexes and other derived state can always be rebuilt.

## Technical direction

The selected implementation direction is:

- **Language:** modern C++ (target standard to be fixed in the build plan)
- **GUI:** Qt 6, favouring a restrained native widget interface
- **Editor:** KDE Frameworks 6 KTextEditor with Vi input mode
- **Markdown reading:** Qt's Markdown-capable document rendering unless an early validation spike demonstrates a material limitation
- **Build system:** CMake
- **Primary runtime:** Omarchy Quattro on Arch Linux under Wayland

KTextEditor is selected to minimise custom editor engineering while providing a mature modal editing foundation. Omanotes will configure and frame the component; it will not attempt to reproduce a text editor engine.

## Quality and security requirements

Every build phase must include checks proportionate to the code introduced. Across the project, the required baseline includes:

- Compiler warnings treated seriously, with a documented warning policy.
- Automated formatting and static analysis.
- Unit tests for pure logic and integration tests for file behaviour.
- Runtime checks appropriate to C++, including sanitised development builds.
- Strict workspace-root and path validation, including deliberate symlink handling.
- Atomic save and recovery tests, including simulated failures.
- No arbitrary shell-command execution in the initial configuration system.
- Safe handling of untrusted Markdown, links, and local resources in reading mode.
- Dependency additions and upgrades require explicit approval.
- No skipped failing test may be treated as a passing checkpoint.

## Delivery model

The application will be built as a sequence of small, runnable vertical slices. Phase 1 must already launch a real native window showing the basic sidebar, editor area, buffer strip, and status area, even though its content and actions are initially skeletal.

Each phase ends with two hard gates:

1. **Engineering gate:** build, tests, static/security checks, and phase-specific validation all pass with recorded evidence.
2. **Orchestrator gate:** Matt installs or runs the current application, explores the new behaviour, asks questions, and explicitly approves continuation.

Agents execute only the approved phase or addressed plan blocks. They do not begin future phases, add speculative architecture, or quietly broaden scope while waiting for feedback.

## Initial scope

The first usable milestone should provide:

- Native Omarchy launch and window behaviour.
- Directory-rooted workspace navigation.
- Scratch buffers and multiple open Markdown buffers.
- KTextEditor-powered Vi editing.
- Configurable application-level commands and complete keyboard navigation.
- Fuzzy file finding and workspace text search.
- Markdown writing and reading modes.
- Safe save, external-change handling, crash recovery, and session behaviour.
- Herdr-inspired, versioned session snapshot and restore with separate dirty-buffer recovery.
- Theme integration sufficient to feel at home on Omarchy.
- A small documented CLI surface for predictable launch context and, where justified, agent interaction.
- Arch packaging suitable for evaluation on Omarchy, eventual AUR/Omarchy Package Repository submission, and an optional separately packaged QML companion plugin when it provides meaningful shell integration.

## Explicit non-goals for the initial release

- Rich-text or WYSIWYG editing.
- A proprietary vault or database format.
- Obsidian-style graph visualisation.
- Canvas, database, spreadsheet, or whiteboard features.
- A general plugin runtime.
- Arbitrary shell hooks or executable configuration.
- Neovim plugin compatibility.
- Embedding Neovim as a subprocess.
- Cloud accounts, synchronisation, or collaboration services.
- Mobile, Windows, or macOS support.
- Replacing Neovim as a code editor.

## Decisions still requiring validation

These are expected design checkpoints rather than permission to expand scope:

1. Confirm `Omanotes` as the final public name after availability checks.
2. Select the default application leader after testing KTextEditor's canonical Vim commands; Space is the current recommendation.
3. Define the minimum supported KTextEditor, Qt, and Omarchy versions from the target packaging environment.
4. Confirm whether KTextEditor's Vi behaviour passes the agreed motion, operator, visual-mode, register, search, repeat, and mapping acceptance suite.
5. Confirm that Qt's Markdown renderer meets the desired reading-view appearance and safety requirements.
6. Define any useful QML companion-plugin scope before proposing a separate repository for `omarchyplugins.com`; the native application itself ships as an Arch package.
7. Select the public licence before the first public release.

## Success definition

Omanotes succeeds when Matt chooses it for everyday Markdown writing because it opens quickly, looks at home on Omarchy, navigates entirely from the keyboard, feels recognisably Vim-like, remains pleasant with a mouse, keeps ordinary files safe, and stays simpler than the tools it deliberately does not replace.
