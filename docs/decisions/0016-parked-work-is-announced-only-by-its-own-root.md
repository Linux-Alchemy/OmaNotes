# ADR 0016: Parked work is announced only by its own root

- **Status:** Accepted (2026-09-12, Matt's ruling during the Phase 8 daily-use trial)
- **Supersedes:** the "parked work is announced, not hidden" clause of ADR 0010; the launch-notice
  clause of ADR 0015
- **Related:** ADR 0010 (session state is root-scoped, which stands), ADR 0015 (seven-day
  retention for vanished roots, which stands and now runs silently)

## Context

ADR 0010 parks unsaved work with the root it was written in and, so that it would not be
forgotten, announces it from every other root at launch: "Unsaved work waiting in ~/notes
(2 buffers). Open it to recover." The alternative, saying nothing, was rejected there as "a
silent failure mode of exactly the kind the project refuses to ship".

The first fix for finding F-22 (this branch, ADR 0015) kept that notice and reworded it for a
root that no longer exists. Matt ran it, saw the line wrap to two rows of the status bar, and
asked the prior question: why is a workspace being told about another workspace at all? Opening
the root that holds the work already restores it dirty and says so. Neovim, the model for the
whole editing surface, warns about a swap file when you open *that file*, not when you open
anything else.

His ruling reverses his own ADR 0010 clause, knowingly: a quieter open, and some
responsibility left with the user. In his words, Neovim has sharp edges and makes no excuses
for them; anyone who wants that level of hand-holding can use Obsidian, and that is not what
OmaNotes is about. The orchestrator recommended keeping the notice for vanished roots only,
since that text can never announce itself, and was overruled: Matt does not want to hear about
a root that no longer exists either.

## Decision

**A root announces its own parked work when it is opened, and nothing else does.**

- On launch, the restore report says "recovered N with unsaved changes" for this root, as it
  always has. That is the whole notification.
- No launch notice about any other root, whether its directory exists or not.
- The ADR 0015 sweep of state for vanished roots runs silently. The text is already gone by the
  time a line could be shown; the line would be noise.
- `ApplicationController::parkedWorkNotice` is removed. `SessionStore::listSiblings` stays as
  the sweep's metadata-only scan.

## Consequences

- Launch is quiet. The status line at start carries this root's restore report and its
  warnings, nothing about elsewhere.
- Unsaved work in a root the user never reopens is theirs to remember. It is safe on disk,
  owner-only, until they do. For a root that has vanished, it is removed after seven days
  without anyone having been told it was there. Accepted, in those words.
- The threat model's T-P4 residue is bounded (ADR 0015) and unannounced (this ADR).
- The F-10 test that exercised the notice against a vanished root goes with the notice. The
  check-before-dereference in `start()` that F-10 was about remains.
- A command to list parked work across roots could be added later without reopening this
  decision, as ADR 0010 already noted. It would be a feature, and Phase 8 is not the phase.

## Rejected alternatives

- **Keep the cross-root notice as ADR 0010 wrote it.** Noise on every launch about somewhere
  else, and for a vanished root, advice that cannot be followed.
- **Keep it for vanished roots only** (the orchestrator's recommendation). Still a notice
  about somewhere else; rare and self-limiting, but Matt does not want it.
- **Announce what the sweep removed.** The text is gone; the user can do nothing with the line.
