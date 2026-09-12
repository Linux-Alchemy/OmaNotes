# ADR 0015: State for a vanished root expires after seven days

- **Status:** Accepted (2026-09-12, Matt's ruling during the Phase 8 daily-use trial). The
  launch-notice clause below is superseded by ADR 0016 the same day; the sweep runs silently.
- **Resolves:** threat-model finding F-22 (recovery records accumulate without bound for dead
  workspaces); the retention question ADR 0010 deferred
- **Supersedes:** the "kept until the user removes the directory by hand" clause of
  `docs/session-format.md`, "Retention and cleanup"
- **Related:** ADR 0010 (session state is root-scoped), ADR 0012 (instance lock)

## Context

ADR 0010 parks unsaved work with the root it was written in and announces it from every other
root: "Unsaved work waiting in ~/notes (2 buffers). Open it to recover." It deferred what
happens to state for a root that no longer exists, and `docs/session-format.md` answered with
"kept until the user removes the directory by hand", on the grounds that deleting unsaved work
because a directory went missing is not a decision this program makes.

Daily use found the gap. A gate run left two dirty test buffers parked under `/tmp/omanotes-gate`;
`/tmp` was cleared on reboot; every launch in every workspace then advised opening a directory
that could not be opened, and the records, plaintext like every recovery record, would have
sat in `~/.local/state/omanotes/sessions/` indefinitely. Kept forever and never shown is the
worst of both: all of the exposure the threat model accepts for records, none of the value.

Herdr and Omawrite were read for comparison. Neither has the problem because neither parks per
root: Herdr stores no unsaved text at all, and Omawrite restores its single draft into whatever
window launches next. The accumulation is the shadow of ADR 0010's own boundary, which Matt
chose on security grounds and which stands.

## Decision

**State for a root that no longer exists is kept for seven days after the program last wrote
into it, then removed.** Concretely, at every launch, for each sibling directory under
`sessions/` other than the current root's:

- If the root path exists, the directory is never touched, whatever its age. The user can open
  that root, and ADR 0010 governs what happens then.
- If the root path is cleanly absent (a "not there", never a permission error or an unreachable
  mount) and the newest regular file in the directory, recovery records included, is older than
  `SessionStore::kVanishedRootRetention` (seven days), the whole directory is removed. If it
  held unsaved buffers, the status line says so once, naming the root and the count. Geometry-
  only state goes quietly; there was nothing in it to lose.
- Inside those seven days the launch notice no longer says "open it to recover". It names the
  root as gone, the buffer count, where the records are, and the day they expire, so the text
  can be retrieved by hand.
- A directory whose instance lock is held is never swept (ADR 0012): a live window may still
  have a vanished directory open.
- Symlinked entries are skipped, never followed, as everywhere else in the store.

Seven days, not thirty: Matt's ruling. The window exists for the cases where "absent at launch"
is not "gone": a drive not yet mounted, a directory renamed and about to be renamed back, a
deletion about to be regretted. Those resolve in days.

## Consequences

- The residue named under T-P4 in the threat model is bounded. Plaintext unsaved text for a
  workspace that has ceased to exist lives at most a week past the last write.
- The notice for a vanished root is actionable: it says where the text is and until when.
- Records for a root that still exists remain until that root is opened, which is unchanged.
  Someone who opens a workspace once, leaves it dirty, and never returns still accumulates one
  parked directory per such root. That is the ADR 0010 trade-off, not this one.
- The sweep must decide "gone" from a single `exists` call. A network mount that reports an
  error is kept; one that reports a clean absence for seven consecutive days is not. The
  seven-day grace is the guard against a mount that is merely slow on the morning of a launch.
- A partial failure of the removal (some files gone, `session.json` still there) is retried at
  the next launch; one where `session.json` went first leaves an unreadable directory the scan
  ignores. Not observed; noted.

## Rejected alternatives

- **Keep forever, document (the prior rule).** The pile grows, is never shown, and the notice
  gives advice that cannot be followed. Rejected by Matt.
- **Delete on first sight of a missing root.** Turns an unmounted drive into silent data loss
  on a routine morning. Exactly the failure the project refuses to ship.
- **Restore a vanished root's records wherever the app launches next, path stripped**
  (Omawrite's model). Answers ADR 0010's save-target objection but not its on-screen one: a
  private root's text surfacing while a shared root is open. Matt's boundary, kept.
- **A purge command inside the application.** A new feature under Phase 8's "no new
  features", and unnecessary once the bound exists.
- **Thirty days.** Overkill, per Matt.
