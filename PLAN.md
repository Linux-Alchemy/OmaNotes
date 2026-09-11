# Omanotes — Build Plan

> **Companion to:** `OUTLINE.md` (the approved what and why)
> **This file is:** the how — ordered, addressable work with hard engineering and orchestrator gates

## How to use this plan

Work from top to bottom and execute only approved blocks. Blocks use `Phase.Task.Block` addresses, such as `2.3.1`. Every phase produces a runnable Omanotes build. A phase is incomplete until both its automated checkpoint and Matt's hands-on acceptance pass.

The plan is a living document. Completed boxes record evidence; scope changes preserve existing addresses and enter the Change Log. The working title may change without renumbering blocks.

## Agent Delegation Protocol

When handed one or more block addresses:

1. Read `OUTLINE.md`, this protocol, the relevant phase header, and the complete task containing the block.
2. Execute only the named blocks. Do not begin later blocks because they look adjacent or entertaining.
3. Respect every **Don't touch** boundary.
4. Ask before adding or upgrading any dependency, changing a declared public contract, editing configuration/schema formats, or modifying files outside the named task.
5. Never commit secrets, suppress a warning to make a gate green, delete or skip a failing test, edit vendored code, or copy third-party source without licence review.
6. Use Herdr only as behavioural inspiration for session persistence. Re-engineer Omanotes' C++ implementation; do not translate or paste its Rust source.
7. Report completion by block address with the exact commands run and their actual results. “Done” is an emotion, not evidence.
8. Stop at every phase checkpoint. Matt must run and accept the build before any block in the next phase begins.
9. If blocked, report the address, cause, evidence, and smallest decision needed. Do not improvise around the gate.
10. Work on a named branch and open a pull request for Matt's review. Never commit or push feature work directly to `main`, merge an agent-authored pull request without explicit approval, or rewrite a shared branch without approval.

## Build-wide quality contract

Every phase runs the checks available at that point. The canonical developer preset should eventually make these equivalent to one `check` target:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target clang-tidy
cmake --build --preset dev --target security-check
```

Development builds enable AddressSanitizer and UndefinedBehaviorSanitizer where compatible with the target Omarchy toolchain. Release builds must not weaken path, state-file, or Markdown safety checks.

## Project Map

```text
omanotes/
├── CMakeLists.txt                 Root build and quality targets
├── CMakePresets.json              Reproducible dev/release/sanitizer presets
├── OUTLINE.md                     Approved product authority
├── PLAN.md                        Addressable build authority
├── README.md                      User and contributor guide
├── cmake/                         Warning, sanitizer, analysis helpers
├── src/
│   ├── main.cpp                   Process entry point only
│   ├── app/                       Startup, lifecycle, CLI resolution
│   ├── core/                      Buffer and command domain models
│   ├── editor/                    KTextEditor adapter and Vi integration
│   ├── workspace/                 Root policy, tree, search, file watching
│   ├── persistence/               Atomic saves and dirty recovery
│   ├── session/                   Versioned snapshot capture and restore
│   └── ui/                        Window, sidebar, tabs, help, reading view
├── tests/
│   ├── unit/                      Pure logic tests
│   ├── integration/               Filesystem and state tests
│   └── fixtures/                  Versioned, non-sensitive test data
├── packaging/
│   ├── arch/                      PKGBUILD and packaging notes
│   └── linux/                     Desktop entry, icon, AppStream metadata
├── scripts/                       Reproducible developer checks
└── docs/                          Architecture, security, testing, decisions
```

```mermaid
flowchart LR
    CLI["Launch arguments"] --> APP["Application controller"]
    APP --> WS["Workspace service"]
    APP --> BUF["Buffer registry"]
    BUF --> EDIT["KTextEditor adapter"]
    BUF --> READ["Markdown reading view"]
    WS --> DISK["Plain Markdown files"]
    APP --> SNAP["Session snapshot"]
    SNAP --> STATE["XDG state directory"]
```

## Phase 1: Runnable Walking Skeleton

**Phase goal:** Launch a native Omanotes window on Omarchy showing the agreed sidebar, buffer strip, blank editor pane, and status area.

**User-visible result:** `omanotes` opens a real window that can be resized and closed cleanly. The controls are skeletal but visually arranged correctly.

**Phase constraint:** No real file loading, KTextEditor integration, configuration parser, session restore, or speculative service layer.

### Task 1.1: Pin the supported development baseline

**Files:** `docs/development-baseline.md`, `docs/decisions/0001-platform-and-toolkit.md`, `docs/decisions/0002-distribution-boundary.md`

**What it does:**

1. Record the current Omarchy, Qt 6, KF6/KTextEditor, compiler, CMake, Ninja, and sanitizer versions.
2. Confirm the minimum packages needed to compile a blank KTextEditor host.
3. Record C++20, Qt Widgets, CMake, and KTextEditor as the initial architecture decision.
4. Document dependency approval and upgrade policy.

**Don't touch:** Source files, package installation, CI services.

**Blocks:**

- [x] **1.1.1** — Capture the current target-machine versions and proposed package list without installing anything.
- [x] **1.1.2** — Present any missing packages to Matt and obtain explicit installation approval.
- [x] **1.1.3** — Record the approved baseline and architecture decision.
- [x] **1.1.4** — Verify: every named dependency resolves from the current Arch repositories and no unapproved package was installed.
- [x] **1.1.5** — Record the approved native-package and optional companion-plugin distribution boundary.

### Task 1.2: Create the minimal build and quality harness

**Files:** `CMakeLists.txt`, `CMakePresets.json`, `cmake/Warnings.cmake`, `cmake/Sanitizers.cmake`, `.clang-format`, `.clang-tidy`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Skeleton:**

```cpp
// src/main.cpp
int main(int argc, char* argv[]);
```

**Contract:** Start `QApplication`, construct the application window, and return the Qt event-loop status. Fatal startup errors must be reported without exposing environment contents.

**What it does:** Configure strict warnings, debug sanitizers, test discovery, formatting checks, static analysis, and a single executable target.

**Don't touch:** Application features, packaging, global compiler configuration.

**Blocks:**

- [x] **1.2.1** — Add the minimal CMake targets and reproducible presets.
- [x] **1.2.2** — Add warning, sanitizer, formatter, and static-analysis policy without blanket suppressions.
- [x] **1.2.3** — Add one process-startup test suitable for headless execution.
- [x] **1.2.4** — Verify: configure, build, test, format-check, and clang-tidy all pass from a clean build directory.

### Task 1.3: Build the static application shell

**Files:** `src/main.cpp`, `src/ui/main_window.hpp`, `src/ui/main_window.cpp`, `tests/unit/main_window_test.cpp`

**Skeleton:**

```cpp
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};
```

**Contract:** Construct the agreed four-region layout with stable object names for testing. Construction performs no filesystem access and does not create user state.

**What it does:** Assemble a `QSplitter` sidebar and main area, a buffer strip, placeholder editing surface, and restrained status area. Use Qt layout behaviour rather than fixed pixel geometry.

**Don't touch:** Filesystem, KTextEditor, themes beyond neutral placeholder styling.

**Blocks:**

- [x] **1.3.1** — Implement the minimal window hierarchy and accessible labels.
- [x] **1.3.2** — Add structural UI tests for regions, default focus, resize, and clean close.
- [x] **1.3.3** — Add `--smoke-test` to construct and close the window deterministically in automation.
- [x] **1.3.4** — Verify: the smoke test exits zero under the target Wayland/XDG test environment.

### Phase 1 Checkpoint

- [x] Configure/build/test/static-analysis/sanitizer gates pass with recorded output.
- [x] `./build/dev/src/omanotes` opens the basic layout on Omarchy.
- [x] No files appear in the workspace or XDG state/config directories.
- [x] **Matt gate:** run it, resize it, inspect the four regions, and approve or request layout changes.
- [x] Do not begin Phase 2 without explicit approval.

## Phase 2: KTextEditor and the Vim Feel Gate

**Phase goal:** Prove that KTextEditor can deliver the required modal editing feel before deeper architecture depends on it.

**User-visible result:** The blank pane becomes a working Markdown editor with visible mode state and mouse support.

**Phase constraint:** One scratch document only; no real workspace navigation or saving.

### Task 2.1: Isolate the editor behind a narrow adapter

**Files:** `src/editor/editor_adapter.hpp`, `src/editor/ktext_editor_adapter.hpp`, `src/editor/ktext_editor_adapter.cpp`, `src/ui/main_window.*`

**Skeleton:**

```cpp
class EditorAdapter {
public:
    virtual ~EditorAdapter() = default;
    virtual QWidget* widget() noexcept = 0;
    virtual QString text() const = 0;
    virtual void setText(const QString& text) = 0;
    virtual bool isModified() const noexcept = 0;
    virtual QString modeName() const = 0;
};
```

**Contract:** Expose only the document operations Omanotes needs. Ownership and lifetime must be explicit; adapter destruction must not leak a KTextEditor document or view.

**Don't touch:** Buffer registry, file persistence, application commands.

**Blocks:**

- [x] **2.1.1** — Host a KTextEditor document/view in Vi input mode through the adapter.
- [x] **2.1.2** — Configure Markdown highlighting, soft wrap, restrained chrome, and mode reporting.
- [x] **2.1.3** — Add lifetime, text round-trip, modified-state, and mode-transition tests.
- [x] **2.1.4** — Verify: sanitizer run reports no lifecycle errors while repeatedly creating and destroying the editor.

### Task 2.2: Define and run the Vim acceptance suite

**Files:** `docs/vim-acceptance.md`, `tests/integration/vim_behaviour_test.cpp`

**What it does:** Test representative motions, counts, operators, text objects, visual character/line/block modes, registers, paste, undo/redo, dot-repeat, search, marks, command mode, mouse selection, and configurable mappings.

**Don't touch:** KTextEditor internals or vendored patches.

**Blocks:**

- [x] **2.2.1** — Agree on the exact must-pass command matrix from Matt's daily Neovim habits.
- [x] **2.2.2** — Automate stable cases and document manual-only cases.
- [x] **2.2.3** — Record deviations as accept, configure around, or blocker; never quietly redefine Vim.
- [x] **2.2.4** — Verify: all mandatory automated cases pass and manual results are recorded.

### Task 2.3: Resolve the application leader

**Files:** `docs/decisions/0003-application-leader.md`, `src/app/prefix_router.*`

**Skeleton:**

```cpp
class PrefixRouter final : public QObject {
public:
    explicit PrefixRouter(QObject* parent = nullptr);
    bool route(QKeyEvent& event, EditorMode mode);
};
```

**Contract:** Recognise an application command prefix without swallowing ordinary text or canonical mandatory Vim commands. Unknown sequences must cancel safely and provide visible feedback.

**Don't touch:** User configuration format, full command implementation.

**Blocks:**

- [x] **2.3.1** — Prototype Space and `Ctrl+B` routing against the Vim acceptance suite.
- [x] **2.3.2** — Select the default with Matt; preserve `Ctrl+B` page-backward unless explicitly rejected.
- [x] **2.3.3** — Display pending prefix and invalid-sequence feedback in the status area.
- [x] **2.3.4** — Verify: typing, Insert mode, Normal mode, and mouse workflows show no swallowed input.

### Phase 2 Checkpoint

- [x] Full Phase 1 quality gates remain green.
- [x] Vim acceptance results contain no unexplained failures.
- [x] The leader decision is recorded, not merely encoded.
- [x] **Matt gate:** write and edit a substantial Markdown sample using real daily motions; approve the feel.
- [x] Editor feel accepted; no engine reassessment required before Phase 3.

## Phase 3: Workspace Roots and File Navigation

**Phase goal:** Implement the launch contract and safe Markdown-focused filesystem navigation.

**User-visible result:** Launch from a directory or file, navigate the sidebar, and open Markdown documents.

**Phase constraint:** Opened files are read-only from Omanotes' persistence perspective; saving waits for Phase 4.

### Task 3.1: Resolve launch context safely

**Files:** `src/app/launch_request.*`, `src/workspace/workspace_root.*`, `tests/unit/launch_request_test.cpp`

**Skeleton:**

```cpp
struct LaunchRequest {
    std::filesystem::path root;
    std::optional<std::filesystem::path> requestedFile;
    bool bypassRestore{false};
};

