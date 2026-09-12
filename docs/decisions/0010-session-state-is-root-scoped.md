# ADR 0010: Session State Is Root-Scoped

- **Status:** Accepted; constrains Phase 7 from Task 7.1 onward. The "announced, not hidden"
  clause is superseded by ADR 0016 (2026-09-12); the root scoping stands.
- **Date:** 2026-09-09

## Context

Phase 7 restores the user's desk — buffers, focus, sidebar, cursor positions — and
protects dirty work across a crash. The plan keys the session store by workspace
root (`SessionStore::load(const WorkspaceRoot&)`, block 7.2.1) but never states
what happens when Omanotes is next launched in a *different* root while another
root still holds unrecovered dirty buffers. Matt raised the case before dispatch:
work left unsaved in root A, app killed, app reopened in root B.

The workspace root is the trust boundary the whole application is built on. The
sidebar (Phase 3), saves (ADR 0005), external-change handling (ADR 0006) and the
reading view's resource policy (ADR 0009) all refuse to see or write anything
outside the resolved root. Session restore must not become the one path that
crosses it.

## Decision

**Snapshots and recovery records belong to the root they were created in, and are
restored only there.** Launching in root B loads B's snapshot or starts clean. Root
A's structural snapshot and dirty recovery records stay parked, untouched, until
A is next opened, at which point its buffers come back as dirty and unsaved. Block
7.1.3's path validation applies to recovery records too: a record whose target
path is not under the resolved root is never restored into that root.

Scratch buffers with no file path belong to the root in which they were created,
under the same rule.

**The parked work is announced, not hidden.** On launch, if any *other* root holds
unrecovered dirty buffers, the status line shows a one-line notice in the spirit
of Neovim's swap-file warning: the other root's display name and a count, for
example "Unsaved work waiting in ~/notes (2 buffers). Open it to recover." The
notice is built from snapshot metadata only. It never reads, previews or displays
buffer contents, and it takes no action on the user's behalf.

Task 7.1.1's format specification must record enough per-root metadata (root
identity, display name, count of live recovery records) to produce this notice
without opening recovery records.

## Consequences

- The user is never surprised by another workspace's text appearing on screen,
  and a save dialog never defaults into the wrong root because a recovered buffer
  carried a path from elsewhere.
- Recovering root A's work requires reopening root A. The launch notice makes that
  a known, visible step rather than something to remember.
- The snapshot store gains a small cross-root index or a cheap scan of per-root
  metadata at startup. It is bounded by the number of roots ever opened and must
  stay so; a scan that reads recovery records to count them is not acceptable.
- Cleaning up state for a root that no longer exists is a Phase 7 retention
  question, not a reason to relax the boundary.

## Rejected alternatives

- **Recover dirty buffers globally into whichever root is open:** Crosses the
  trust boundary. A private root's text could surface while a shared root is on
  screen, and a recovered buffer's save target would have to be invented inside
  the wrong root. Matt rejected it outright on security grounds.
- **Say nothing about parked work in other roots:** Simplest, and a silent
  failure mode of exactly the kind the project refuses to ship. Dirty work would
  depend on the user remembering where they left it.
- **Prompt on launch and offer to switch roots:** More helpful, but a modal on
  every start is not the Omarchy feel, and switching roots at launch is a Phase 3
  concern this phase should not reopen. A passive notice is enough; a command to
  jump to the other root can be added later without changing this decision.
