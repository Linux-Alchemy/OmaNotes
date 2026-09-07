# Finding notes and text

From Normal mode or the sidebar, `Space f f` or `Space Space` finds Markdown
filenames. `Space /` searches saved Markdown text. Type to narrow the results;
use Up/Down or Ctrl+J/K, Enter to open, and Escape to cancel. Double-click a
result or click Open for the mouse route. The preview is plain text.

Filename search matches a case-insensitive subsequence, preferring contiguous
matches and filename prefixes. Empty queries list files by relative path.
Text search is literal and case-insensitive, with one result per matching line.
It searches the saved files, so unsaved buffer edits are not indexed. Opening
an already-open buffer preserves its text. A text result moves to its saved
line and column; these positions may differ in an unsaved buffer. Finding an
existing file by name preserves its cursor position.

Search uses the current root. It skips symlinks, hidden directories, unreadable
entries and non-Markdown files. This follows the sidebar's symlink exclusion;
there is no traversal of directory cycles. A result is checked again by the
normal file-opening path before it opens.

Work runs on one background worker, with at most one pending query. Typing a
new query cancels the previous query, and only the latest query can publish
results. Escape cancels without waiting for the worker on the UI thread. There
are no search subprocesses, network requests, saved indexes or dependencies.
The thread is joined during destruction, so shutdown can still wait for an
operating-system filesystem call on a stalled filesystem.

## Search limits

- Queries: 128 characters.
- Enumeration: 20,000 entries per query. Hidden directories are not traversed.
  If a directory exceeds the remaining enumeration budget, its partial listing
  is discarded rather than presenting an arbitrary subset in filesystem order.
- Results: 200, ordered deterministically by score then relative path for file
  search, and relative path then line for text search.
- Content: 4 MiB per file and 64 MiB of reads per query. Binary/NUL-containing
  or invalid UTF-8 files have no preview and are excluded from text matches.
  Filename finding can still list their `.md` names.
- Text lines longer than 4,096 characters are skipped. Previews of file results
  show at most 2,000 characters; text results preview the matching line.

The result count reports when enumeration or result limits are reached.
Content policy exclusions are intentional; an empty result is not proof that
no matching text exists outside these limits.

## Help

`Space ?` and the small `?` button show commands enabled in the current context,
with their leader sequences and direct shortcuts. Select a command and press
Enter or double-click it to execute through the same command registry used by
the keyboard. Escape returns to the previous pane. Reading mode remains
unavailable until Phase 6.