[[nodiscard]] std::expected<LaunchRequest, LaunchError>
resolveLaunchRequest(std::span<const std::string_view> arguments,
                     const std::filesystem::path& currentDirectory);
```

**Contract:** Implement the approved relative/absolute launch table, reject invalid roots, and never perform writes. Returned paths are normalized under a documented symlink policy.

**Don't touch:** UI, session loading, file content.

**Blocks:**

- [x] **3.1.1** — Implement and test every approved launch-table case plus missing, unreadable, and traversal cases.
- [x] **3.1.2** — Add `--fresh` as a parsed restore-bypass flag without session deletion.
- [x] **3.1.3** — Pass the resolved request into the window title and workspace controller.
- [x] **3.1.4** — Verify: table-driven tests pass under temporary directories and symlink fixtures.

### Task 3.2: Populate the Markdown-focused sidebar

**Files:** `src/workspace/file_tree_model.*`, `src/ui/sidebar.*`, `tests/unit/file_tree_model_test.cpp`

**Skeleton:**

```cpp
class FileTreeModel final : public QAbstractItemModel {
public:
    explicit FileTreeModel(std::filesystem::path root, QObject* parent = nullptr);
    void setShowAllFiles(bool enabled);
    [[nodiscard]] std::filesystem::path pathForIndex(const QModelIndex& index) const;
};
```

**Contract:** Show directories and Markdown files by default, optionally show all files, avoid following directory cycles, and never expose entries outside the resolved root.

**Don't touch:** File mutation, fuzzy search, session persistence.

**Blocks:**

- [x] **3.2.1** — Implement lazy, cycle-safe tree enumeration and filtering.
- [x] **3.2.2** — Add mouse and `j/k/h/l/Enter` sidebar navigation.
- [x] **3.2.3** — Connect file activation to read-only editor loading with clear error messages.
- [x] **3.2.4** — Verify: hidden, permission-denied, symlink-loop, non-UTF-8-name, and large-directory fixtures behave safely.

### Phase 3 Checkpoint

- [x] Launch-contract and root-boundary tests pass under sanitizers.
- [x] Sidebar never escapes the root through `..` or symlinks.
- [x] The application remains runnable with an empty, missing-file, and populated workspace.
- [x] **Matt gate:** launch using every command form, navigate with keys and mouse, and approve root/sidebar behaviour.

## Phase 4: Buffers, Saving, and External Changes

**Phase goal:** Make ordinary Markdown editing dependable.

**User-visible result:** Scratch and file-backed buffers, tabs, safe saving, modified indicators, and external-change protection.

**Phase constraint:** No session restoration or reading view.

### Task 4.1: Introduce the buffer registry

**Files:** `src/core/buffer.*`, `src/core/buffer_registry.*`, `src/ui/buffer_strip.*`

**Skeleton:**

```cpp
using BufferId = QUuid;

struct BufferState {
    BufferId id;
    std::optional<std::filesystem::path> path;
    QString displayName;
    bool modified{false};
};

class BufferRegistry final {
public:
    BufferId createScratch();
    std::expected<BufferId, BufferError> open(const std::filesystem::path& path);
    bool activate(BufferId id);
    std::expected<void, BufferError> close(BufferId id);
};
```

**Contract:** Preserve stable buffer identity and order, deduplicate canonical file identities, and refuse silent closure of modified buffers.

**Don't touch:** Disk writing implementation, session serialization.

**Blocks:**

- [x] **4.1.1** — Implement scratch/file buffer identity, ordering, activation, and guarded close.
- [x] **4.1.2** — Bind the registry to tabs and editor documents without duplicating text ownership.
- [x] **4.1.3** — Implement approved keyboard and mouse buffer switching.
- [x] **4.1.4** — Verify: unit tests cover duplicate opens, modified close, missing active buffer, and final-buffer scratch fallback.

### Task 4.2: Implement atomic saves

**Files:** `src/persistence/atomic_file_writer.*`, `src/persistence/document_store.*`, `tests/integration/atomic_save_test.cpp`

**Skeleton:**

```cpp
class AtomicFileWriter final {
public:
    [[nodiscard]] std::expected<void, SaveError>
    write(const std::filesystem::path& target,
          QByteArrayView contents,
          const WorkspaceRoot& root) const;
};
```

**Contract:** Write a same-directory temporary file, flush as required by the documented durability level, preserve the original on failure, atomically replace the target, clean temporary artefacts, and reject targets outside the root.

**Don't touch:** Session files, arbitrary backup rotation, shell commands.

**Blocks:**

- [x] **4.2.1** — Implement save/save-as through the root policy using the temp-file-and-rename pattern.
- [x] **4.2.2** — Test permission failures, disk-write interruption simulation, symlinks, existing targets, and cleanup.
- [x] **4.2.3** — Connect scratch naming and file saves to the buffer registry.
- [x] **4.2.4** — Verify: failure injection never corrupts or truncates the original file.

### Task 4.3: Detect external changes and conflicts

**Files:** `src/workspace/file_watcher.*`, `src/persistence/conflict_detector.*`

**Skeleton:**

```cpp
enum class ExternalChangeAction { ReloadClean, PromptConflict, FileRemoved };

