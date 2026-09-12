# Phase 5 key configuration review — 2026-09-07

Phase / blocks completed:
  5.3.2–5.3.4 complete the Phase 5 engineering work. Matt reviewed and merged
  search/help PR #10; baseline is eb2f5b2. The hands-on Phase 5 checkpoint remains
  open, and Phase 6 has not started.

Branch and PR:
  phase/5-key-configuration; implementation commit f923e6a.
  Draft PR: https://github.com/Linux-Alchemy/OmaNotes/pull/11

Files changed:
  src/app/keymap.* — bounded JSON loading and complete-candidate validation.
  src/ui/main_window.* — effective shortcut routing, help labels and fallback warning.
  src/ui/sidebar.cpp — removes the hardcoded route when its key is overridden.
  tests/unit/keymap_test.cpp — valid overrides, invalid mappings and file errors.
  tests/unit/main_window_test.cpp — Save, pane focus, help and leader precedence.
  tests/integration/launch_process_test.cpp — malformed-config process startup.
  CMakeLists.txt, src/CMakeLists.txt, tests/CMakeLists.txt — test and quality targets.
  PLAN.md, docs/configuration.md, docs/keymap-proposal.md, docs/decisions/0008-*,
  docs/phase-5-review.md and this report — decision, user guide and gate evidence.

Dependencies added or changed:
  None.

Commands run and actual results:

```text
$ cmake --preset dev
-- Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
-- Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
-- Configuring done (0.2s)
-- Generating done (0.1s)
-- Build files have been written to: ~/github/omanotes/build/dev

$ cmake --build --preset dev
[5/5] Linking CXX executable tests/omanotes_process_tests

$ ctest --preset dev --output-on-failure
16/16 Test #16: keymap ...........................   Passed    1.54 sec

100% tests passed out of 16

Total Test time (real) =  73.90 sec

$ cmake --build --preset dev --target format-check
[1/1] Checking C++ formatting

$ cmake --build --preset dev --target clang-tidy
[36/36][32.1s] /usr/bin/clang-tidy -p=~/github/omanotes/build/dev -quiet ~/github/omanotes/src/ui/main_window.cpp
[1/1] (1/2) Processing file ~/github/omanotes/src/ui/main_window.cpp.
[1/1] (2/2) Processing file ~/github/omanotes/src/ui/main_window.cpp.

$ cmake --build --preset dev --target security-check
[1/1] Checking executable hardening
security-check: PIE, RELRO, BIND_NOW, NX stack, and no RPATH: PASS
```

All six commands exited zero. The optional WrapVulkanHeaders discovery warning
is unchanged; configuring and building succeed.

FIRMITAS — does it hold up:
  Parser tests cover unknown commands/fields, invalid types, duplicate and
  shadowed sequences, lost help access, unsafe Vim/typing shortcuts, editor
  action collisions including chord prefixes, malformed JSON and the 64 KiB
  bound. UI tests prove rejected candidates retain all defaults and help works;
  valid overrides replace old routes and update help. A child process starts
  successfully with malformed config, logs its location and leaves it untouched.
  The dev preset enables ASan/UBSan; headless GUI tests disable leak detection.
  Existing Vim, atomic-save, conflict, search and root-boundary suites also run.
  Qt warnings may go to the desktop journal; the process test forces stderr
  logging so its diagnostic assertion is independent of desktop log routing.

UTILITAS — does it do what the outline promised:
  Known commands accept partial key overrides at the XDG config location.
  Invalid input falls back atomically with an actionable persistent warning.
  Space ? stays reachable and help shows effective bindings. Config values
  cannot execute programs or scripts. Existing keyboard/mouse command routes
  remain covered by the UI and workspace-search suites.

VENUSTAS — for Matt to judge:
  Whether custom keys feel natural, help accurately explains them, and the
  fallback warning is legible without obstructing normal editing.

Known limitations:
  Restart to reload config. The Space leader itself remains fixed. New direct
  keys use conservative Ctrl+Alt combinations; desktop shortcuts and keyboard
  layouts can prevent a physical combination from reaching the application.
  Editor action conflicts are checked against the installed initial view at
  startup. Later editor/desktop reconfiguration is not monitored.
  Search still reads saved Markdown with the bounds described in docs/search.md.

My reservations:
  Headless tests cannot establish the feel of focus transitions or how the
  chosen physical shortcuts interact with Matt's compositor and keyboard layout.
  That is the remaining acceptance work, not an automated claim of completion.

How to run it:

```sh
cd ~/github/omanotes
cmake --preset dev
cmake --build --preset dev
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./build/dev/src/omanotes .
```

For a disposable workspace and keymap, leaving existing notes/config untouched:

```sh
omanotes_trial=$(mktemp -d /tmp/omanotes-p5.XXXXXX)
mkdir -p "$omanotes_trial/config/omanotes" "$omanotes_trial/notes"
printf '# Acceptance note\nSearch needle\n' > "$omanotes_trial/notes/one.md"
cat > "$omanotes_trial/config/omanotes/keymap.json" <<'JSON'
{"leaderBindings":{"buffer.new":["n"]},"shortcuts":{"file.save":"Ctrl+Alt+Shift+S"}}
JSON
XDG_CONFIG_HOME="$omanotes_trial/config" ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  ./build/dev/src/omanotes "$omanotes_trial/notes/one.md"
```

What to inspect by hand:
  1. In the disposable run, type some text in Insert mode. Ctrl+S should no
     longer save; Ctrl+Alt+Shift+S should clear the modified marker and save.
     Escape, Space n should open a scratch buffer. Space ? should list both
     changed keys and retain the unchanged ones.
  2. Save a scratch note, switch with Shift+H/L, and close with Space b d.
     A dirty close must refuse; Space b D remains deliberate discard.
     Show the sidebar with Space e; use Ctrl+H/L to move between panes.
  3. Use Space Space to find `one`; use Space / to search `needle`. Navigate
     results with Ctrl+J/K and Enter, then repeat via mouse selection. Click
     the ? button and activate commands. Escape should return to editing.
  4. Close the disposable app. In the same shell, replace only its temporary
     keymap with malformed JSON and restart using the same launch command:

```sh
printf '{broken' > "$omanotes_trial/config/omanotes/keymap.json"
XDG_CONFIG_HOME="$omanotes_trial/config" ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  ./build/dev/src/omanotes "$omanotes_trial/notes/one.md"
```

  Expect a persistent warning with the filename and byte position. Space ?
  must open help; Space f n and Ctrl+S must be restored. The malformed file
  must remain unchanged. The temporary XDG directory also isolates KDE editor
  settings, so judge the regular launch for your usual editor appearance.

Decision required: approve / request changes / stop and redesign
  Accept Phase 5 after the hands-on session, or report findings on this branch.
  P6 remains behind PLAN.md's Matt gate. Vitruvius also requires: "Then stop.
  Do not begin the next phase because the current one went well."
