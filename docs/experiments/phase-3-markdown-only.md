# Phase 3 Experiment: Markdown-only Sidebar

## Hypothesis

Omanotes should behave like a focused note workspace: directories remain navigable, but the
sidebar exposes only `.md` files and explicit non-Markdown file launches are refused clearly.

## Branch boundary

- Branch: `experiment/phase-3-markdown-only`
- Base: accepted Phase 2 tip `c48c605`
- Files shown: directories and case-insensitive `.md` files
- Files opened: valid UTF-8 Markdown loaded into editable, unsaved memory
- Files written: none; persistence remains deferred to Phase 4
- Editor behaviour: provided by KTextEditor Vi mode; Omanotes adds pane and tree navigation only

The model retains the plan's internal `setShowAllFiles` seam, but this branch never enables or
exposes it. The general-purpose sibling will be created independently from the same base rather
than stacked on this branch.

## Automated evidence

- All approved relative and absolute launch forms, `--fresh`, missing paths, unreadable paths,
  traversal, and symlink-root cases pass.
- The lazy tree passes Markdown filtering, hidden-entry, permission-denied, symlink-loop,
  outside-root, non-UTF-8-name, and 500-file-directory fixtures.
- UI tests pass mouse activation, in-memory-only loading, explicit non-Markdown rejection,
  `j/k/h/l/Enter`, and `Ctrl+H`/`Ctrl+L` pane focus.
- The real executable passes empty and populated roots; relative and absolute directories;
  relative, nested, and absolute files; `--fresh`; and safe missing-file rejection.
- The full eight-test suite passes under the development sanitizer preset.
- Formatting, clang-tidy, and executable hardening checks pass.

## Manual comparison

Accepted by Matt on 2026-09-02. The real-session test passed Markdown-only filtering, directory
navigation, mouse opening, `j/k/h/l/Enter`, `Ctrl+H`/`Ctrl+L` pane focus, and memory-only editing.
This accepts the experiment as a valid candidate; the final Phase 3 sidebar policy remains pending
comparison with the general-purpose sibling.

The general-purpose sibling also passed, but Matt selected this Markdown-only branch for continued
development. The alternative remains parked for possible future work; Omanotes will stay focused
rather than drifting into a prettier Neovim.

The Phase 3 checkpoint was accepted on 2026-09-02 after hands-on launches with no argument,
relative and absolute directories, relative and nested-relative files, an absolute file, and
`--fresh`. The missing-file form produced a clear diagnostic and exited with status 2. Sidebar
navigation worked with mouse and keys, roots could not be escaped upward, and closing after opening
`PLAN.md` left the worktree untouched.

During an instrumented real-GUI shutdown, LeakSanitizer reported retained allocations entirely in
Qt's GTK/Pango/fontconfig theme stack. Manual GUI launches therefore used
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, matching the automated GUI-test environment while
retaining AddressSanitizer's memory-error checks.
