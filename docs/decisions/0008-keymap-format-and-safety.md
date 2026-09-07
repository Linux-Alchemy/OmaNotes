# ADR 0008: Keymap Format and Safety

- **Status:** Implemented; Phase 5 hands-on acceptance pending
- **Date:** 2026-09-07

## Context

Task 5.3 requires partial overrides of known application commands, discoverable
help, and safe startup after malformed configuration. PR #10 included a JSON
proposal. After reviewing and merging that PR, Matt instructed continuation
according to the plan; 5.3.2–5.3.4 are the remaining Phase 5 blocks.

## Decision

Use the proposed `omanotes/keymap.json` under XDG config, with `leaderBindings`
and `shortcuts` objects. Missing entries retain defaults. Validate a complete
candidate before replacing any bindings. Keep `Space ?` as a reserved help
route, and show effective keys in contextual help.

Direct overrides use conservative Ctrl+Alt combinations or the existing
application exceptions. Check them against the installed editor's action
shortcuts, including multi-key sequence prefixes. Preserve canonical Vim keys
and text-entry controls. The format contains no executable values.

Use Qt's existing JSON and key-sequence APIs; no new parser dependency. Bound
input to 64 KiB. Invalid input shows an actionable persistent warning while
launching with defaults. Read only at startup; never create or modify the
user's configuration.

## Consequences

- Binding changes are atomic from the user's perspective, and help cannot
  advertise a stale hardcoded key after an override.
- Removing a default direct binding also removes its old application route.
- Not every possible physical key combination is supported. The conservative
  policy and editor collision check protect editing behaviour; desktop-level
  shortcuts still require hands-on validation on the target machine.
- The Space leader itself remains fixed in this schema. Its command sequences
  are configurable; Ctrl+B remains Vim page-backward.

## Alternatives

- Per-entry fallback would mix defaults and overrides after an error, making
  the effective keys harder to predict. Reject the whole candidate instead.
- A general settings or executable configuration language adds scope and risk
  beyond Task 5.3. Neither is introduced.
