# ADR 0011: Session State Layout and Workspace Identity

- **Status:** Accepted; implemented by Task 7.2
- **Date:** 2026-09-09

## Context

Task 7.2 stores one session per workspace outside the workspace. Two things
had to be fixed before the first file was written, because changing either
later orphans every existing session: where the files live, and how a
workspace's directory is named. Block 7.2.1 asks that the naming not expose
raw path names unnecessarily.

## Decision

**Location.** `$XDG_STATE_HOME/omanotes/sessions/<workspace-id>/`, with
`session.json` for the structural snapshot and, from Task 7.3, `recovery/`
for dirty-buffer records. State is never written inside a workspace, under
`$XDG_CONFIG_HOME`, or anywhere shared between users.

**Identity.** `<workspace-id>` is the first 128 bits of SHA-256 over the
bytes of the canonical root path, rendered as 32 lowercase hex characters.
The snapshot inside carries the full root path and is checked against the
launch root before use, so the id only has to be stable and unambiguous in
practice, not cryptographically unique.

**Permissions.** Every directory the store creates is `0700`; the session
file is `0600`; both are enforced on every save, not only on creation.
Symlinks inside the store are refused, never followed.

**Replacement.** Through `replaceFileAtomically`, the core of the Phase 4
note writer, so notes and state share one durability policy (ADR 0005).

## Consequences

- A directory listing of `sessions/` reveals how many workspaces have been
  opened and nothing else; the root path is visible only inside each file,
  which is owner-only.
- Renaming or moving a workspace directory gives it a new id and therefore
  an empty session; the old one is left in place until retention (7.3)
  cleans it. Following renames is deliberately out of scope.
- The derivation is pinned by a test. Changing it is a format change and
  needs a migration, not a tweak.
- The note writer gained a mode parameter and a test-only fault seam so the
  store's failure paths could be proven rather than trusted. Its
  workspace-facing behaviour is unchanged and still covered by its own suite.

## Rejected alternatives

- **Plain root path as the directory name, escaped:** Simplest to debug, but
  the state directory then lists every workspace in clear text. Rejected per
  block 7.2.1.
- **A random id per workspace with an index file mapping paths to ids:**
  Avoids hashing but introduces a second file whose corruption loses every
  session at once, and the index itself spells out the paths. Rejected.
- **A single global session file:** Rules out per-root scoping (ADR 0010)
  and makes concurrent instances in different workspaces fight over one
  file. Rejected.
- **Full 256-bit hex ids:** Harmless but 64-character directory names for no
  gain over 128 bits when the snapshot verifies the root anyway. Rejected
  for tidiness.
