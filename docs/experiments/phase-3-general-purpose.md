# Phase 3 Experiment: General-purpose Sidebar

## Hypothesis

Omanotes can remain a restrained, keyboard-first editor while exposing all root-contained files
and delegating language-aware editing to KTextEditor and KSyntaxHighlighting.

## Branch boundary

- Branch: `experiment/phase-3-general-purpose`
- Base: accepted Phase 2 tip `c48c605`
- Files shown: non-symlink directories and regular files, including hidden entries
- Files opened: valid UTF-8 text loaded into editable, unsaved memory
- Files refused: binary-looking or invalid UTF-8 content, with visible feedback
- Files written: none; persistence remains deferred to Phase 4
- Language support: filename-based definitions supplied by KSyntaxHighlighting, with plain-text
  fallback when no definition exists

Omanotes does not implement language servers, completion engines, syntax definitions, plugins, or
new modal editing behaviour in this experiment. KTextEditor remains the capability boundary.

## Automated evidence

- All seven sanitizer-backed tests pass independently on this branch.
- Sidebar tests show Markdown, source, extensionless, and hidden entries while preserving lazy,
  cycle-safe, root-bounded enumeration.
- KTextEditor selects KSyntaxHighlighting definitions from filenames (including Markdown and
  Python) and falls back to plain text for unknown extensions.
- UI tests pass mouse and keyboard loading of non-Markdown text, memory-only modification, and
  visible refusal of binary-looking content.
- Formatting, clang-tidy, and executable hardening checks pass.

## Manual comparison

Pending Matt's real-session review against the accepted Markdown-only experiment.
