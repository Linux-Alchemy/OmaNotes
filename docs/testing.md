# Building and testing

> Block 8.3.4. Everything needed to build OmaNotes, run every check, and know what each
> check proves, on a machine that has never seen the repository. This page was written by
> following it from a fresh clone; the transcript is in the pull request that added it.

## Prerequisites

Arch Linux with these packages, all from the official repositories:

```
qt6-base ktexteditor extra-cmake-modules cmake ninja clang git
```

`namcap` is wanted only to lint the package. The versions the checks last passed on are in
`docs/development-baseline.md`; the floors the build enforces are in `CMakeLists.txt`.

## The three presets

`CMakePresets.json` defines three configurations. Each builds into its own directory under
`build/`, so they never interfere.

| Preset | Directory | What it is for |
| --- | --- | --- |
| `dev` | `build/dev` | Debug, Clang, every warning as an error, AddressSanitizer and UndefinedBehaviorSanitizer on. Where the tests find bugs. |
| `release` | `build/release` | Optimised, sanitizers off, every hardening flag on. The binary the package ships and the security check measures. |
| `fuzz` | `build/fuzz` | The libFuzzer harnesses. Not part of `all`, not part of the tests, never packaged. |

Presets are read from the repository root. Every command below assumes you are there; run
from inside `build/` and CMake reports that it cannot find `CMakePresets.json`.

## The gate

This is what "the gate" means whenever a pull request or the plan says it. Each command is
its own check with its own output, and a chained exit status is not evidence: read each.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target clang-tidy
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cmake --build --preset release --target security-check
```

What each proves:

- **`cmake --build --preset dev`** compiles with `-Werror` and a strict warning set
  (`cmake/Warnings.cmake`). A warning is a failed build.
- **`ctest --preset dev`** runs the 24 suites below under both sanitizers. About 100
  seconds. Every test has a timeout, so a hang is a failure, not a wait.
- **`format-check`** is `clang-format --dry-run --Werror` over every source file the tree
  contains. It changes nothing; `clang-format -i <files>` is how you fix what it reports.
- **`clang-tidy`** runs `run-clang-tidy` over `src/` and `tests/` with the analyzer,
  `bugprone-*`, `performance-*`, and `portability-*` families as errors. Silence is a pass.
- **`ctest --preset release`** runs the same 24 suites against the optimised binary, about
  12 seconds. It is in the gate because two test assumptions have been found that only held
  under the sanitizers' allocator (Change Log, 2026-09-11); the release run is the one that
  resembles what users run.
- **`security-check`** reads the release binary and confirms PIE, full RELRO, a
  non-executable stack, no RPATH, the stack protector, CET marks, and fortified source. Run
  against the dev binary it confirms everything but fortification, which needs
  optimisation, and says so.

Nothing needs to be exported first. Each test sets its own environment: the offscreen Qt
platform, the Fusion style, sanitizer options, and private `XDG_*` directories under
`build/<preset>/tests/`, so a test run never reads or writes your real configuration or
state. Running a test binary by hand, outside CTest, is the one time you set them yourself:

```sh
QT_QPA_PLATFORM=offscreen QT_STYLE_OVERRIDE=Fusion ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  ./build/dev/tests/omanotes_ui_tests refusesToWriteAReadOnlyNoteWithoutBang
```

The test name selects one case; omit it for the whole suite. Leak detection is off under
CTest because the harness traces child processes, which LeakSanitizer cannot inspect; it is
on when you launch the dev binary yourself:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./build/dev/src/omanotes ~/notes
```

Drop `detect_leaks=0` to see leak reports on exit.

## The suites

Twenty-four, in `tests/`, each a Qt Test executable and one CTest entry. `unit/` tests one
class through its public surface; `integration/` drives real files, real processes, or the
real window; `property/` generates inputs. The names are the documentation; the table says
what each is about so you know where to look.

