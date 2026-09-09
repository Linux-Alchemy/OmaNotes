# Session state format

Omanotes remembers each workspace's desk so that reopening it feels like
returning to the same chair: the window, the sidebar, which notes were open,
where the cursor was, and which of them held unsaved work. This document
specifies the structural snapshot introduced by Task 7.1 and the rules every
reader and writer of it must follow.

The ideas are the proven ones from Herdr — a version field, a structural
snapshot kept apart from content, compatible restore, round-trip fixtures —
implemented independently in C++ with no code copied.

## What is stored, and where

| Kind | Location | Contains |
|---|---|---|
| Structural snapshot | `$XDG_STATE_HOME/omanotes/sessions/<workspace-id>/session.json` | Layout and references. **Never note text.** |
| Recovery records | `$XDG_STATE_HOME/omanotes/sessions/<workspace-id>/recovery/<uuid>.json` | The unsaved text of one dirty buffer (Task 7.3) |

`$XDG_STATE_HOME` defaults to `~/.local/state`. Omanotes never stores session
state inside the workspace, in `$XDG_CONFIG_HOME`, or anywhere shared.

`<workspace-id>` is the first 128 bits of SHA-256 over the canonical root
path, as 32 lowercase hex characters (ADR 0011). It is deterministic, needs no
index, and keeps the state directory listing from spelling out where every
workspace lives. The snapshot itself still carries the root path, so a
collision, however unlikely, is caught by `checkSessionRoot` rather than
restoring the wrong desk.

### Permissions and replacement (Task 7.2)

`omanotes/`, `sessions/` and each `<workspace-id>/` are created `0700` and
tightened back to `0700` if found wider; `session.json` is `0600` from the
moment it exists, regardless of the umask. Replacement is the same
temporary-file-and-rename used for notes (ADR 0005): temporary in the same
directory, flushed, renamed over the file, directory flushed. A failure at any
step leaves the previous file byte-for-byte intact and removes the temporary.

Two instances saving the same workspace at once each write their own
temporary and rename; the last rename wins and a reader only ever sees a
complete document. A temporary older than fifteen minutes is presumed
abandoned by a crashed writer and removed before the next save; younger ones
may belong to a live instance and are left alone.

Nothing in the store is ever followed through a symlink. A symlinked
directory or `session.json` is refused, on read and on write, with a
diagnostic.

### Privacy boundary

The structural snapshot may reveal: the workspace's absolute path, the
relative paths of notes that were open or selected, cursor and scroll
positions, and how many buffers had unsaved changes. It may never contain a
byte of note content, a search term, clipboard text, or anything from
outside the workspace. Unsaved text lives only in recovery records, which are
separately protected and separately documented.

### Scope (ADR 0010)

A snapshot belongs to exactly one workspace root and is restored only there.
A restorer must call `checkSessionRoot` before using any of it. The document
is deliberately self-describing enough — root path, buffer count, dirty count
— that the parked-work notice for *other* roots can be produced from
snapshots alone, without opening a single recovery record.

## Recovery records (Task 7.3)

A recovery record is the unsaved text of one dirty buffer, kept so a crash or
forced kill loses nothing. It is the only place note content ever leaves the
workspace, so it is treated with more care than the snapshot.

### What a record holds

`recovery/<uuid>.json`, owner-only, one JSON object:

| Field | Type | Required | Rules |
|---|---|---|---|
| `version` | integer | yes | Same policy as the snapshot; currently 1 |
| `path` | string | no (none) | The buffer's note, relative to the root, same rules as snapshot paths; absent for a scratch buffer |
| `contents` | string | yes | The full buffer text, at most 16 MiB of UTF-8 |
| `baseDigest` | string | no (none) | Hex SHA-256 of the note's on-disk bytes when the checkpoint was taken; absent when the note did not exist |

`baseDigest` is what lets a restore tell whether the note changed underneath
the crashed session, using the same content-hash machinery as external change
detection (ADR 0006). A buffer larger than the cap is not checkpointed; the
caller is told, on the status line, so the user knows that buffer is
unprotected until saved.

### Identity and lookup

