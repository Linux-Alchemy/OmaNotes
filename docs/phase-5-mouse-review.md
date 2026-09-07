# Phase 5 mouse buffer actions review — 2026-09-07

Phase / blocks completed:
  No new blocks. This is a gate fix inside the Phase 5 checkpoint: Matt's
  hands-on session passed the keyboard paths and help overlay but found that
  `buffer.new` and `buffer.close` had no mouse route, contradicting the
  checkpoint line "every core workflow has a tested keyboard path and a mouse
  path where visible". Verdict recorded as request changes.

Branch and PR:
  fix/5-mouse-buffer-actions, branched from main at 5707675 (PR #11 merge).

Files changed:
  src/ui/buffer_strip.* — tabs gain close buttons; a close click reports the
    clicked buffer's identity, never an index the registry has to guess at.
  src/ui/main_window.cpp — a small `+` button sits after the last tab; both
    new controls run the existing registry commands (`buffer.new`,
    `buffer.close`) through the same runCommand path as tab and tree clicks,
    and both ids join routedOutsideLeader_ so the command audit stays truthful.
  tests/unit/main_window_test.cpp — new mouseCreatesAndClosesBuffers test.
  PLAN.md — Change Log entry, checkpoint evidence pointer, and Matt's parked
    default-direct-key idea recorded as unapproved.
  docs/phase-5-mouse-review.md — this report.

Dependencies added or changed:
  None.

Commands run and actual results:

```text
$ cmake --preset dev
-- Build files have been written to: /home/reaper/github/omanotes/build/dev

$ cmake --build --preset dev
[64/64] Linking CXX executable tests/omanotes_process_tests

$ ctest --preset dev --output-on-failure
16/16 Test #16: keymap ...........................   Passed    1.46 sec

100% tests passed out of 16

Total Test time (real) =  72.32 sec

$ cmake --build --preset dev --target format-check
[1/1] Checking C++ formatting

$ cmake --build --preset dev --target clang-tidy
[36/36][32.3s] /usr/bin/clang-tidy -p=... src/ui/main_window.cpp

$ cmake --build --preset dev --target security-check
security-check: PIE, RELRO, BIND_NOW, NX stack, and no RPATH: PASS
```

All six commands exited zero.

FIRMITAS — does it hold up:
  The new test clicks the real widgets: the `+` button opens a scratch buffer;
  a close click on a dirty tab is refused with the same guard message as
  `Space b d`; a close click acts on the clicked tab rather than the active
  buffer; the file on disk is untouched throughout. Because both controls run
  the registry descriptors, every existing close-path test (dirty guard,
  final-buffer scratch fallback, editor teardown, watcher unwatch) covers the
  mouse route too. Sanitizers ran as part of the dev preset.

UTILITAS — does it do what the outline promised:
  Opening and closing a buffer now each have a mouse path where the buffer is
  visible — on its tab — closing the gap Matt found. No new behaviour was
  invented: the controls are strictly second routes to commands the keyboard
  already runs.

VENUSTAS — for Matt to judge:
  Whether the close buttons and the `+` button sit quietly in the strip or
  clutter it. The `+` is flat (autoRaise) and takes no focus; close buttons
  are the style's stock affordance. If they read as noise, saying so is a
  complete review.

Known limitations:
  Mouse discard-close does not exist: a dirty tab's close button refuses and
  the status line names `Space b D`, matching the keyboard's deliberate
  two-step. No context menu on tabs. Both are one command away if wanted, but
  neither was approved and restraint seemed the safer default.

My reservations:
  Close-button placement (left or right of the label) follows the Qt style in
  use, so it may sit differently under your theme than under the test
  environment's. Cosmetic, but I have not seen it under Omarchy's style —
  that is yours to see.

How to run it:

```sh
cd /home/reaper/github/omanotes
git switch fix/5-mouse-buffer-actions
cmake --preset dev
cmake --build --preset dev
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./build/dev/src/omanotes .
```

What to inspect by hand:
  1. Click `+` at the end of the buffer strip. A `[No Name]` scratch buffer
     should open and take focus, exactly as `Space f n` does.
  2. Type into it, then click its tab's close button. It must refuse with the
     status message naming `Space b D`, and the tab must stay.
  3. Open a file from the sidebar, keep the dirty scratch active, and click
     the *file's* close button. The file's tab closes; the scratch and its
     text remain untouched.
  4. Close a clean buffer by its close button; the last one closing should
     leave a fresh scratch, as with the keyboard.
  5. Judge the strip's look: do the close buttons and `+` belong, or clutter?

Decision required: approve / request changes / stop and redesign
  This completes the mouse gap from your gate session. The Phase 5 Matt gate
  itself stays open until you re-run the checkpoint session and accept.
