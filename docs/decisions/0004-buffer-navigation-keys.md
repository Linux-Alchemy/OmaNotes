# ADR 0004: Buffer Navigation Keys

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

Phase 4 introduces multiple open buffers, so Omanotes needs a way to move between them. The product
principle is that Vim's navigation language shapes the application, and the plan's Task 4.1 requires
"approved keyboard and mouse buffer switching" without saying what approved means.

Matt's daily Neovim configuration is the reference for that vocabulary. His LazyVim setup binds:

- `<S-h>` to `bprevious` and `<S-l>` to `bnext`
- `<leader>bb` to the alternate buffer and `<leader>bd` to buffer delete

`Shift+H` and `Shift+L` are therefore already muscle memory. They are not free, however: KTextEditor's
Vi mode implements `H` and `L` as the canonical move-to-top-of-view and move-to-bottom-of-view
motions, and a test in this phase confirms `H` works today.

The Phase 2 acceptance matrix lists the mandatory motions as `h j k l`, `w b e`, `0 ^ $`, `gg G`,
`{ }`, and counts. `H` and `L` are not among them, and were not raised during the Phase 2 hands-on
feel gate.

## Decision

Bind `Shift+H` and `Shift+L` to previous and next buffer, in Normal mode only, matching Matt's
LazyVim bindings.

The binding is conditional in two ways:

- It applies only while the editor reports Normal mode, so Insert-mode capitals are never consumed.
- It applies only when two or more buffers are open. With a single buffer there is nothing to switch
  to, so the key falls through to KTextEditor and `H` remains the canonical Vim motion.

A pending application-leader sequence takes precedence, so `Space` followed by `L` remains a leader
sequence rather than a buffer switch.

Mouse users click a tab in the buffer strip. Buffer close bindings are deliberately not part of this
decision; guarded close exists in the registry and its key binding belongs to the Phase 5 command
language, where multi-key sequences such as `<leader>bd` are actually designed.

## Consequences

- Buffer switching matches Matt's existing habit with no new vocabulary to learn.
- With multiple buffers open, `H` and `L` no longer reach their Vim screen motions. `gg`, `G`, and
  counts remain the documented ways to move within a document, and `Ctrl+F`/`Ctrl+B` page normally.
- The override is a genuine Vim deviation and is recorded as one in `docs/vim-acceptance.md`.
- If the lost motions prove more valuable than the familiar switch keys, the binding can move to a
  leader sequence in Phase 5 without changing the registry.

## Rejected alternatives

- **`<leader>n` / `<leader>p`:** preserves `H` and `L` entirely, but invents a vocabulary Matt does
  not currently use and would have to learn alongside his Neovim habit.
- **`:bnext` / `:bprevious` only:** canonical Vim, but KTextEditor's command line is not yet wired to
  application commands, and command-mode-only switching is slower than the daily habit it replaces.