[[nodiscard]] ExternalChangeAction classifyExternalChange(
    const SavedRevision& known,
    const DiskRevision& current,
    bool bufferModified);
```

**Contract:** Reload clean buffers, preserve dirty buffers, and require an explicit decision before replacing either version. File hashes supplement timestamps where ambiguity matters.

**Don't touch:** Automatic merge, session restore.

**Blocks:**

- [x] **4.3.1** — Implement revision tracking and watcher event coalescing.
- [x] **4.3.2** — Add non-destructive reload/conflict/removed-file flows.
- [x] **4.3.3** — Test agent-like rapid writes, rename-replace saves, and simultaneous local edits.
- [x] **4.3.4** — Verify: no conflict path silently overwrites editor or disk content.

### Phase 4 Checkpoint

- [x] All save and conflict tests pass with failure injection and sanitizers.
- [x] Scratch buffers are never created on disk without an explicit save target.
- [x] An external agent edit appears safely in a clean buffer and prompts against a dirty one.
- [x] **Matt gate:** edit several notes, switch buffers, save, provoke external changes, and approve the safety/flow.

## Phase 5: Application Command Language, Find, and Help

**Phase goal:** Make the entire application keyboard-complete and discoverable.

**User-visible result:** Leader commands, pane navigation, fuzzy file finding, text search, and contextual Herdr-style help.

**Phase constraint:** No arbitrary user commands or executable bindings.

### Task 5.1: Centralise named application commands

**Files:** `src/core/command.*`, `src/core/command_registry.*`, `src/app/prefix_router.*`

**Skeleton:**

```cpp
struct CommandDescriptor {
    QString id;
    QString label;
    QString category;
    std::function<bool(const AppContext&)> enabled;
    std::function<void(AppContext&)> execute;
};
```

**Contract:** Each action has one stable identifier and one execution path shared by keyboard, mouse, and menus. Unknown or disabled commands never execute.

**Don't touch:** Shell execution, plugin system, configuration parsing.

**Blocks:**

- [x] **5.1.1** — Register tree, buffer, pane, view, file, and help commands.
- [x] **5.1.2** — Route leader sequences and clickable controls to the same descriptors.
- [x] **5.1.3** — Test every focus context, cancellation path, and disabled command.
- [x] **5.1.4** — Verify: a command audit reports no duplicate action implementations.

### Task 5.2: Add fuzzy file and text search

**Files:** `src/workspace/file_index.*`, `src/workspace/text_search.*`, `src/ui/search_palette.*`

**Skeleton:**

```cpp
class WorkspaceSearch final {
public:
    std::vector<FileMatch> findFiles(QStringView query) const;
    std::expected<std::vector<TextMatch>, SearchError>
    findText(QStringView query, const SearchOptions& options) const;
};
```

**Contract:** Search remains root-scoped, cancellable, bounded, and responsive. Binary/oversized files are skipped by documented policy; result ordering is deterministic.

**Don't touch:** Network, external search process until explicitly approved, persistent index database.

**Blocks:**

- [x] **5.2.1** — Implement tested in-process file scoring and bounded Markdown text search.
- [x] **5.2.2** — Add keyboard/mouse palette navigation and result preview.
- [x] **5.2.3** — Add cancellation and performance fixtures for large workspaces.
- [x] **5.2.4** — Verify: root escapes, binary content, pathological lines, and cancellation pass.

### Task 5.3: Build contextual help and safe key configuration

**Files:** `src/ui/help_overlay.*`, `src/app/keymap.*`, `docs/configuration.md`

**Skeleton:**

```cpp
class Keymap final {
public:
    static std::expected<Keymap, KeymapError> fromConfig(const QVariantMap& values);
    [[nodiscard]] QKeySequence sequenceFor(QStringView commandId) const;
};
```

**Contract:** Accept only known command identifiers and valid key sequences. Invalid configuration reports actionable locations and falls back without executing arbitrary content.

**Don't touch:** General settings format beyond key bindings, hot-reload unless separately approved.

**Blocks:**

- [x] **5.3.1** — Render current-context commands from the command registry.
- [x] **5.3.2** — Add partial key overrides using the approved XDG config location.
- [x] **5.3.3** — Test duplicate, unreachable, invalid, and editor-conflicting mappings.
- [x] **5.3.4** — Verify: `Leader+?` remains reachable and the app starts safely with malformed config.

### Phase 5 Checkpoint

- [x] Every core workflow has a tested keyboard path and a mouse path where visible.
- [x] Search remains responsive and root-scoped under stress fixtures.
- [x] Configuration cannot invoke arbitrary programs.
- [x] **Matt gate:** operate for one session without the mouse, then repeat key actions with the mouse and assess the help overlay.

Engineering evidence and the remaining hands-on checklist are in
`docs/phase-5-keymap-review.md`. Matt's first gate session passed the keyboard
paths and help overlay but found no mouse route for opening or closing a
buffer; the fix and its evidence are in `docs/phase-5-mouse-review.md`.
P6 awaits this acceptance gate.

## Phase 6: Markdown Reading View and Omarchy Appearance

**Phase goal:** Deliver the polished graphical writing experience that distinguishes Omanotes from Neovim.

**User-visible result:** `Leader+m` toggles between styled source and a secure reading view; the app follows Omarchy appearance and text scaling.

**Phase constraint:** No WYSIWYG editing, embedded browser, remote content, or theme marketplace.

### Task 6.1: Render Markdown without changing source

**Files:** `src/ui/markdown_view.*`, `src/core/view_mode.*`, `tests/integration/markdown_render_test.cpp`

**Skeleton:**

```cpp
class MarkdownView final : public QTextBrowser {
public:
    explicit MarkdownView(QWidget* parent = nullptr);
    void render(QStringView markdown, const ResourcePolicy& policy);
};
```

**Contract:** Render supported Markdown deterministically, block remote fetching, constrain local resources to policy-approved paths, and never mutate the editor document.

**Don't touch:** HTML execution, JavaScript, WYSIWYG edits.

**Blocks:**

- [x] **6.1.1** — Define supported Markdown and resource policy with malicious fixtures.
- [x] **6.1.2** — Implement writing/reading toggle while preserving cursor and buffer state.
- [x] **6.1.3** — Style typography, headings, lists, quotes, links, and code for restrained reading.
- [x] **6.1.4** — Verify: hostile HTML, remote images, traversal URLs, and malformed Markdown cause no execution or out-of-root reads.

### Task 6.2: Integrate Omarchy theme and scaling

**Files:** `src/ui/theme_adapter.*`, `docs/omarchy-integration.md`

**Skeleton:**

```cpp
class ThemeAdapter final : public QObject {
public:
    ThemePalette currentPalette() const;
    void refresh();
signals:
    void paletteChanged(const ThemePalette& palette);
};
```

**Contract:** Derive a complete safe palette from supported Omarchy/system sources, fall back to readable Qt colours, and apply changes without altering user theme files.

**Don't touch:** Omarchy-owned configuration, theme activation, private hard-coded machine paths.

**Blocks:**

- [x] **6.2.1** — Document supported current-theme and text-scale inputs on Quattro.
- [x] **6.2.2** — Apply semantic colours consistently to window, editor, sidebar, status, and reading view.
  The focused pane must be unmistakable: after `Ctrl+L` from the tree, the sidebar's current-row
  highlight stays as strong as when the tree had focus, and nothing says the editor is live
  (found at Matt's Task 5.1 gate, 2026-09-04). Dim the inactive selection and mark the active pane.
- [x] **6.2.3** — Handle live theme/text-scale changes or document the minimal restart boundary.
- [x] **6.2.4** — Verify: representative dark/light themes retain contrast, focus visibility, and readable selection colours.

### Phase 6 Checkpoint

- [x] Markdown security fixtures and theme tests pass.
- [x] Writing/reading toggles never alter source or modified state.
- [x] The app looks coherent across approved Omarchy themes and scale settings.
- [x] **Matt gate:** write, read, switch themes/scales, inspect links/images, and approve the visual character.

## Phase 7: Herdr-inspired Session Snapshot and Recovery

**Phase goal:** Reopen Omanotes exactly where the user left it while keeping unsaved work safe.

**User-visible result:** Close and reopen to recover root, window, sidebar, buffers, focus, cursor/scroll positions, view modes, and dirty scratch/file contents.

**Phase constraint:** Restore application/document state only; do not relaunch processes or store workspace content in the structural snapshot.

### Task 7.1: Define a versioned structural snapshot

**Files:** `src/session/session_snapshot.*`, `docs/session-format.md`, `tests/fixtures/session/`

**Skeleton:**

```cpp
inline constexpr std::uint32_t kSessionFormatVersion = 1;

