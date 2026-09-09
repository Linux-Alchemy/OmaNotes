# Session snapshot fixtures

The input side of `docs/session-format.md` (Task 7.1). Every file here is fed
to `parseSessionSnapshot` or `readSessionSnapshot` by
`tests/unit/session_snapshot_test.cpp`. The invariants for **every** fixture:

- parsing terminates within the documented limits, whatever the input
- a refused document produces a located diagnostic, never a partial snapshot
- the fixture file is byte-for-byte untouched after the attempt

## Committed fixtures

| File | Contains | Expected |
|---|---|---|
| `valid-full.json` | Every field, three buffers (clean file, dirty file in reading view, dirty scratch), a sidebar selection | Parses; serializes back to identical bytes; `dirtyBufferCount()` is 2 |
| `valid-minimal.json` | Only required fields | Parses; every omission takes its documented default |
| `corrupt-truncated.json` | JSON cut off mid-key | `Malformed`, with the byte offset |
| `corrupt-not-object.json` | A JSON array at top level | `Malformed` |
| `missing-field-window.json` | No `window` object | `InvalidField` at `window` |
| `missing-field-buffer-id.json` | A buffer with no `id` | `InvalidField` at `buffers[0].id` |
| `future-version.json` | `version: 2` plus fields this build does not know | `FutureVersion`; the unknown fields are *not* what is reported |
| `old-version.json` | `version: 0` | `UnsupportedVersion` |
| `unknown-field.json` | A same-version document with an extra top-level key | `InvalidField` at `contents` |
| `escape-parent.json` | A buffer path beginning `../` | `InvalidField` at `buffers[0].path`; the document never yields a path |
| `escape-absolute.json` | An absolute sidebar selection | `InvalidField` at `sidebar.selectedPath` |
| `dirty-without-recovery.json` | `modified: true` and no `recovery` | `InvalidField` at `buffers[0].recovery` |
| `active-not-listed.json` | `activeBuffer` naming an id absent from `buffers` | `InvalidField` at `activeBuffer` |
| `out-of-range.json` | `window.width: 0` | `InvalidField` at `window.width` |

## Generated at test runtime

- **Oversized document** — a syntactically valid document padded past
  256 KiB. Expected: `Oversized`, without the whole file being read.
- **Symlink escape** — a real workspace in a temporary directory with
  `linked.md` pointing outside it. Expected: `resolveSessionPath` refuses it
  as `OutsideRoot`; a missing file comes back `Missing` so the restorer can
  skip it rather than abort.
- **Root mismatch** — a document for one temporary root checked against
  another. Expected: `RootMismatch` (ADR 0010).

`/home/matt/notes` in the committed fixtures is a placeholder root: parsing
only checks it is absolute and normal, and the filesystem is consulted only
by `checkSessionRoot` and `resolveSessionPath` against a live root.
