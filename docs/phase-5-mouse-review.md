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
  src/ui/main_window.* — a small `+` button sits after the last tab, running
    `buffer.new`. A close click on a clean buffer runs `buffer.close`
    outright; on a dirty one it brings that buffer to the front and asks
    Save / Discard / Cancel (Matt's direction, second review round). Save on
    a scratch routes through the existing save-as prompt and finishes the
    close once named; Save on a file-backed buffer writes and closes; Discard
    runs `buffer.close.discard`; Cancel keeps everything. The status label
    now word-wraps so a long message can no longer raise the writing pane's
    minimum width and squeeze the sidebar (Matt's second finding). Both new
    ids join routedOutsideLeader_ so the command audit stays truthful.
  tests/unit/main_window_test.cpp — mouseCreatesAndClosesBuffers covers the
    + button, Cancel, Save-then-name-then-close, Discard, and targeted clean
    close.
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
  The test clicks the real widgets: the `+` button opens a scratch buffer;
  a dirty tab's close button raises the prompt; Cancel keeps buffer and text;
  Save names a scratch through the save-as prompt, writes it inside the root,
  and closes it; Discard closes without touching the disk; a clean close
  needs no prompt and acts on the clicked tab rather than the active buffer.
  All disk mutation still flows through the registry commands and the atomic
  writer — the dialog decides, it does not write. Sanitizers ran as part of
  the dev preset.

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
  Keyboard `Space b d` keeps its status-line guard rather than the dialog —
  the two routes now deliberately diverge, dialog for mouse, guard for
  keyboard, per Matt's direction; say the word if they should converge.
  No context menu on tabs. The reported highlight around spaces while typing
  is not addressed here: nothing in Omanotes configures whitespace or search
  highlighting, so it needs diagnosis with Matt before anything is changed.

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
  2. Type into it, then click its tab's close button. The Save / Discard /
     Cancel prompt should appear; Cancel keeps everything. Close again,
     choose Save, name it in the save-as line, press Enter — the file lands
     in the workspace and the tab closes.
  3. Dirty a file-backed buffer and close it choosing Discard: it closes,
     the file on disk keeps its saved content.
  4. With the sidebar open and widened, provoke a long status message (a
     `Space b d` on a dirty buffer will do). The sidebar must not move; the
     message wraps in the status line instead.
  5. Judge the prompt's wording and the strip's look: do the close buttons,
     `+`, and dialog belong, or clutter?

Decision required: approve / request changes / stop and redesign
  This completes the mouse gap from your gate session. The Phase 5 Matt gate
  itself stays open until you re-run the checkpoint session and accept.
