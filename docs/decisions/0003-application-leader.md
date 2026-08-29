# ADR 0003: Application Leader

- **Status:** Accepted
- **Date:** 2026-08-29

## Context

Omanotes needs an application-level command prefix that feels familiar to Neovim users without
silently consuming ordinary text or mandatory Vim commands inside KTextEditor. Phase 2 prototyped
Space and `Ctrl+B` in Normal, Insert, and Visual modes against the accepted Vim behaviour suite.

`Ctrl+B` is Vim's canonical page-backward command and passed the Omanotes acceptance suite. Space
also moves right in Vim Normal mode, but `l` and the right-arrow key retain that behaviour and Space
is the conventional leader in many Neovim configurations.

## Decision

Use Space as the default application leader. Recognise it only while the editor is in Normal mode;
Insert, Visual, Replace, and unknown modes pass Space through untouched. Keep `Ctrl+B` available to
KTextEditor for page-backward.

The Phase 2 router recognises `Leader+?` and `Leader+m` only far enough to prove routing and feedback.
It does not implement help or reading-mode commands ahead of their planned phases. Unknown sequences
cancel safely and display feedback in the status area.

## Consequences

- The default follows the intended Neovim-style application vocabulary.
- Normal-mode Space is no longer available as an alias for moving right; `l` and Right remain.
- Ordinary Insert-mode spaces, Visual workflows, mouse input, and `Ctrl+B` page-backward remain intact.
- The default may become configurable only through the validated keymap work planned for Phase 5.

## Rejected alternative

- `Ctrl+B`: rejected because using it as the leader necessarily consumes canonical Vim
  page-backward and violates the Phase 2 editor-feel contract.