| CTest name | Exercises | The question it answers |
| --- | --- | --- |
| `buffer-registry` | `core/buffer_registry` | Buffer identity, order, activation, and that two routes to one file are one buffer |
| `command-registry` | `core/command_registry` | Every command has one implementation and a route; disabled commands say why |
| `prefix-router` | `app/prefix_router` | The leader key collects a sequence, reports each step, and never swallows ordinary input |
| `keymap` | `app/keymap` | `keymap.json` is validated whole, refuses editor collisions, and a bad file leaves defaults |
| `launch-request` | `app/launch_request` | The launch table from the outline, symlinked roots, escapes, and the wide-root notice |
| `launch-process` | the real binary as a child process | Every approved launch form starts; a missing file exits cleanly; a bad keymap still starts |
| `process-startup` | the real binary with `--smoke-test` | The built executable starts and exits at all |
| `editor-adapter` | `editor/ktext_editor_adapter` | The KTextEditor boundary: modes, modification, no URL ever given to the document, modelines inert |
| `vim-behaviour` | KTextEditor's Vi mode through the window | The mandatory Vim matrix from `docs/vim-acceptance.md` |
| `document-store` | `persistence/document_store` | Where a save may go: inside the root, Markdown only, no implicit directories, read-only asked |
| `atomic-save` | `persistence/atomic_file_writer` | Temporary-and-rename, permissions kept, symlinks that leave the root refused, every injected failure leaves the note intact |
| `conflict-detector` | `persistence/conflict_detector` | Content hashes, not timestamps, decide whether the disk moved |
| `note-reader` | `persistence/note_reader` | Reads stop at 16 MiB, refuse symlinks, and never block on a pipe |
| `file-watcher` | `workspace/file_watcher` | Rapid writes coalesce; rename-replace is seen; removal is seen |
| `file-tree-model` | `workspace/file_tree_model` | Lazy, Markdown and directories only, hidden and symlinked entries skipped |
| `workspace-search` | `workspace/file_index`, `text_search`, the palette | Scoring, safe text only, bounds and cancellation, newer query wins |
| `markdown-render` | `ui/markdown_view` | Raw HTML inert, remote and escaping images refused, every hostile scheme in the fixture refused |
| `theme-adapter` | `ui/theme_adapter` | Both Omarchy schemas, readable fallbacks, the text scale and its `config.toml` override |
| `ui-structure` | `ui/main_window` | The window as a whole: regions, focus, keys, saves, refusals, mouse routes; the largest suite |
| `session-snapshot` | `session/session_snapshot` | Byte-for-byte round trip, defaults, refusals, version handling, root checks |
| `session-store` | `session/session_store` | Owner-only per-root state, atomic replacement, symlinks refused, the seven-day sweep |
| `recovery-store` | `persistence/recovery_store` | Records are owner-only, survive failures, are never paths, and restore never writes the workspace |
| `session-restore` | the controller with the real window | Clean close and forced kill both come back; other roots are silent; two instances; the lock |
| `parser-properties` | every parser | Generated mutations of snapshots, records, keymaps, themes, and links never escape their rules |

## Fuzzing

Five libFuzzer harnesses, one per parser that takes untrusted bytes, with seed corpora under
`fuzz/corpus/`. Clang only.

```sh
cmake --preset fuzz
cmake --build --preset fuzz
ASAN_OPTIONS=alloc_dealloc_mismatch=0 ./build/fuzz/fuzz/fuzz_keymap fuzz/corpus/keymap -max_total_time=60
```

The harnesses are `fuzz_session_snapshot`, `fuzz_recovery_record`, `fuzz_keymap`,
`fuzz_theme`, and `fuzz_link`; the corpus directories are `recovery`, `keymap`, `theme`, and
`links` (the snapshot harness uses `tests/fixtures/session`). The `ASAN_OPTIONS` setting
silences one mismatch report that originates inside Qt's JSON parser, not in this code
(`docs/dependency-review.md`); every other check stays on. A crash, any other sanitizer
report, or an assertion inside a harness is a finding. The fuzzers are not part of the gate;
they were run for block 8.1.2 and are run again when a parser changes.

## The package check

`makepkg` in `packaging/arch/` builds the release preset from a fresh clone of `main` and, in
its check step, runs `ctest --preset release` and the security check on the binary about to
be packaged. A failure means no package. `docs/packaging.md` has the whole route.

## What is not automated

There is no continuous integration, by ruling (threat model F-9). Every gate is run by the
person opening a pull request and its output pasted into the request. The `cert-*` tidy
checks and automated leak detection are not enabled (F-19). The manual checkpoint at each
phase gate, Matt running the application, is not a test and is not replaced by one.
