# Search and help review — 2026-09-07

Phase / blocks completed:
  5.2.1–5.2.4 and 5.3.1. Phase 5 remains incomplete: 5.3.2–5.3.4 await
  the keymap-format decision, followed by Matt's hands-on phase gate.
  Baseline b5260c8 already records accepted P4 and completed 5.1.

Branch and PR:
  phase/5-search-and-help. Implementation commit b302d69.
  Draft PR: https://github.com/Linux-Alchemy/OmaNotes/pull/10

Files changed:
  src/workspace/file_index.* — root-scoped Markdown enumeration and fuzzy scoring.
  src/workspace/text_search.* — bounded literal text search and safe previews.
  src/ui/search_palette.* — cancellable worker, result list and preview.
  src/ui/help_overlay.* — enabled commands and their keys, with activation.
  src/ui/main_window.* — registry routes, help button, sidebar leader routing.
  tests/integration/workspace_search_test.cpp — safety, bounds, cancellation,
    responsiveness, ordering, preview and keyboard/mouse selection.
  tests/unit/main_window_test.cpp — result opening, contextual help and sidebar access.
  CMakeLists.txt, src/CMakeLists.txt, tests/CMakeLists.txt — build and quality coverage.
  PLAN.md, docs/search.md, docs/keymap-proposal.md, docs/phase-5-review.md — scope,
    search policy, pending decision and evidence.

Dependencies added or changed:
  None.

Commands run and actual results:

```text
$ cmake --preset dev
-- Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
-- Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
-- Configuring done (0.3s)
-- Generating done (0.1s)
-- Build files have been written to: /home/reaper/github/omanotes/build/dev

$ cmake --build --preset dev
[23/23] Linking CXX executable tests/omanotes_ui_tests

$ ctest --preset dev --output-on-failure
15/15 Test #15: workspace-search .................   Passed    2.47 sec
100% tests passed out of 15
Total Test time (real) =  88.34 sec

$ cmake --build --preset dev --target format-check
[1/1] Checking C++ formatting

$ cmake --build --preset dev --target clang-tidy
[34/34][56.6s] /usr/bin/clang-tidy -p=/home/reaper/github/omanotes/build/dev -quiet /home/reaper/github/omanotes/src/ui/main_window.cpp
[1/1] (1/2) Processing file /home/reaper/github/omanotes/src/ui/main_window.cpp.
[1/1] (2/2) Processing file /home/reaper/github/omanotes/src/ui/main_window.cpp.

$ cmake --build --preset dev --target security-check
[1/1] Checking executable hardening
security-check: PIE, RELRO, BIND_NOW, NX stack, and no RPATH: PASS
```

FIRMITAS — does it hold up:
  All six commands exited zero. QtTest summaries record zero failed and zero
  skipped cases. ASan/UBSan are active through the dev preset; the existing
  headless GUI environment disables leak detection. No TSan run is claimed.
  Search tests exercise root escape/cycles, binary and invalid UTF-8 content,
  oversized files/lines, result limits, cancellation and stale-query suppression.
  The 3,000-file fixture reported 92 ms in this run; this is a fixture observation,
  not a performance guarantee for every workspace or filesystem.

UTILITAS — does it do what the outline promised:
  File finding and text search now have keyboard and mouse paths, previews and
  bounded background work. Help lists enabled registry commands and invokes
  their existing execution paths. Configurable keys are still outstanding.

VENUSTAS — for Matt to judge:
  Whether the search dialog, preview proportions and small help button feel at
  home in the writing workflow; whether keys, focus return and help are clear.

Known limitations:
  Search reads saved Markdown; unsaved buffer text is preserved but not searched.
  Text result coordinates refer to disk content and may differ in a dirty buffer.
  Search excludes symlinks/hidden directories and has documented size/result
  limits. Filename results may have no preview for unsupported content.
  No reading view, persistent index, executable configuration or session work.
  The keymap parser and its validation tests await the format decision.

My reservations:
  Cancellation keeps the UI responsive, but destruction joins the worker and can
  wait for an OS filesystem call on a stalled filesystem. Search limits can omit
  content; absence of results is not proof that no matching text exists.
  The new layout and focus behaviour still need Matt's hands-on judgement.

How to run it:

```sh
cd /home/reaper/github/omanotes
cmake --preset dev
cmake --build --preset dev
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./build/dev/src/omanotes .
```

What to inspect by hand:
  1. In Normal mode, press Space Space, type `plan`, inspect the preview, then
     Enter. Repeat with Space f f; use the mouse to choose a result.
  2. Press Space /, search `Phase 5`, move through results with Ctrl+J/K, then
     Enter. It should open the matching line. Escape should dismiss a search.
  3. Press Space ?, inspect the available commands and activate New buffer.
     Repeat through the small ? button. Show the sidebar with Space e and
     verify Space ? also works there.
  4. Type quickly in a search, then cancel and reopen it. Old results should
     never replace the current query's results or steal focus after cancellation.

Decision required:  approve / request changes / stop and redesign
  First, approve or revise docs/keymap-proposal.md so 5.3.2–5.3.4 can proceed.
  This review does not claim that the Phase 5 checkpoint has passed.
