# ADR 0012: Concurrent instances and recovery ownership

- **Status:** Proposed (2026-09-11), for Matt's gate with the 8.1.3 high-severity PR
- **Resolves:** threat-model finding F-7 (T-P6)
- **Related:** ADR 0010 (session state is root-scoped), ADR 0011 (layout and workspace id),
  `docs/session-format.md`

## Context

Nothing stopped two OmaNotes windows from opening the same workspace, and the plan never
asked for that to be refused: `omanotes idea.md` from a second terminal is part of the
launch contract. The snapshot already handled it, last close wins, documented and tested.
Recovery records did not. A second instance computed "records nothing references" against
its own memory, which at start is empty, so it treated the first instance's live records as
orphans, restored them as its own dirty buffers, and from then on checkpointed into the same
record files. Two writers, one file, and whichever wrote last destroyed the other's unsaved
text with no message. The same happened through the snapshot path: restoring a buffer that
named a record adopted that record's id.

## Decision

One advisory lock per workspace, `flock(2)` on `<sessions>/<workspace-id>/instance.lock`,
taken at start and held for the life of the process (`src/session/instance_lock.cpp`).

- **The holder owns recovery for that root.** It restores referenced records, adopts
  orphans, and checkpoints into the ids it restored, exactly as before.
- **Everyone else leaves records alone.** An instance that finds the lock held restores the
  desk's structure only: a buffer the snapshot marks dirty comes back clean from disk, or not
  at all when it was a scratch, and the status line says how many are "held by another
  OmaNotes". It never reads, claims, or removes another instance's record. Its own dirty
  buffers get their own new records, so nothing it does can collide.
- **A lock that cannot be taken for any other reason is treated the same as contention.**
  When in doubt, adopt nothing; the records wait for a launch that can.
- **The lock file is never deleted.** The kernel drops the lock when the descriptor closes,
  on a clean exit or a crash alike, so a dead instance never holds it. An empty lock file is
  harmless; deleting one is a race.

The snapshot stays last-close-wins. The next launch after both instances are gone holds the
lock and brings back every record: the ones the final snapshot references, and the other
instance's as orphans. Nothing is lost; at worst a buffer comes back as an orphan rather
than in its tab order.

## Alternatives rejected

- **Stamp ownership into each record** (pid, start time, boot id) and treat a record as
  live while its owner runs. Needs a record-format change and a liveness check that is
  either racy (pid reuse) or platform-specific. The lock gets the same answer from the
  kernel without touching the format.
- **Refuse the second launch.** Hostile to the launch contract and to the obvious workflow
  of opening a note from a second terminal. It also does nothing for the case where the
  first instance is hung.
- **Hand the second launch to the first instance** (single-instance activation over D-Bus
  or a local socket). The right long-term shape for "open this file in my running window",
  but it is a feature, Phase 8 forbids features, and it still needs this lock underneath for
  the moment two processes race to become the instance.

## Consequences

- `SessionStore` gains `lockFileFor` and a public `ensureDirectoryFor`, because the lock
  must exist before the first checkpoint would have created the directory.
- `SessionRestorer::restore` gains an `adoptRecords` flag and `RestoreReport` a
  `heldElsewhere` count.
- A second window sees clean notes where the first has unsaved edits. That is honest: the
  edits are not on disk. Saving from the second window against a note the first has dirty
  is then an ordinary conflict on the first's next save, handled by ADR 0006.
- Tests: `secondInstanceLeavesTheFirstsRecordsAlone` and `theLockFollowsTheLiveInstance` in
  `tests/integration/session_restore_test.cpp`. The in-process test relies on `flock`
  locks being per open file description, so two opens in one process contend as two
  processes would.
