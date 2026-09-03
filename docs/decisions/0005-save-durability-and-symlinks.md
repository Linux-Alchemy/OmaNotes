# ADR 0005: Save Durability and Symlink Policy

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

Task 4.2 introduces the first code in Omanotes that writes to a user's notes. The plan's contract
requires a same-directory temporary file, a flush "as required by the documented durability level",
and an atomic replace — but no such durability level had been documented, and the plan did not say
what should happen when the target is a symlink.

Both questions change what happens to real files during a power loss or an unusual workspace layout,
so both are recorded here rather than left to the implementation.

## Decision

### Durability

Write to a temporary file in the target's own directory, `fsync` that file, `rename` it over the
target, then `fsync` the containing directory.

This is stricter than Neovim, which defaults to `nofsync`. The stricter setting was chosen
deliberately: the fourth product principle makes plain Markdown files the source of truth, and a
tool whose whole claim is that your notes are safe should not lose a paragraph to a flat battery.
The cost is a few milliseconds per save on the target hardware.

### Symlinks

A symlinked target is written *through*. The link is resolved first, the bytes land on the file it
points at, and the link itself survives as a link.

The file the link points at must also be inside the workspace root. A link that leaves the root is
refused rather than followed.

The alternative — letting `rename` replace the link with a regular file — is what a naive
temp-and-rename does, and it silently changes the shape of a user's notes directory: a note that was
a link into a synced folder quietly stops being one and stops receiving edits. Neovim's default
`backupcopy=auto` preserves links for the same reason.

### File permissions

An existing file keeps its permissions across a save. A newly created file is `0644` masked by the
user's umask, so saving never widens access beyond what the user's environment intends.

## Consequences

- A power loss during a save leaves either the old file or the new one, never a truncated file.
- A symlink pointing outside the workspace becomes effectively read-only in Omanotes. This is a
  deliberate consequence of the root boundary, and it will surprise the first user who hits it.
- Saving does not create directories; `:w notes/idea.md` fails when `notes` does not exist, as it
  does in Vim without `++p`.
- Saving is restricted to `.md` targets, matching the files Omanotes is willing to open. A
  refused target names the `.md` path that would have been accepted, so the rule never costs
  the user a guess.

## Rejected alternatives

- **Rename only, no fsync:** faster and still atomic against a process crash, but a power loss can
  leave a correctly named file whose contents never reached storage.
- **Breaking the symlink:** simpler and never refuses a save, but it silently restructures the
  user's notes directory, which is the worse failure for a tool built on plain files.