struct SessionSnapshot {
    std::uint32_t version{kSessionFormatVersion};
    std::filesystem::path workspaceRoot;
    WindowSnapshot window;
    SidebarSnapshot sidebar;
    std::vector<BufferSnapshot> buffers;
    std::optional<BufferId> activeBuffer;
};
```

**Contract:** Represent only structural state and references to separately protected recovery records. Parsing is bounded, validates all fields, rejects newer incompatible versions, and defaults explicitly supported older omissions.

**What it does:** Apply Herdr's proven ideas—version field, structural snapshot, compatible restore, and round-trip fixtures—using independent C++ design.

**Don't touch:** Unsaved text storage, workspace Markdown, Herdr source copying.

**Blocks:**

- [x] **7.1.1** — Specify fields, limits, version policy, XDG paths, and privacy boundaries.
- [x] **7.1.2** — Implement serialization/parsing with round-trip, corrupt, oversized, missing-field, old-version, and future-version fixtures.
- [x] **7.1.3** — Validate every restored path against the resolved workspace root.
- [x] **7.1.4** — Verify: invalid state yields a clean launch and a diagnostic without modifying the bad file.

### Task 7.2: Write snapshots atomically and privately

**Files:** `src/session/session_store.*`, `tests/integration/session_store_test.cpp`

**Skeleton:**

```cpp
class SessionStore final {
public:
    std::expected<void, SessionError> save(const SessionSnapshot& snapshot) const;
    std::expected<std::optional<SessionSnapshot>, SessionError> load(
        const WorkspaceRoot& root) const;
};
```

**Contract:** Store versioned state beneath the XDG state directory using user-only permissions and temp-file-and-rename replacement. A failed save preserves the previous valid snapshot.

**Don't touch:** User config, workspace files, cloud storage.

**Blocks:**

- [x] **7.2.1** — Implement per-workspace state identity without exposing raw path names unnecessarily.
- [x] **7.2.2** — Implement atomic write, permission enforcement, and stale-temp cleanup.
- [x] **7.2.3** — Add failure injection for partial write, rename failure, full disk, corrupt prior state, and concurrent launch.
- [x] **7.2.4** — Verify: the previous valid session always survives an interrupted replacement.

### Task 7.3: Protect dirty-buffer recovery separately

**Files:** `src/persistence/recovery_store.*`, `tests/integration/recovery_store_test.cpp`

**Skeleton:**

```cpp
class RecoveryStore final {
public:
    std::expected<RecoveryId, RecoveryError> checkpoint(const BufferRecovery& state);
    std::expected<std::optional<BufferRecovery>, RecoveryError> load(RecoveryId id) const;
    std::expected<void, RecoveryError> remove(RecoveryId id) const;
};
```

**Contract:** Persist only modified/scratch contents needed for recovery, with user-only permissions, bounded sizes, atomic replacement, and no silent write into the workspace during restore.

**Don't touch:** Saved-file duplication, encryption claims not actually implemented.

**Blocks:**

- [x] **7.3.1** — Define checkpoint cadence, retention, cleanup, and sensitive-content documentation.
- [x] **7.3.2** — Implement dirty recovery records referenced from the structural snapshot.
- [x] **7.3.3** — Restore dirty buffers as dirty and require explicit save targets where appropriate.
- [x] **7.3.4** — Verify: crash simulation restores text; normal save and discarded buffers clean obsolete recovery records.

### Task 7.4: Restore the full working context

**Files:** `src/app/application_controller.*`, `src/session/session_restorer.*`

**Skeleton:**

```cpp
class SessionRestorer final {
public:
    RestoreReport restore(const SessionSnapshot& snapshot,
                          BufferRegistry& buffers,
                          MainWindow& window) const;
};
```

**Contract:** Restore valid state best-effort, report skipped items, focus the requested launch file last, and never let one missing file prevent the rest of the session from opening.

**Don't touch:** Automatic file creation or deletion, future format migration beyond approved fixtures.

**Blocks:**

- [x] **7.4.1** — Capture final snapshot on clean close and debounced checkpoints during meaningful state changes.
- [x] **7.4.2** — Restore session before presenting the final window; honour `--fresh` without deleting state.
- [x] **7.4.3** — Apply explicit file arguments after restore so they open/focus predictably.
  - Note (2026-09-09): once the editor adapter exposes cursor and scroll position for the
    snapshot, use the same accessor so `Ctrl+M` lands the reading view near the editor cursor
    and switching back returns there (Obsidian behaviour). Matt approved; bug fix that opens
    reading at the top shipped separately on `fix/reading-view-scroll-top`.
- [x] **7.4.4** — Verify: clean close, forced kill, missing files, changed roots, corrupt state, and concurrent-launch scenarios match the documented matrix.

### Phase 7 Checkpoint

- [x] Snapshot, recovery, migration, permissions, failure-injection, and sanitizer tests pass.
- [x] Structural state contains no note contents; dirty contents exist only in protected recovery records.
- [x] `--fresh` bypasses but does not destroy the last session.
- [x] **Matt gate:** arrange several buffers and sidebar state, leave dirty work, close/reopen, force-kill/reopen, and confirm it feels like returning to the same desk.

## Phase 8: Hardening, Packaging, and First Usable Release

**Phase goal:** Turn the accepted application into an auditable Omarchy package suitable for daily use and eventual publication.

**User-visible result:** Install, launch from Omarchy, use daily, uninstall cleanly, and inspect documented architecture/security decisions.

**Phase constraint:** No new product features. Findings may fix existing behaviour but cannot smuggle in scope.

### Task 8.1: Conduct the release security and reliability audit

**Files:** `docs/security.md`, `docs/threat-model.md`, `scripts/security-check.sh`, tests as findings require

**What it does:** Audit path boundaries, symlinks, temporary files, state permissions, Markdown resources, config parsing, external edits, concurrent instances, denial-of-service limits, logs, and dependency advisories.

**Don't touch:** Feature scope, remote services, telemetry.

**Blocks:**

- [x] **8.1.1** — Write and review the threat model with assets, trust boundaries, threats, and mitigations. Evidence: `docs/threat-model.md` (Change Log 2026-09-11).
- [ ] **8.1.2** — Run compiler hardening, sanitizers, static analysis, dependency review, and fuzz/property tests for parsers where justified.
- [ ] **8.1.3** — Resolve every release-blocking finding or record Matt's explicit deferral with impact.
- [ ] **8.1.4** — Verify: clean release and hardened debug builds pass the complete test matrix.

### Task 8.2: Package for Omarchy evaluation

**Files:** `packaging/arch/PKGBUILD`, `packaging/linux/*.desktop`, `packaging/linux/*.metainfo.xml`, icons, `docs/packaging.md`

**What it does:** Package only built artefacts and declared runtime dependencies, integrate XDG desktop metadata, and provide reproducible install/uninstall instructions.

**Don't touch:** System config, user themes, automatic repository publication.

**Blocks:**

- [ ] **8.2.1** — Revalidate the Omarchy Package Repository/AUR route, companion-plugin marketplace contract, and naming availability before release work.
- [ ] **8.2.2** — Build a clean Arch package in an isolated packaging environment.
- [ ] **8.2.3** — Install, launch, upgrade, and uninstall on the target Omarchy system without orphaning user-authored notes.
- [ ] **8.2.4** — Verify: package metadata, dependency list, file ownership, desktop launch, CLI launch, and removal checks pass.

### Task 8.3: Explain the system to its orchestrator and contributors

**Files:** `README.md`, `docs/architecture.md`, `docs/testing.md`, `docs/configuration.md`, `docs/decisions/`

**What it does:** Explain the architecture, data ownership, editor boundary, launch model, session/recovery distinction, test commands, security posture, and how to delegate plan blocks.

**Don't touch:** Marketing claims, unsupported platforms, unverified benchmarks.

**Blocks:**

- [ ] **8.3.1** — Document user installation, key language, launch contract, recovery, and troubleshooting.
- [ ] **8.3.2** — Document the component/data flow for design-level understanding without requiring C++ fluency.
- [ ] **8.3.3** — Record remaining limitations, licence, name status, and publication checklist.
- [ ] **8.3.4** — Verify: a clean checkout follows the documented build/test/package path without tribal knowledge.

### Phase 8 Checkpoint

- [ ] Complete clean-room build, test, hardening, and package evidence is attached to the phase report.
- [ ] Install/upgrade/uninstall preserve notes and handle XDG state as documented.
- [ ] Architecture and security documents match the shipped code.
- [ ] **Matt gate:** use the packaged build for an agreed daily-use trial and approve any public-release step separately.
- [ ] No direct push to `main`, release, public-repository change, or plugin-board submission occurs without explicit approval. Approved work may be pushed to a review branch solely to open its pull request.

## Quick Reference: Phase Boundaries

| Phase | In scope | Explicitly out of scope |
| --- | --- | --- |
| 1 | Build harness and static runnable layout | Real editor, files, sessions |
| 2 | KTextEditor Vi feel and leader decision | Saving, workspace features |
| 3 | Launch roots, sidebar, read-only opens | File writes, sessions |
| 4 | Buffers, atomic save, conflicts | Reading view, restore |
| 5 | Commands, complete navigation, find/help | Shell commands, plugins |
| 6 | Secure reading view and Omarchy appearance | WYSIWYG, remote content |
| 7 | Versioned snapshot and dirty recovery | Process resurrection, cloud sync |
| 8 | Audit, docs, Arch/Omarchy package | New features or publication without approval |

## Orchestrator Review Template

At each phase gate, the implementing agent must provide:

```text
Phase:
Blocks completed:
Files changed:
Dependencies added or changed:
Commands run and actual results:
Security/reliability checks:
Known limitations:
How Matt can run it:
What to inspect manually:
Decision required: approve / request changes / stop and redesign
```

## Change Log

- **2026-08-28:** Initial plan created from approved `OUTLINE.md`; session snapshot/restore added as a core Phase 7 capability inspired by Herdr's structural persistence pattern.
- **2026-08-29:** Native application distribution fixed as Arch/AUR/Omarchy Package Repository; `omarchyplugins.com` is reserved for an optional, separate QML companion plugin with meaningful shell integration.
- **2026-08-29:** Phase 1 manually accepted after verifying typing, window resizing, and sidebar resizing; Phase 2 remains separately approval-gated.
- **2026-08-29:** Adopted a branch-and-pull-request workflow: agents may push approved work to review branches, but Matt reviews and controls merging into `main`.
- **2026-08-29:** Selected Space as the application leader after the Phase 2 prototype; preserved `Ctrl+B` page-backward and renumbered the leader decision to ADR 0003 because ADR 0002 already exists.
- **2026-08-29:** Phase 2 accepted after the real-session Vim feel and Space-leader smoke tests; fixed and regression-tested shifted `Space+?` routing before closing the phase.
- **2026-09-02:** Phase 3 sidebar policy moved to two isolated sibling experiments from the accepted Phase 2 tip: Markdown-only first, then general-purpose within KTextEditor capabilities. No Neovim feature reimplementation is part of either experiment.
- **2026-09-02:** Both Phase 3 sidebar experiments passed; continued development selected the Markdown-only branch for product focus, while retaining the general-purpose branch as a parked alternative.
- **2026-09-03:** Published the parked general-purpose sidebar experiment to `origin` as branch
  `experiment/phase-3-general-purpose` and immutable tag `parked/phase-3-general-purpose-sidebar`;
  it had existed only as an unpushed local branch. Added a second development machine and
  re-validated the Phase 1-3 gates on it.
- **2026-09-03:** Task 4.1 introduced the buffer registry, buffer strip, and per-buffer
  editor documents. Buffer switching follows Matt's LazyVim habit (`Shift+H`/`Shift+L`) and is
  recorded as ADR 0004 with its Vim deviation; buffer close bindings were deliberately left to
  the Phase 5 command language.
- **2026-09-03:** Task 4.2 added atomic saves with fsync durability and symlink-preserving writes
  (ADR 0005). Found and closed two input-routing defects: the Space leader was eating spaces typed
  in KTextEditor's own command line and search bar, and KTextEditor's Vi mode implements `:w`
  internally through its own writer, which bypassed the workspace root and the atomic replace.
- **2026-09-04:** Task 4.3 added external-change detection: a directory-and-file watcher with
  event coalescing, content-hash revision tracking, and Vim-shaped conflict handling (`:w!`,
  `:e`, `:e!`, `File exists`), recorded as ADR 0006. The save itself checks the disk, so the
  guarantee does not depend on the watcher. Hashes replace timestamps entirely, and the
  classifier gained an `Unchanged` outcome beyond the plan's skeleton.
  Matt's gate found the `:w` interception missing a route: a bare `:w` keeps the command bar's
  completion popup open and the Return lands there, reaching KTextEditor's own Save As dialog.
  Closed with a regression test that presses Return on the popup.
- **2026-09-04:** Phase 4 accepted after Matt's gate on the rebuilt tip: `:w` in Normal mode and
  `Ctrl+S` in Insert mode both write through the atomic path with no dialog. `Ctrl+S` has been
  the application save shortcut since Task 4.2; it stays hardcoded until Task 5.3's keymap.
- **2026-09-04:** Task 5.1 centralised application commands in a registry with multi-key leader
  sequences following Matt's LazyVim vocabulary (`Space e`, `Space b d`, `Space b D`,
  `Space f n`, ...), recorded as ADR 0007. Buffer close gained its binding, deferred from ADR
  0004. Later features are registered disabled with a hint naming their block. The Vi command
  line's `:w`/`:e` remain intercepted verbs rather than registry commands; the descriptor gained
  `disabledHint` beyond the plan's skeleton. Matt's gate turned `Space e` into a sidebar toggle
  (unplanned; the outline had only listed visibility as Phase 7 session state) and added
  `Space Space` as LazyVim's second find-files route. The gate also found that moving focus
  from the sidebar to the editor leaves no visible sign of which pane is live; deferred to Task
  6.2, where focus visibility is already a requirement. The sidebar now starts hidden, overriding
  the outline's "visible on the left"; Phase 7's session restore will remember its state.

- **2026-09-07:** Task 5.2 implemented bounded in-process file/text search with a cancellable
  worker, deterministic ordering, plain-text previews and keyboard/mouse result navigation.
  Task 5.3.1 added contextual command help from the existing registry and a mouse help button;
  leader routing also works from the sidebar. Search policy is documented in `docs/search.md`.

- **2026-09-07:** Matt's Phase 5 gate session: keyboard paths and the help overlay passed,
  but `buffer.new` and `buffer.close` had no mouse route, so the checkpoint's mouse-path
  claim was premature. Fixed: tab close buttons and a `+` button beside the buffer strip.
  On Matt's hands-on review of the fix, the mouse close of a dirty buffer became a
  Save / Discard / Cancel prompt at his direction — Save on a scratch routes through the
  save-as prompt and closes once named; Discard closes without saving; keyboard
  `Space b d` keeps its status-line guard. His review also caught the sidebar shrinking
  when a long status message appeared: the status label now wraps instead of raising the
  pane's minimum width. Parked idea (unapproved, revisit after Phase 6): shipping
  *default* direct-key bindings (`Ctrl+…`) alongside leader sequences for common
  commands. The keymap already lets a user add one today via `shortcuts` in
  `keymap.json` without losing the leader route, so the open question is only about
  defaults.

- **2026-09-07:** Phase 5 accepted. Matt's gate ran the keyboard-only session, the mouse
  repeat, and the help overlay; the mouse gaps and sidebar squeeze found along the way
  were fixed and merged as PR #12, and the full hand-inspection list passed on the
  rebuilt tip. One observation stays on watch rather than in a fix: a session showed a
  highlight block on every space while typing, which vanished on restart and matched
  KTextEditor vi-mode search highlighting (session-scoped, like nvim's hlsearch) rather
  than any Omanotes configuration. If it recurs: `/zzqx` then `:noh` in Normal mode
  before restarting, which also answers whether the vi emulation honours `:noh`.

- **2026-09-07:** Block 6.1.1: Matt set the reading-view direction (minimal deps, Obsidian
  restraint, vault-model resources, clickable links) and accepted the security amendments,
  recorded as ADR 0009 — Qt-native CommonMark+GFM with raw HTML never parsed, raster-only
  root-scoped images with size caps and no SVG/data:/remote, and a link scheme allowlist
  (http/https via the browser, in-root `.md` links open in-app, all else refused) with
  hover target reveal. The malicious fixture set encoding the policy lives in
  `tests/fixtures/markdown/`; block 6.1.4 verifies against it. Threat model note: agents
  write into the workspace by design, so every note is untrusted input.

- **2026-09-07:** Blocks 6.1.2–6.1.4 implemented the reading view per ADR 0009. `Space m`
  toggles per buffer between the editor and a `QTextBrowser` projection, preserving cursor,
  text, and modified state; the reading view re-renders when a clean buffer reloads from an
  external change. Every image is requested eagerly at render so policy verdicts are not at
  the mercy of lazy layout; refusals show a small placeholder and are recorded per render.
  The fixture suite (`markdown-render`, 12 cases) covers raw HTML as inert text, remote and
  traversal images, the scheme allowlist, plus runtime-generated symlink escapes, size/pixel
  bombs, and pathological text. Typography is deliberately restrained — system face plus one
  point, 130% line height, wide document margin — with colour left to Task 6.2's theme.
  Test-contract updates: help now lists `view.reading` (its mouse route), and editor-stack
  counts include the one permanent reading-view widget.

- **2026-09-07:** Matt's gate on the Phase 6 build found Omarchy's universal paste broken:
  Super+V delivers a literal Ctrl+V (per `default/hypr/bindings/clipboard.lua`), which the
  Phase 2 matrix had released to Vi as visual block, and bare `p` never reads the system
  clipboard in stock Vi (LazyVim's `clipboard=unnamedplus` is why it does in Neovim). Matt
  chose mode-sensitive routing: a new `edit.paste` command defaults to Ctrl+V and acts in
  Insert mode only, pasting via the editor's own paste action; Normal and Visual keep
  visual block, and `"+p`/`"+y` remain the Vim-correct registers. Matt's retest found the
  Super chords still dead: Hyprland's `sendshortcut` injects Ctrl+key while the physical
  Super is held, so the app receives Ctrl+Meta+key, which no widget or keymap shortcut
  matched (the initial claim that Super+C "already worked" was wrong — untested optimism,
  withdrawn). Fixed by ignoring the Meta modifier in application shortcut matching and
  adding `edit.copy` (default Ctrl+C, fires only over a selection, via the editor's own
  copy action; without a selection Ctrl+C stays Vi's abort). Matt then set the product
  rule — Omarchy users expect Super+C/V to work everywhere, always. A Meta-modifier
  discriminator was tried and failed on the real compositor: clipboard.lua's
  send_key_state exists precisely to deliver a clean chord without the held Super, so
  Super+V and Ctrl+V are indistinguishable at the application. Final resolution:
  `Ctrl+V` pastes in every mode (matching what terminals give Neovim via bracketed
  paste), and Vi's visual block relocates to `Ctrl+Q` — gvim's classic answer to this
  collision — implemented by handing Vi a synthetic Ctrl+V. Deviations recorded in
  `docs/vim-acceptance.md`; the 2.2 matrix gap (no system-clipboard interop cases) is
  thereby closed.
  The 15-suite engineering gate passes with ASan/UBSan, formatting, clang-tidy and hardening.
  Tasks 5.3.2–5.3.4 await the configuration-format decision in `docs/keymap-proposal.md`;
  the Phase 5 hands-on gate remains open. The working baseline is merged upstream `b5260c8`,
  which already records P4 acceptance and completion of 5.1.

- **2026-09-08:** Matt merged PRs #15 and #16 (the second refreshed against main with both
  Change Log entries retained); all six gates re-ran clean on the merged tip. The known
  interplay gap between the two — the clipboard chords consulted only the hidden editor
  while the reading view was showing — is closed: `edit.copy` now reads the visible pane's
  selection (the `QTextBrowser`'s when reading), `edit.paste` while reading refuses with a
  status hint instead of silently mutating the hidden source buffer, and `Ctrl+Q` (visual
  block, a writing-mode key) no longer routes while reading. No new commands; the registry
  stays at 18.

- **2026-09-08:** Task 6.2 dispatched. Block 6.2.1 surveyed the target machine: Quattro
  materializes the active theme into `$XDG_STATE_HOME/omarchy/current/theme/` (a real
  directory regenerated on switch, not 3.x's symlink), whose `colors.toml` comes in two
  schemas — semantic (`mode`/`accent`/`selection`/layered grounds) and legacy flat
  (`selection_background`, `color0–15`, no `mode`); text scale is `[font] base-size` in
  `$XDG_CONFIG_HOME/omarchy/shell.toml`. All documented in `docs/omarchy-integration.md`
  with the role mapping. `ThemeAdapter`/`ThemePalette` implement the contract: overlay
  onto a complete readable fallback, a 3.0:1 WCAG contrast guard that refuses an
  unreadable ground/ink pair as a pair, clamped text scale, XDG-resolved paths only, and
  `refresh()` that emits only on real change (the 6.2.3 live-change hook). Read both
  schemas' flat key/value subset directly rather than adding a TOML dependency. New
  `theme-adapter` suite (7 cases); application to the widgets is blocks 6.2.2–6.2.4.

- **2026-09-08:** Block 6.2.2: the window wears the theme. One stylesheet paints the chrome
  (window, sidebar, status, buffer strip, name prompt, splitter handles); the sidebar tree's
  selection goes through QPalette groups so it dims to the derived inactive colour when
  focus leaves it, and both panes carry a permanent 2px top border that turns accent on the
  focused one — the 5.1 gate debt (dim inactive selection, mark the active pane). The
  editor takes the semantic grounds plus a mode-matched Breeze syntax theme via KTextEditor
  config; the reading view takes background/text/link/selection and keeps its one-point-up
  face at the theme's base size. A `ThemeSources` constructor overload lets tests dress the
  window in a fixture theme; two new ui-suite cases assert region colours and the accent
  mark following Space e / Ctrl+L. Live re-application on theme change is wired
  (`paletteChanged` → repaint) but nothing watches the files yet — that decision is 6.2.3.

- **2026-09-08:** Matt's first gate pass on 6.2.2 found the sidebar still flat grey (the
  stylesheet painted the frame, not the QTreeView viewport or heading inside it) and made
  the 6.2.3 call by testing it: switching to catppuccin mid-run must apply live, not on
  restart. Fixed on the same PR: tree and heading joined the stylesheet (selection stays
  in QPalette groups so the inactive dim survives), and a second FileWatcher now watches
  theme.name, colors.toml, and shell.toml — theme.name's parent is the stable `current/`
  directory, so a switch that regenerates the whole theme directory still fires — feeding
  `ThemeAdapter::refresh()`, which repaints only on real change; a visible reading view
  re-renders so document colours follow. Block 6.2.3 resolved as live-change, box ticked;
  a new ui case rewrites the fixture theme and asserts the window follows without restart.

- **2026-09-08:** Matt's third gate pass approved the live behaviour (theme hopping with the
  reading view open, sidebar dim in and out). Block 6.2.4 closed with an automated
  verification instead of eyeballs: representative themes of both schemas and both modes
  (plus the fallback) must keep text, muted text, both selection inks, accent, and link
  above WCAG contrast bars, measured with the exported `contrastRatio`. Writing it caught
  a real bug before Matt could: legacy catppuccin pairs a cream `selection_background`
  with its own `selection_foreground`, which the mapping ignored — light-on-light selected
  rows. New `selectedText`/`inactiveSelectedText` roles are chosen by contrast
  (`readableOn`: theme ink, then text, background, then black/white), the muted blend is
  clamped to the bar, and the stylesheet paints rows with them. Task 6.2 complete;
  Phase 6 checkpoint awaits Matt's gate.

- **2026-09-08:** Phase 6 accepted. After Task 6.2 merged (PR #19), Matt's hands-on gates
  drove a venustas campaign across three further PRs, each finding what the offscreen suite
  structurally cannot see: PR #20 themed the dialogs (help, search, close prompt — separate
  windows the scoped stylesheet rules missed); PR #21 replaced the stock chrome (a quiet ×
  close glyph on tabs, platform-theme icons stripped from every dialog button) and, at
  Matt's "pure Omarchy" call, unified typography — the font census had found three fonts
  and two size systems (GTK's Adwaita Sans 11 on the chrome, fontconfig monospace in the
  editor, Adwaita 13 in the reading view); the whole app now wears the system monospace at
  shell.toml's base-size via a `*` stylesheet rule, scaling live. The recurring lesson of
  the phase, now structural: with a stylesheet active, Qt ignores QPalette for backgrounds
  and item selection and app-wide setFont entirely, so every visual decision lives in the
  stylesheet — the palette survives only where document rendering genuinely reads it.
  Also: window title and Wayland app_id spell OmaNotes (packaging must ship a matching
  OmaNotes.desktop, noted for 8.2). Matt's gate ran the full checkpoint list live across
  ethereal-black, catppuccin, and osaka-jade with the reading view open; all four
  checkpoint boxes ticked. All six engineering gates green on merged main `4b7dac5`.
  Phase 7 (session snapshot and recovery) awaits dispatch.

- **2026-09-07:** Matt reviewed and merged PR #10, then requested cleanup and
  continuation. Fast-forwarded to `eb2f5b2`, removed the merged search/help branch
  and superseded local P4 prototype, and retained the parked general-purpose
  experiment. Tasks 5.3.2–5.3.4 implement the reviewed JSON keymap proposal with
  whole-candidate validation, safe direct shortcuts, effective help labels and
  visible default fallback. ADR 0008 records the format and constraints.
  Phase 5 engineering is complete; Matt's hands-on gate remains open before P6.

- **2026-09-09:** Pre-dispatch review of Phase 7. Matt questioned what happens when the
  app is killed with dirty buffers in one root and next launched in another. Settled as
  ADR 0010: session and recovery state are root-scoped and restored only in their own
  root; recovering into a different root is rejected as a trust-boundary breach; a
  metadata-only status-line notice announces parked work in other roots. Task 7.1.1
  must specify the per-root metadata that notice needs. Task 7.1 not yet dispatched.

- **2026-09-09:** Matt found that on a fresh launch the first `Ctrl+M` landed at the bottom of
  any note long enough to scroll. Cause: `MarkdownView::render` replaced the document under
  the widget's own cursor, pushing it to the end, and the view's deferred first layout
  scrolled to it. Fixed on `fix/reading-view-scroll-top`: a rendered note opens at the top;
  regression test reproduces the hidden-then-shown first render. Cursor-following between
  writing and reading noted under 7.4.3.
- **2026-09-09:** Task 7.1 built on `task/7.1-session-snapshot` (blocks 7.1.1–7.1.4).
  `docs/session-format.md` specifies format version 1: JSON via Qt's parser, structural
  state only, per-root under `$XDG_STATE_HOME/omanotes/sessions/<id>/`, explicit limits
  (256 KiB, 256 buffers, 4096-byte paths), unknown fields refused, version checked first so
  a future document is reported as newer rather than malformed, dirty iff recovery record.
  `session_snapshot.*` implements deterministic serialize, bounded parse, a read that never
  writes, `checkSessionRoot` (ADR 0010) and `resolveSessionPath` through the existing
  `WorkspaceRoot` policy. New `session-snapshot` suite (23 cases) over 15 committed fixtures
  plus generated oversized, symlink-escape and root-mismatch cases. Also gave the
  `theme-adapter` suite the headless test environment it had been missing: without it the
  GTK platform theme leaked under LeakSanitizer and the suite failed on this machine.
  Awaiting Matt's gate on the format document before 7.2.

- **2026-09-09:** Task 7.2 built on `task/7.2-session-store` (blocks 7.2.1–7.2.4). ADR 0011
  fixes the layout (`$XDG_STATE_HOME/omanotes/sessions/<id>/session.json`) and the id
  (first 128 bits of SHA-256 over the canonical root, hex). `SessionStore` saves through
  the Phase 4 note writer's core, extracted as `replaceFileAtomically` with an explicit
  mode and a test-only fault seam; the workspace-facing `AtomicFileWriter::write` is
  unchanged and its suite still passes. Directories 0700 and file 0600 enforced on every
  save; symlinks in the store refused on read and write; temporaries older than 15 min
  removed, younger kept. New `session-store` suite (15 cases): five injected failures
  (short write, ENOSPC, EIO, failed fsync, failed rename) each leave the prior snapshot
  byte-identical with no temporary; corrupt prior state is reported on load and replaced
  on save; four writers and a concurrent reader never observe a partial file. Awaiting
  Matt's gate before 7.3.

- **2026-09-09:** Task 7.3 built on `task/7.3-recovery-store` (blocks 7.3.1–7.3.4).
  `docs/session-format.md` gains a "Recovery records" section: format, id-only lookup inside
  the workspace's own `recovery/` directory (ADR 0010), checkpoint cadence (2 s debounce,
  focus loss, clean close; wired in 7.4), retention (removed on save and discard, orphans
  swept after restore, other workspaces never touched), restore dispositions, and the
  plaintext-like-swap-files statement Matt accepted. `RecoveryStore` checkpoints through
  `replaceFileAtomically` (0700/0600, symlinks refused, 16 MiB cap with a diagnostic);
  `planRecovery` reuses ADR 0006's content hashes to restore a record as dirty scratch,
  dirty file, dirty-in-conflict, or dirty-recreating a missing file, and refuses paths that
  now leave the root. New `recovery-store` suite (15 cases) including a two-process crash
  simulation through the snapshot reference. The UI half of 7.3.3 (opening the restored
  buffer dirty, naming prompt, conflict flag) lands with the restorer in 7.4, whose files
  it lives in. Awaiting Matt's gate.

- **2026-09-09:** Task 7.4 built on `task/7.4-session-restore` (blocks 7.4.1–7.4.4). Phase 7
  becomes visible: `ApplicationController` (new, `src/app/`) restores the last desk for the
  root before the window is shown, brings back unreferenced recovery records as dirty
  buffers, opens or focuses the command-line file last, and shows the ADR 0010 parked-work
  notice from snapshot metadata. It checkpoints dirty buffers and rewrites the snapshot two
  seconds after the desk changes, on window deactivation, and on close; a record is removed
  the moment its buffer is saved, reloaded, or closed. `SessionRestorer` (new,
  `src/session/`) drives a `SessionHost` interface that `MainWindow` implements. `--fresh`
  reads no snapshot and writes none. The editor adapter gained cursor/scroll accessors, the
  sidebar selection round-trips, and `Space m` now opens reading near the editor cursor and
  returns near where the reader stopped (proportional, per the 7.4.3 note). Fixed on the
  way: a latent use-after-free at shutdown when the sidebar tree held focus (the
  application-wide focus hook fired during child destruction). New `session-restore` suite
  (13 scenarios: clean close, forced kill, missing file, other root + notice, corrupt state,
  `--fresh`, requested file last, concurrent launches, orphan record, recovered note
  changed on disk, reading-view cursor following, debounce). Phase 7 checkpoint awaits
  Matt's hands-on gate.

- **2026-09-10:** Phase 7 accepted. PR #28 merged after Matt's hands-on gate on the release
  build: all six manual steps passed (restore of tabs, order, active tab, sidebar, reading
  mode and an unsaved paragraph; `kill -9` recovery; recovery record cleared by `:w`;
  `--fresh` leaving the prior session intact; reading view following the cursor; parked-work
  notice from another root). Same-day evidence on the branch: 22/22 ctest, format-check,
  clang-tidy and security-check clean. Rulings on the five judgement calls flagged in #28:
  orphan recovery records restored as dirty buffers (not swept) — accepted; `[No Name]`
  scratch as part of the desk — accepted in function, its form to be polished; the
  shutdown use-after-free fix and the `SessionHost` interface — accepted as is. On closing
  with dirty buffers: Omarchy's SUPER+w is a window kill with no chance to ask, so the
  checkpoint-and-restore behaviour is right for that path, but a deliberate in-app quit
  must bring the save prompt back. Two items parked on Matt's fine-tuning list, to be
  worked before Phase 8: a proper in-app close command (with the prompt), and the
  `[No Name]` polish.

- **2026-09-10:** Fine-tuning pass on `polish/untitled-and-dirty-marker`, one PR for the lot,
  every item driven by Matt's hands-on findings after the Phase 7 gate. Chrome: `[No Name]`
  is `Untitled`; no `[+]` dirty marker anywhere (the close prompt and the recovery record
  carry that state); the status line shows the mode alone, sentence case, without
  KTextEditor's `VI:` prefix; no scrollbars (zero-size via the stylesheet, KTextEditor
  offering no switch; wheel, keys and trackpad still scroll); the `?` glyph and both bars
  wear the pane colour; the sidebar's selection bar is painted only while the sidebar has
  focus and neither pane carries an accent line; help is two columns, themed header, sized
  to its content, unshifted letters in lower case (`Ctrl+s`), the discarding close renamed
  "Close buffer, discard", and `:q` listed as "IYKYK". Editor: `Ctrl+D`/`Ctrl+U` released
  from Kate's Comment/Uppercase actions so Vi's half page works (the reading view scrolls
  half a screen on the same keys); Vi's `:` line and its completion drop-down themed (the
  drop-down is a parentless QCompleter popup, reached only by an application-level rule).
  Bundled syntax themes "OmaNotes Dark/Light" (Breeze copies under `src/editor/themes/`,
  loaded from KSyntaxHighlighting's `themes-addons` resource path) with CurrentLine and
  Separator fully transparent: `current-line-color`, set since Phase 6, was never a
  KTextEditor config key, so the grey current-line bar and the icon-border hairline had
  been the theme's all along. The parked close item lands here: `:q` quits with a
  Save / Discard / Cancel prompt for unsaved work, `:q!` discards, `:wq`/`:x` write the
  active buffer first, `:wqa`/`:xa` write every named one, an unsaved Untitled refuses with
  "no file name"; Vi's own `:q` had asked a host application OmaNotes never registers, so
  it did nothing. Bugs found on the way and fixed: the sidebar's first-entry row was
  current but never selected (a QTreeView given focus with no current row selects
  nothing); the leader's "Space …" feedback outlived the sequence it announced; focus is
  returned to the editor after the quit prompt. Left alone by choice: the block cursor in
  Normal mode, hard-coded in KTextEditor's Vi mode. Awaiting Matt's gate.

- **2026-09-10:** Fine-tuning pass accepted. PR #30 merged after Matt's hands-on gate; each of
  its twenty-one commits had been hand-tested as it landed. Both items parked at the Phase 7
  gate are closed: the deliberate in-app quit with its save prompt (`:q` and family) and the
  `Untitled` polish. The block cursor in Normal mode stays, by Matt's choice. Housekeeping on
  2026-09-11: the repository directory was renamed from `omanotes` to `OmaNotes`, which left both
  build trees pointing at a source path that no longer existed; `build/dev` and `build/release`
  were reconfigured with `cmake --preset <name> --fresh`. Evidence on the merged tip `97418a6` at
  the new path: 22/22 ctest under sanitizers, format-check, clang-tidy and security-check clean,
  release build current. Remote branches from squash-merged PRs were deleted; the parked
  `experiment/phase-3-general-purpose` branch and its tag remain on `origin`. Matt is taking a
  further hands-on pass over his fine-tuning list before Phase 8 opens.

- **2026-09-11:** Sidebar mouse route, on `polish/sidebar-mouse-toggle`, PR #32, merged after
  Matt's hands-on gate. His finding: opening the sidebar was the one core action the mouse could
  not do. A flat glyph at the head of the buffer strip runs the existing `pane.sidebar.toggle`
  command, so focus rules and the session snapshot are unchanged; it takes no focus and wears
  the pane colour like the `+` and the `?`. Glyph settled at the gate in three steps: `≡`
  rejected as a web hamburger, `»` chosen, then made to turn to `«` while the tree is out, with
  the tooltip following ("Show sidebar (Space e)" / "Hide sidebar (Space e)"). Every show and
  hide of the sidebar now passes through one helper, `setSidebarShown`, which also sets the
  glyph, so it cannot drift from the tree's state across the toggle, `Ctrl+H` and session
  restore. New test `mouseTogglesTheSidebar`; `themeDressesEveryRegion` asserts the glyph's
  stylesheet rule. Evidence on the branch tip `4c1db0b`: 22/22 ctest under sanitizers,
  format-check, clang-tidy and security-check clean, release build current. No PLAN.md block;
  logged as a fine-tuning item. The fine-tuning list is otherwise clear; Phase 8 is next.

- **2026-09-11:** Block 8.1.1 built on `task/8.1.1-threat-model`: `docs/threat-model.md`, no
  code or tests changed. Six assets, seven actors (the primary adversary is a writer inside the
  workspace, per the 2026-09-07 note), seven trust boundaries, five structural facts the model
  rests on (the editor never opens a file itself; one writer; nothing in the workspace is ever
  deleted; no process or socket; canonical component-wise containment), and 47 threat rows in
  twelve areas, each with the mitigating code and the proving test by `file:line`, or a plain
  statement that neither exists. Two experiments recorded: `kate:` modelines are inert on the
  adapter's load path (seven variables unmoved through load, set, highlighting change, and
  modify-and-save; throwaway test, reverted), and KTextEditor persists Vi registers and macros
  to `~/.config/katevirc`. Twenty-eight findings for 8.1.3, one Blocking: the open, reload,
  and conflict-hash paths read by name after validation, follow symlinks, and have no size
  cap, reachable with no user action through the watcher. Seven need Matt's ruling before code
  moves (katevirc, two-instance record adoption, CI, dangerous roots, read-only notes,
  dependency policy, record retention). Awaiting Matt's gate on the document before 8.1.2.

- **2026-09-11:** Findings F-1 and F-7 closed on `task/8.1.3-high-severity` (8.1.3 work, the
  two rows the threat model rated High; stacked on the 8.1.1 branch so the document's rows
  could be updated in the same change). F-1: `readNoteFile` (`src/persistence/note_reader.cpp`)
  is the one reader for every note read after validation: `O_NOFOLLOW` on the final component,
  regular-file check on the open descriptor, 16 MiB cap matching the recovery ceiling; used by
  the open, the explicit reload, the watcher's reload, and the conflict hash, and both reloads
  re-validate the tracked path first. A note that is now a symlink, a pipe, a directory, or
  too large is refused with a message and the buffer is kept. Found on the way: the watcher
  handler looked buffers up through a canonicalising lookup, so a swapped note made its own
  buffer unfindable and the swap went unreported; it now matches the exact tracked path.
  F-7, ADR 0012: a per-root `flock` on `sessions/<id>/instance.lock`; only the holder
  restores, adopts, or checkpoints into existing recovery records, and any other instance
  restores structure only, reopens dirty notes clean, says how many are held by another
  OmaNotes, and writes only its own new records. No record-format change. New `note-reader`
  suite (5 cases: limit inclusive, over limit, symlink, directory and pipe without blocking,
  missing versus unreadable); `conflict-detector` gains the symlink and oversize cases; the
  window suite gains oversized open refused, symlink swap kept and reported, growth past the
  limit kept; `session-restore` gains the second-instance scenario and the lock following
  clean close and kill. Awaiting Matt's gate.
