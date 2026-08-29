# Vim Acceptance Suite

> Agreed with Matt on 2026-08-29 for the Phase 2 KTextEditor feel gate.

## Decision rule

A mandatory behaviour is acceptable only when its automated test passes or its recorded manual
check passes. Differences from Neovim are classified explicitly as **accept**, **configure
around**, or **blocker**. Omanotes does not quietly redefine Vim.

## Mandatory matrix

| Area | Required behaviour | Check |
| --- | --- | --- |
| Modes | Normal, Insert, Visual character, Visual line, and Visual block | Automated pass |
| Motions | `h j k l`, `w b e`, `0 ^ $`, `gg G`, `{ }`, and counts | Automated core pass; manual pass |
| Operators | `d c y` with motions; `dd cc yy`; `x`, `r`, `J` | Automated core pass; manual pass |
| Text objects | `iw aw`, `i" a"`, `i( a(`, and `ip ap` | Automated core pass; manual pass |
| Registers and paste | Unnamed, named, black-hole, `p`, and `P` | Automated pass with accepted deviation below |
| History and repeat | `u`, `Ctrl+R`, and `.` | Automated pass |
| Search | `/`, `?`, `n`, `N`, `*`, and `#` | Automated core pass; manual pass |
| Marks | `ma`, `'a`, and `` `a `` | Automated core pass; manual pass |
| Command mode | Enter/cancel command mode and substitute text | Automated pass |
| Mouse | Cursor placement and character, line, and drag selection | Manual pass |
| Mappings | Define, exercise, and remove one Normal-mode mapping | Automated pass |

Macros, `f/F/t/T`, `%`, and change-list navigation are useful but are not Phase 2 blockers unless
Matt promotes them after hands-on use.

## Application help binding

The approved plan assigns the contextual Herdr-style help overlay to `Leader+?` in Phase 5. Bare
`?` therefore remains Vim backward search inside the editor. This is not a Vim deviation and keeps
the application-level binding reachable without swallowing an editor command.

## Manual feel pass

Matt will use a substantial Markdown sample to check:

1. All mandatory motions, operators, text objects, searches, marks, and visual modes that are
   awkward or brittle to simulate under the offscreen Qt test platform. **Passed 2026-08-29.**
2. Mouse cursor placement and selection in the real Wayland session. **Passed 2026-08-29.**
3. Undo grouping, selection edges, status-mode feedback, and whether the overall interaction feels
   recognisably like daily Neovim use. **Passed 2026-08-29.**

Matt reported that his usual Vim motions function and mouse dragging works. `Ctrl+H` does not move
focus to the skeletal sidebar; that is expected in the Phase 2 single-editor constraint and is not
an editor deviation. Pane and sidebar navigation will be evaluated when those application contexts
become functional.

The final leader smoke test passed in the real Wayland session: Insert-mode spaces, Normal-mode
commands, `Ctrl+B`, and mouse workflows were not swallowed. `Space+?` initially exposed a physical
key-event bug where the leading Shift cancelled the pending prefix and `/` opened editor search.
Modifier-only keypresses now leave the prefix pending; an automated regression test and Matt's
manual retest both confirm that `Space+?` reaches the planned unavailable-command feedback.

Record each deviation below before the Phase 2 checkpoint.

## Deviations

| Behaviour | Observed difference | Classification | Decision |
| --- | --- | --- | --- |
| Explicit named yank does not also update the unnamed register | After `"ayy`, `"ap` uses the named contents while bare `p` retains the prior unnamed contents | Accept | Matt accepted on 2026-08-29; named, unnamed, and black-hole registers each work, and patching KTextEditor internals is out of scope |