The `<uuid>` is chosen when a buffer first becomes dirty and reused for every
later checkpoint of that buffer, so the snapshot only changes when the desk
changes. Lookup is **by id inside the current workspace's own `recovery/`
directory and nowhere else**: an id is validated as a UUID before it becomes a
file name, so a record can never be addressed by path, and a snapshot can never
point a restore at another workspace's text (ADR 0010). Symlinks in the
directory are refused, never followed.

### Checkpoint cadence

Wired in Task 7.4. A dirty buffer is checkpointed two seconds after its last
change, when the window loses focus, and on a clean close. A buffer that
returns to clean (saved, or edited back to its saved text) has its record
removed at that moment. The cadence is a ceiling on loss, not a guarantee of
zero loss: the last two seconds before a power cut are not promised.

### Retention and cleanup

- **Save succeeds:** the record is removed immediately. A saved note needs no
  second copy.
- **Buffer discarded:** closing with `:q!`, `Space b D`, or declining to save
  removes the record. Discarding is a decision; the record honours it.
- **Restore:** a record is removed only once the restored buffer is saved or
  discarded, never merely because it was loaded. A crash during restore must
  not eat the only copy.
- **Orphans:** after a successful restore, any record in this workspace's
  directory that the snapshot no longer references is removed. An orphan is
  a record whose buffer was already resolved; it holds nothing the user
  still has.
- **Other workspaces:** never touched. A workspace that has moved or been
  deleted keeps its records until the user removes
  `~/.local/state/omanotes/sessions/<id>/` by hand. Deleting unsaved work
  because a directory went missing is not a decision this program makes.

### Restoring a record

A restored buffer comes back **dirty**, with the recovered text, never
silently written to disk. Where it can be saved depends on what the disk
looks like now, decided by `planRecovery`:

| Situation | Restored as | Saving |
|---|---|---|
| Scratch (no `path`) | `[No Name]`, modified | Needs an explicit name, as any scratch buffer does |
| Note unchanged since the checkpoint | The note, modified | `:w` writes as usual |
| Note changed on disk since the checkpoint | The note, modified, in conflict | `:w` refuses; `:w!` overwrites, `:e!` discards, exactly as ADR 0006 |
| Note gone from disk | The note, modified | `:w` recreates it |
| Path now escapes the root | Not restored; reported | — |

### Sensitive content

Recovery records are plaintext, like Neovim's swap files and for the same
reason: they exist to survive a crash, and a key that must be available to
recover after a crash offers little against anyone who can already read the
user's files. They are owner-only, short-lived by the retention rules above,
and never contain anything the user did not type into a buffer. Anyone for
whom plaintext at rest is unacceptable should treat the workspace itself the
same way, since the saved notes carry the same exposure. Encryption at rest is
not implemented and not claimed.

## The document

Format version **1**. UTF-8 JSON, one object, written indented with keys in
sorted order and a trailing newline. Serialization is deterministic: the same
state always produces the same bytes.

```json
{
    "activeBuffer": "0d5b7d8e-1f6a-4c3b-9e2d-8a7f6b5c4d3e",
    "buffers": [
        {
            "cursor": { "column": 4, "line": 12 },
            "id": "0d5b7d8e-1f6a-4c3b-9e2d-8a7f6b5c4d3e",
            "modified": false,
            "path": "journal/2026-09-09.md",
            "scrollLine": 8,
            "viewMode": "writing"
        },
        {
            "cursor": { "column": 3, "line": 1 },
            "id": "9e8d7c6b-5a4f-4e3d-9c2b-1a0f9e8d7c6b",
            "modified": true,
            "recovery": "c0ffee00-1234-4abc-8def-000000000002",
            "scrollLine": 0,
            "viewMode": "writing"
        }
    ],
    "sidebar": { "selectedPath": "journal/2026-09-09.md", "visible": true, "width": 240 },
    "version": 1,
    "window": { "height": 720, "maximized": false, "width": 1100 },
    "workspaceRoot": "/home/matt/notes"
}
```

### Fields

Required fields must be present. Optional fields take the stated default when
absent; the writer always emits them anyway.

