# Task 6.1 reading view review — 2026-09-07

Phase / blocks completed:
  6.1.2, 6.1.3, 6.1.4 — the remainder of Task 6.1, dispatched by Matt after
  he approved and merged the 6.1.1 policy (ADR 0009, PR #14).

Branch and PR:
  phase/6.1-reading-view, branched from main at ad7d5b1.

Files changed:
  src/core/view_mode.hpp — Writing/Reading, per buffer, with a toggle.
  src/ui/markdown_view.* — the reading view: Qt-native CommonMark+GFM with
    raw HTML never parsed; classifyLink and resolveImageSource as pure,
    separately testable policy functions; every image requested eagerly at
    render so refusals are complete up front; refused resources show a small
    crossed placeholder and are recorded per render; hover emits the real
    link target.
  src/ui/main_window.* — `Space m` toggles the active buffer's mode and is
    the enabled `view.reading` command (its mouse route: the help overlay).
    The projection preserves cursor, text, and modified state; a clean
    buffer's external reload re-renders an active reading view; link clicks
    route through `file.open` for in-root notes, the status line shows hover
    targets, refusals, and READING as the mode.
  tests/integration/markdown_render_test.cpp — 12 fixture-driven cases.
  tests/unit/main_window_test.cpp — toggle/state-preservation test; two
    contract updates (help lists view.reading; stack counts include the
    permanent reading view).
  src/CMakeLists.txt, tests/CMakeLists.txt — new sources and test target.
  PLAN.md, docs/phase-6.1-review.md — bookkeeping and this report.

Dependencies added or changed:
  None. Qt's own Markdown support; KSyntaxHighlighting-based code-block
  colouring deliberately deferred (monospace only in v1).

Commands run and actual results:

```text
$ cmake --preset dev
-- Build files have been written to: ~/github/omanotes/build/dev

$ cmake --build --preset dev
[21/21] Linking CXX executable tests/omanotes_markdown_tests

$ ctest --preset dev --output-on-failure
100% tests passed out of 17
Total Test time (real) =  75.37 sec

$ cmake --build --preset dev --target format-check
[1/1] Checking C++ formatting

$ cmake --build --preset dev --target clang-tidy
(38 files, no findings)

$ cmake --build --preset dev --target security-check
security-check: PIE, RELRO, BIND_NOW, NX stack, and no RPATH: PASS
```

FIRMITAS — does it hold up:
  The markdown-render suite feeds every ADR 0009 fixture to the real view:
  raw HTML renders as inert text; five remote image forms are refused with
  zero network paths in the code at all; seven traversal/absolute forms are
  refused including a genuinely existing out-of-root target; the scheme
  table (javascript, data, vscode, ssh, magnet, mailto, file, protocol-
  relative, case games) refuses everything but http/https, in-root `.md`,
  and bare anchors; a runtime symlink escape is refused by the same
  canonical containment the sidebar uses; an 11 MiB file and a 9000px image
  are refused by cap; malformed and pathological input (2 MiB line,
  200-deep quotes, 2000-row table) complete rendering. Two renders of the
  same input produce identical documents and leave the source untouched.
  Sanitizers ran throughout. During development the tests caught a real
  hole: lazy layout meant images were never requested headlessly, so
  refusals were silently empty — render now requests every image eagerly.

UTILITAS — does it do what the outline promised:
  `Space m` flips between styled source and a secure reading view, per
  buffer, without disturbing cursor, text, or modified state — the plan's
  user-visible result for this task. Obsidian-style vault behaviour holds:
  workspace-relative note links open in-app, local raster images render,
  https links reach the browser with the target shown on hover first.

VENUSTAS — for Matt to judge:
  Whether the projection reads like a restrained Obsidian page or a Qt demo.
  Styling is deliberately light — system face one point up, 130% line
  height, wide margins, Qt's own heading/list/quote/code shapes — because
  colour and theme belong to Task 6.2. If the bones look wrong now, say so
  before 6.2 paints them.

Known limitations:
  Colours are the stock palette until 6.2. Code blocks are monospace but
  not syntax-coloured (deferred with KSyntaxHighlighting available when
  wanted). The toggle's only mouse route is the help overlay's clickable
  command list — a visible toggle control is a design call I did not make
  for you. Reading view scroll position is not preserved across toggles.
  j/k do not scroll the reading view (arrows and PageUp/Down do); teaching
  it Vim motions is a venustas call for the gate.

My reservations:
  The eager image pass walks every fragment at render; on a normal note it
  is nothing, and the pathological fixtures stay fast, but an enormous
  gallery note would do its refusals up front rather than as you scroll. I
  chose deterministic security over lazy loading and would make the same
  choice again — but it is a choice, not a law.

How to run it:

```sh
cd ~/github/omanotes
git switch phase/6.1-reading-view
cmake --preset dev && cmake --build --preset dev
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./build/dev/src/omanotes .
```

What to inspect by hand:
  1. Open a real note, `Space m`: the projection should appear with READING
     in the status line. `Space m` again: cursor exactly where you left it,
     [+] state unchanged.
  2. In a note with `![img](assets/...)` and links: local image renders;
     hover a link and watch the status line show the true target; click an
     https link (browser opens, status says so); click a note link (opens
     in Omanotes, in writing mode).
  3. Paste a hostile line — `[click](javascript:alert(1))` or a remote
     image — toggle to reading, click it: a status-line refusal naming the
     scheme, and a small crossed placeholder for the image.
  4. Provoke an agent edit (echo into the file) while reading a clean
     buffer: the projection should update in place.
  5. Judge the venustas: margins, line air, heading weight. Is it a page
     you would read?

Decision required: approve / request changes / stop and redesign
  Task 6.2 (Omarchy theme + focused-pane visibility) remains before the
  Phase 6 checkpoint and your gate.