| Field | Type | Required | Rules |
|---|---|---|---|
| `version` | integer | yes | See version policy |
| `workspaceRoot` | string | yes | Absolute path, lexically normal, 1–4096 bytes, no NUL |
| `window.width`, `window.height` | integer | yes | 1–32767 |
| `window.maximized` | boolean | no (`false`) | |
| `sidebar.visible` | boolean | yes | |
| `sidebar.width` | integer | no (`0`) | 0–32767; `0` means "use the default width" |
| `sidebar.selectedPath` | string | no (none) | Relative path rules below |
| `buffers` | array | yes | 0–256 entries |
| `buffers[].id` | string | yes | UUID; unique within the document |
| `buffers[].path` | string | no (none) | Relative path rules below; absent for a scratch buffer |
| `buffers[].viewMode` | string | no (`"writing"`) | `"writing"` or `"reading"` |
| `buffers[].cursor.line`, `.column` | integer | no (`0`, `0`) | 0–100 000 000 |
| `buffers[].scrollLine` | integer | no (`0`) | 0–100 000 000 |
| `buffers[].modified` | boolean | no (`false`) | |
| `buffers[].recovery` | string | iff `modified` | UUID; unique within the document |
| `activeBuffer` | string | no (none) | Must be one of `buffers[].id` |

**Relative path rules.** A stored path is relative to `workspaceRoot`, uses
`/` separators, is 1–4096 bytes, contains no NUL, is lexically normal, and
contains no `.` or `..` components. Anything else is refused at parse time, so
a document cannot even *name* a location outside its root. Whether the file
still exists, or is a symlink pointing out, is decided later against the live
root.

**Consistency rules.** A buffer is `modified` exactly when it has a
`recovery` record: a dirty buffer without one would silently lose text, and a
clean buffer with one would restore text the user discarded. Unknown fields at
any level are refused; a same-version document with keys this program never
wrote was not written by this program.

### Limits

| Limit | Value | Why |
|---|---|---|
| Document size | 256 KiB | Bounds parse time and memory on a corrupt or hostile file. The reader stops one byte past this. |
| Buffers | 256 | Well above any real desk; bounds restore work |
| Path length | 4096 bytes | `PATH_MAX` |
| Dimensions | 32767 | Qt's widget coordinate ceiling |
| Positions | 100 000 000 | Generous for any note; rejects garbage |

The writer must respect the same limits so that whatever it writes can be
read back.

## Version policy

- `version` is checked **first**, before any other field. A document from a
  newer Omanotes may contain fields this build has never heard of; it is
  reported as *newer*, not as *unknown field*.
- `version > 1` (newer than this build): refused as `FutureVersion`. The file
  is left alone for the newer build that wrote it.
- `version < 1`: refused as `UnsupportedVersion`. Version 1 is the oldest
  format there is; this branch exists so that when the floor rises, the
  refusal is already in place and tested.
- Compatible restore across versions works by **explicit defaults, not
  guesses**: when a later version adds a field, it adds it to the optional
  table above with a default, and older documents parse unchanged. A change
  that cannot be defaulted raises `version`. Renaming or retyping an existing
  field always raises `version`.

## What a refused document does

Every refusal is a `SessionError` with a code, a location (`buffers[2].path`,
`window.width`, `byte 94`) and a plain message. The reader:

- **never writes**: it does not delete, truncate, rename, or "repair" the bad
  file, so it stays available for inspection (`tests/unit/session_snapshot_test.cpp`
  hashes the file before and after to prove it);
- reports the diagnostic so the application can start clean and say so on the
  status line — a silent fallback would hide a corrupted state directory;
- leaves recovery records untouched. Structural state being unreadable is not
  permission to discard unsaved text.

Wiring the diagnostic to the status line and the clean launch is Task 7.4's;
7.1 supplies the error and the guarantee.

## Restoring paths

`resolveSessionPath(root, relative)` resolves through `WorkspaceRoot::resolveFile`,
the same policy that guards the sidebar and saves, not a second
implementation. Outcomes a restorer must handle:

| Result | Meaning | Restorer's response |
|---|---|---|
| a canonical path | Inside the root, a readable regular file | Open it |
| `Missing` | File no longer exists | Skip it, report it, keep going |
| `OutsideRoot` | A symlink or mount now points out of the root | Refuse; never open |
| `NotRegularFile`, `Unreadable` | Directory, device, or no permission | Skip it, report it |

One missing file never prevents the rest of the session from opening.

## Fixtures

`tests/fixtures/session/` holds one document per rule above, described in its
README; `tests/unit/session_snapshot_test.cpp` runs them all, plus the
generated cases (oversized file, symlink escape, root mismatch) that do not
belong in git.
