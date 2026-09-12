# OmaNotes threat model

> **Block:** 8.1.1. **Status:** draft for Matt's gate, 2026-09-11.
> **Audited tip:** `main` at `479fb3c` (PR #32 merged). Line numbers refer to that tip.
> **Amended 2026-09-11:** findings F-1 and F-7 closed on `task/8.1.3-high-severity` (#35).
> Second pass the same day: F-2, F-3, F-4, F-6, F-8, F-10, F-13 closed across #36, #37, #38, #39;
> F-5 and F-9 accepted by Matt's ruling and recorded below. Block 8.1.2 closed F-18 and F-19 with
> tests. **The remaining Low rows are deferred by Matt's ruling of 2026-09-11**: the High and
> moderate tiers are closed or accepted, and he judged the Lows not worth chasing before the
> first release. Their impact is as each row states; none affects A4 or A5. Rows carry their
> new status; line numbers elsewhere are from the audited tip and drift by the size of those
> changes.
> **Rule of the document:** every mitigation names the code that does it and the test that
> proves it. A row with no test says so. A threat with no mitigation is listed, not omitted.

## 1. How to read this

OmaNotes is a native Markdown editor whose workspace is an ordinary directory. The security
question is therefore narrow and concrete: **can anything the program reads, from the
command line, a note, a config file, or a file changing under it, make it touch files it
should not, run code it should not, lose writing it should hold, or lie about what it did?**

Each threat below has an id (`T-<area>-<n>`), a status, the mitigation with `file:line`
references, the test that proves it, and any residue. Statuses:

| Status | Meaning |
| --- | --- |
| **Mitigated** | Code closes it and a named test exercises the closure. |
| **Partial** | Code closes part of it, or closes it without a test, or the closure is a side effect nothing records. |
| **Unmitigated** | Nothing in the code addresses it. |
| **Inert** | The attack cannot happen today because of a property of the design, but nothing tests that property and a small change would revive it. |
| **Accepted** | Matt ruled it out of scope or acceptable, with the date and the reason. |

Findings for block 8.1.3 are collected in section 9 with a proposed severity and the smallest
decision each needs. Nothing in this document changes code; that is 8.1.3's job.

## 2. Assets

| Id | Asset | What matters |
| --- | --- | --- |
| A1 | Notes on disk, the workspace's `.md` files | Integrity and availability. Confidentiality is the filesystem's job, not the editor's. |
| A2 | Unsaved writing: dirty buffers and their recovery records | Availability above all. Confidentiality equal to the notes themselves. |
| A3 | Session state under `$XDG_STATE_HOME/omanotes` | Structural only, private to the user, never a channel for reading or writing elsewhere. |
| A4 | Every file outside the workspace root | The program must never read, list, or write one on the say-so of an argument, a note, or a record. |
| A5 | The user's session and machine | No process spawned, no code run, no network opened, on the say-so of content or config. |
| A6 | The user's trust in what the screen says | Status line, tab names, link targets, and prompts must show what is true. |

## 3. Actors and trust

| Id | Actor | Trust |
| --- | --- | --- |
| P1 | Matt, the interactive user | Trusted. Makes mistakes: wrong root, `:q!`, `rm` in another terminal. The program should make those recoverable, not impossible. |
| P2 | Agents and tools writing into the workspace | Trusted to exist, **not trusted in what they write**. Every note is untrusted input (recorded in the plan on 2026-09-07). This is the primary adversary of this document: a writer inside the root. |
| P3 | Authors of notes obtained elsewhere: cloned repos, shared folders, downloads | Untrusted. Same threats as P2 without the live timing. |
| P4 | Omarchy theme and shell config, the keymap file | Same user, so trusted in intent, parsed defensively because they are edited by hand and by other tools. |
| P5 | Other processes running as the same user | Out of scope for confidentiality: same uid can read anything the editor can. In scope for concurrency: a second OmaNotes, an agent mid-write. |
| P6 | Other users on the machine | In scope only for the state directory's permissions. |
| P7 | The network | There is none. The program opens `http`/`https` links in the browser and does nothing else. |

## 4. Trust boundaries

| Id | Boundary | Where it lives |
| --- | --- | --- |
| B1 | Command line and environment into the launch request | `src/main.cpp`, `src/app/launch_request.cpp` |
| B2 | The workspace root against the rest of the filesystem | `src/workspace/workspace_root.cpp`, used by every open, save, list, search, and link |
| B3 | Note bytes into the editor, the reading view, and search | `src/ui/main_window.cpp` (`readNoteBytes`, `decodeNote`), `src/ui/markdown_view.cpp`, `src/workspace/text_search.cpp` |
| B4 | Config files into behaviour and into the stylesheet | `src/app/keymap.cpp`, `src/ui/theme_adapter.cpp` |
| B5 | The program onto disk | `src/persistence/atomic_file_writer.cpp`, `src/session/session_store.cpp`, `src/persistence/recovery_store.cpp` |
| B6 | The program to the desktop | `QDesktopServices::openUrl` at `src/ui/markdown_view.cpp:172`, the only outbound edge |
| B7 | This process against other processes | The file watcher, concurrent instances, KTextEditor's own state files |

## 5. Structural facts the whole model rests on

These are true today and load-bearing. Each is worth a regression test, because losing one
silently would reopen several rows at once.

- **S1. The editor never opens a file itself.** KTextEditor's document is created with no URL
  (`src/editor/ktext_editor_adapter.cpp:51`) and only ever receives text through `setText`
  and `loadText` (`:78`, `:80-89`). The adapter exposes no open or URL method
  (`ktext_editor_adapter.hpp:22-39`). Consequences: KTextEditor's swap files, backup files,
  encoding sniffing, its own modified-on-disk reload, and its `kate:` modeline reader all have
  nothing to act on. Verified for modelines by experiment on 2026-09-11 (section 7, T-E4).
- **S2. Every write to a note goes through one writer** (`AtomicFileWriter::write`,
  `src/persistence/atomic_file_writer.cpp:176-205`), and every write of state goes through the
  same core (`replaceFileAtomically`, `:89-174`). There are exactly three writers in the
  program: notes (`:202`), the session snapshot (`src/session/session_store.cpp:162`), and
  recovery records (`src/persistence/recovery_store.cpp:261`).
- **S3. The program never deletes a file inside the workspace.** Its three deletions are its
  own temporary on failure (`atomic_file_writer.cpp:114`), stale session temporaries older
  than 15 minutes in its own state directory (`session_store.cpp:71-89`), and one recovery
  record by validated UUID (`recovery_store.cpp:306-312`). `RecoveryStore::removeAllExcept`
  exists (`:346-363`) but is called from nowhere in `src/`.
- **S4. No process is spawned and no socket is opened by the program's own code.** No
  `QProcess`, `system`, `popen`, `fork`, or `exec` in `src/`; no `QNetwork*`. The single
  outbound edge is `QDesktopServices::openUrl` for `http`/`https` only.
- **S5. Containment is decided canonically, component by component.**
  `WorkspaceRoot::contains` (`src/workspace/workspace_root.cpp:73-88`) canonicalises the
  candidate, fails closed on error, and compares path components, so `/notes-evil` is not
  inside `/notes` and a symlink anywhere in the path is resolved before the comparison.

## 6. Threat catalogue

### 6.1 Launch and the root (B1, B2)

**T-R1. Escape the root through the launch argument** (`omanotes ../x.md`, `omanotes link.md`).
- Status: **Mitigated.**
- Mitigation: relative paths join the canonical working directory
  (`src/app/launch_request.cpp:49-50`); a file argument goes through `WorkspaceRoot::resolveFile`
  (`src/workspace/workspace_root.cpp:90-122`): exists, canonical, `contains`, regular file,
  readable.
- Proof: `tests/unit/launch_request_test.cpp:151-161` (symlinked root followed, `..` refused,
  escaping symlink refused `:155-157`).

**T-R2. Make the program adopt a dangerous root** (`omanotes /`, `omanotes ~`,
`omanotes /etc/motd.md` making `/etc` the root).
- Status: **Unmitigated, by design so far.**
- Detail: an absolute file argument makes its parent the root with no confirmation
  (`launch_request.cpp:67-78`). `/` and `$HOME` are accepted like any directory. The sidebar
  and search are lazy and capped (T-D1), so this is not a crash, but it is the widest possible
  read scope handed over by one argument.
- Closed as ruled (F-13, #36): a status-line notice for `/` and the home directory, nothing
  narrower. Test: `noticesOnlyTheWidestRoots` (`tests/unit/launch_request_test.cpp`).

**T-R3. Smuggle options past argument validation.**
- Status: **Partial.**
- Detail: unknown `-x` options are refused (`launch_request.cpp:28-31`), but `QApplication` is
  constructed first (`src/main.cpp:31` before `:39-46`), so Qt strips its own options
  (`-platform`, `-style`, `-stylesheet`, `-qwindowgeometry`, and plugin-relevant ones) before
  the check sees them. `--smoke-test` is a test hook honoured anywhere in argv in the shipped
  binary (`main.cpp:21-26`). No `--` separator, so a file named `-note.md` cannot be opened.
- Proof: option refusal tested `tests/unit/launch_request_test.cpp`; the Qt bypass is untested.
  Finding F-24.

**T-R4. Environment steers the program to read or write elsewhere.**
- Status: **Accepted (XDG is the contract).**
- Detail: `XDG_CONFIG_HOME` selects the keymap (`src/app/keymap.cpp:191-194`);
  `XDG_STATE_HOME` selects session, recovery, and Omarchy theme discovery
  (`src/session/session_store.cpp:97-100`, `src/ui/theme_adapter.cpp:157-164`). No
  `OMANOTES_*` variables exist. All reads of those files are bounded or refused per T-C*.
- Proof: `tests/integration/launch_process_test.cpp:118-121` redirects them.

**T-R5. The root changes identity under a running program** (directory replaced by a symlink
elsewhere after launch).
- Status: **Unmitigated.**
- Detail: opens and saves re-run `WorkspaceRoot::resolve` on the original path string
  (`src/ui/main_window.cpp:1555`, `:1754`), while the tree caches its root at construction
  (`src/workspace/file_tree_model.hpp:53`). The two can disagree. If the directory vanishes
  between launch parsing and window construction, `FileTreeModel`'s constructor throws
  (`file_tree_model.cpp:23-29`) with no handler in `main.cpp`; and
  `src/app/application_controller.cpp:211` dereferences the result of `WorkspaceRoot::resolve`
  without checking it, which is undefined behaviour if it failed (`:64` does check the same
  thing in `start()`).
- Closed in part (F-10, #36): the notice returned nothing on a failed resolution and a startup
  exception exits with a message. The notice and its test went with ADR 0016 (2026-09-12);
  `start()` still checks the resolution before use. The drift itself remains; F-13's ruling
  took the notice, not the pinning.

### 6.2 Reading notes (B2, B3)

**T-N1. A note link or image reaches outside the root** (`../../etc/passwd`, absolute paths,
`file:` URLs, `~`, percent-encoded traversal, symlinks).
- Status: **Mitigated.**
- Mitigation: `classifyLink` (`src/ui/markdown_view.cpp:55-108`) refuses every scheme except
  `http`/`https`, any authority, absolute and empty paths, and non-`.md` targets, then resolves
  through `WorkspaceRoot`. Images (`resolveImageSource`, `:110-153`) refuse any scheme,
  authority, absolute path, and non-raster extension (no SVG), then go through `resolveFile`.
  Note links dispatch the registry's `file.open`, which re-resolves (`main_window.cpp:219-224`,
  `:1553-1567`).
- Proof: `tests/integration/markdown_render_test.cpp:116-133` (traversal and absolute),
  `:148-156` (schemes), `:160-172` (symlink escape), fixtures under `tests/fixtures/markdown/`.
- Residue: `hostile-schemes.md` is not itself rendered by any test, and the hand-written scheme
  list omits two cases the fixture includes (`omanotes-evil://`, `java script:`). Finding F-18.

**T-N2. Raw HTML or script in a note executes or renders.**
- Status: **Mitigated.**
- Mitigation: `QTextDocument::setMarkdown` with `MarkdownNoHTML` (`markdown_view.cpp:197-199`),
  per ADR 0009. HTML is inert text. `setOpenLinks(false)` and `setOpenExternalLinks(false)`
  (`:158-159`) keep the widget from navigating itself. `loadResource` returns nothing for any
  resource type except images (`:229-234`).
- Proof: `markdown_render_test.cpp:88-101`.

**T-N3. A note fetches from the network.**
- Status: **Mitigated, structurally.**
- Mitigation: remote images are refused before any loader runs (`markdown_view.cpp:112-120`);
  the program's own code has no network client (S4).
- Proof: `markdown_render_test.cpp:103-114` counts refusals. Nothing asserts that no socket
  opened; the guarantee is by construction. Residue noted in F-18.

**T-N4. A huge or pathological note exhausts memory or hangs the UI.**
- Status: **Mitigated for size (F-1 closed); Partial for render time.**
- Mitigation (2026-09-11): every read of a note goes through `readNoteFile`
  (`src/persistence/note_reader.cpp`), which checks the size on the open descriptor and
  stops reading at 16 MiB, the recovery ceiling. The open, the explicit reload, the watcher's
  reload, and the conflict hash all use it. A file that grows past the limit is refused and
  a clean buffer is kept with a message. Tests: `refusesAFileOverTheLimit`
  (`tests/unit/note_reader_test.cpp`), `refusesToOpenAnOversizedNote` and
  `keepsTheBufferWhenItsNoteGrowsPastTheLimit` (`tests/unit/main_window_test.cpp`),
  `symlinkedOrOversizedFileIsUnreadableNotReloaded` (`tests/unit/conflict_detector_test.cpp`).
- Residue: no render timeout or node bound below the size cap (F-17 territory).
- Original detail: `readNoteBytes` (`main_window.cpp:66-77`) read the entire file with no size cap,
  into a `std::string`, then a `QByteArray`, then a `QString`, on the UI thread, and hands it
  to the editor and to `setMarkdown` with no size, node, depth, or time bound. The content hash
  for conflict detection reads the whole file too (`src/persistence/conflict_detector.cpp:26`)
  with no regular-file check, so a FIFO or a device reached through an in-root name blocks or
  fills memory. Search, by contrast, caps at 4 MiB per file and 64 MiB per query
  (`src/workspace/text_search.cpp:18-19`). The reading view's pathological-text test
  (`markdown_render_test.cpp:199-215`) asserts completion, not time, and no ctest timeout is
  set.
- Impact: a writer inside the root (P2) can take the editor down without the user doing
  anything, because the watcher reloads clean buffers on change (T-W2). Finding F-1.

**T-N5. Open a file that is not Markdown or not a regular file.**
- Status: **Mitigated.**
- Mitigation: `.md` enforced on open (`main_window.cpp:1565`) and on link targets
  (`markdown_view.cpp:88-95`); `resolveFile` requires a regular file
  (`workspace_root.cpp:90-122`); NUL bytes refused as binary (`main_window.cpp:66-77`); strict
  UTF-8 decode (`:79-88`).
- Proof: `tests/unit/main_window_test.cpp` `rejectsExplicitNonMarkdownFileClearly`;
  `launch_request_test.cpp:164-185`.

**T-N6. The file changes between validation and read** (time-of-check to time-of-use).
- Status: **Mitigated for the final component (F-1 closed); the directory-swap race remains
  under T-S4 / F-2.**
- Mitigation (2026-09-11): `readNoteFile` opens with `O_NOFOLLOW` and checks the descriptor
  is a regular file, so a note swapped for a symlink, directory, or pipe after validation is
  refused rather than read. Both reload paths re-run `resolveFile` and require the canonical
  path to still equal the tracked one (`MainWindow::revalidateTrackedPath`). Tests:
  `refusesASymlinkEvenToAReadableFile`, `refusesADirectoryAndAPipeWithoutBlocking`
  (`tests/unit/note_reader_test.cpp`), `keepsTheBufferWhenItsNoteIsSwappedForASymlink`
  (`tests/unit/main_window_test.cpp`).
- Correction found while fixing (2026-09-11): the watcher-driven variant described below was
  already blocked, by accident rather than design. The registry's path lookup canonicalises,
  so once the note was a symlink the handler could not find its own buffer and returned
  silently, with no message and no conflict mark. The explicit reload (`:e!`) did follow
  the link. The fix looks the buffer up by its exact tracked path
  (`BufferRegistry::findByExactPath`), so the swap is now reported and marked as well as
  refused.
- Original detail: `loadMarkdownFile` validated through `resolveFile` (`main_window.cpp:1560`) and then
  reads by name with a plain `std::ifstream` (`:67`), which follows symlinks. The reload on
  external change (`:1409`) and the reload command (`:1361`) skip validation entirely and read
  the stored path. Swapping an open note for a symlink to any user-readable file makes the
  program read and display that file. Search shows the right pattern: `O_NOFOLLOW` plus
  `fstat` (`text_search.cpp:28-38`), the only place it is used.
- Impact: disclosure of any file the user can read, driven by P2 with no user action.
  Finding F-1.

**T-N7. Symlinks inside the root** are handled inconsistently.
- Status: **Partial.**
- Detail: the sidebar and the index skip every symlink (`file_tree_model.cpp:183-186`,
  `file_index.cpp:49-52`); opening and saving follow them as long as the target canonicalises
  inside the root (`workspace_root.cpp:99-103`, `atomic_file_writer.cpp:44-57`). So `:e link.md`
  opens what the tree never shows. Hidden `.md` files are searchable but not listed
  (`file_index.cpp:55-61` filters dot-directories only; `file_tree_model.cpp:178` hides
  dot-files).
- Proof: each half is tested (`file_tree_model_test.cpp:78-92`, `workspace_search_test.cpp:69-79`,
  `atomic_save_test.cpp:98-153`); the asymmetry is recorded nowhere. Finding F-14.

### 6.3 Links, clipboard, and the desktop (B6)

**T-L1. A link in a note runs something.**
- Status: **Mitigated for schemes; Accepted for http(s).**
- Mitigation: only `http`/`https` reach `QDesktopServices::openUrl` (`markdown_view.cpp:58-60`,
  `:172`), never through a shell; everything else is refused with a reason
  (`:61-95`). Nothing navigates without a click (`:168`). The hover target is shown in the
  status line (`:190-191`, `main_window.cpp:230-236`).
- Proof: `markdown_render_test.cpp:148-156`.
- Accepted: one click opens the browser at a URL the note's author chose, with no
  confirmation. ADR 0009 accepts this. The browser is the desktop's boundary, not ours.

**T-L2. Copy from the reading view carries more than text.**
- Status: **Unmitigated, low impact.**
- Detail: copy is `QTextEdit::copy` (`main_window.cpp:1622-1631`), which puts `text/plain` and
  `text/html` on the clipboard; the HTML flavour carries `<img src>` references to
  workspace-relative paths. Pasting into another application may reveal the workspace
  layout. Paste into the editor is text only via KTextEditor (`:1601-1620`); paste into the
  reading view is refused (`:1603-1606`).
- Residue: undocumented. Finding F-27.

### 6.4 The status line and other text the user trusts (A6)

**T-U1. A file name, link target, or path in an error renders as rich text.**
- Status: **Unmitigated.**
- Detail: `statusArea_` is a `QLabel` left at `Qt::AutoText` (`main_window.cpp:126-134`), so
  Qt applies its looks-like-HTML heuristic. Untrusted strings reach it verbatim: file names
  (`:1375`, `:1424`, `:1430`, `:1437`, `:1824`), hover targets uncapped (`:230-235`), and
  `WorkspaceError` messages carrying full paths (`:1557`, `:1562`, `:1581`). The sidebar
  heading (`src/ui/sidebar.cpp:29`), the search palette's status (`src/ui/search_palette.cpp:41-43`,
  `:147-154`), and the two `QMessageBox` prompts naming a buffer (`:1070`, `:1281-1288`) have
  the same shape. A rich-text `QLabel` honours `<img src="/local/path">`, so a note or
  directory name can spoof the status line or probe local files. The program already knows the
  fix: the keymap warning is set to plain text (`:674`), and nothing else is.
- Closed (F-3, #36): the status line, sidebar heading, search status and both close prompts are
  `Qt::PlainText`. Test: `showsUntrustedNamesAsPlainText` (`tests/unit/main_window_test.cpp`).

**T-U2. Error text discloses filesystem structure outside the root.**
- Status: **Unmitigated, low impact.**
- Detail: `DocumentStore::resolveTarget` reports "No such directory: <absolute path>" before
  any containment check (`src/persistence/document_store.cpp:48-51`), and `saveTo` shows it
  (`main_window.cpp:1762`), so `:w /some/where/x.md` tells the user whether `/some/where`
  exists. `WorkspaceError` messages embed absolute paths (`workspace_root.cpp:26-28`) and the
  search palette shows them verbatim (`search_palette.cpp:147-148`). The window title carries
  the full root path (`main_window.cpp:183-185`) and the tree's tooltips carry absolute paths
  (`file_tree_model.cpp:125-127`).
- Impact: the user is the only reader, so this is an existence oracle for the user's own
  filesystem. Finding F-12.

### 6.5 Configuration (B4)

**T-C1. The keymap runs a program or reaches a command it should not.**
- Status: **Mitigated.**
- Mitigation: JSON via `QJsonDocument` (`src/app/keymap.cpp:211-219`), 64 KiB cap
  (`:207-210`), only `leaderBindings` and `shortcuts` accepted (`:88-97`), every command id
  checked against the registry (`:107-111`, `:139-143`), target-taking commands barred
  (`:16-18`), `Space ?` pinned to help (`:131-135`), direct shortcuts restricted to
  `Ctrl+Alt` chords plus four named legacy keys (`:31-60`), editor action collisions refused
  (`:153-158`), dry run on a copy before applying (`:168-172`, `:175-189`). Malformed input
  rejects the whole file, keeps defaults, and shows a plain-text warning with the location
  (`main_window.cpp:669-681`).
- Proof: `tests/unit/keymap_test.cpp:85-86`, `:104-105`, `:117-119`, `:129`, `:155`.
- Residue: the file is read through a symlink (`:199-204`); safe because of the cap, but
  unrecorded in ADR 0008.

**T-C2. A theme file injects into the stylesheet or exhausts the program.**
- Status: **Partial.**
- Mitigation for injection: every colour reaches the stylesheet as `QColor::name(HexRgb)`
  (`main_window.cpp:1855`), so an invalid string never becomes a colour and a valid one is
  normalised to `#rrggbb`; the font size is `toDouble` clamped to 6–32
  (`theme_adapter.cpp:256-263`). Contrast rules force readable pairs (`:203-211`, `:234-254`).
  Tests `tests/unit/theme_adapter_test.cpp:127-176`.
- Gap for exhaustion: `readFlatToml` (`theme_adapter.cpp:22-57`) and `themeName()` (`:168-175`)
  read unbounded files on the UI thread at startup and on every watcher-driven refresh. The
  keymap has a cap; the theme does not. Same-user files, so low severity. Finding F-16.
- Uncertainty: whether `QString::toDouble` can report success for `nan`, which `std::clamp`
  would pass through into the stylesheet. Believed no; untested.

### 6.6 Writing to disk (B5)

**T-S1. A save lands outside the root** (through `:w`, save-as, or a symlink).
- Status: **Mitigated.**
- Mitigation: `AtomicFileWriter::write` checks containment on the requested target
  (`atomic_file_writer.cpp:181`) and again on the symlink-resolved destination (`:186-195`),
  refuses directories (`:198`); `DocumentStore::resolveTarget` enforces `.md` and refuses to
  create directories (`document_store.cpp:26-54`).
- Proof: `tests/integration/atomic_save_test.cpp:127-179`, `tests/unit/document_store_test.cpp:28-33`.

**T-S2. A crash, full disk, or I/O error mid-save corrupts a note.**
- Status: **Partial.**
- Mitigation: `mkstemp` in the target's directory, mode set before any byte lands, write
  loop with `EINTR` retry, `fsync`, `rename`, directory `fsync` (`atomic_file_writer.cpp:89-174`);
  the temporary is removed on failure (`:108-116`). ADR 0005.
- Proof: injected short write, `ENOSPC`, `EIO`, failed fsync, and failed rename each leave the
  prior file byte-identical, tested for the session store (`tests/integration/session_store_test.cpp:196-239`)
  and the recovery store (`recovery_store_test.cpp:255-289`).
- Gap: the fault seam is never plumbed through the note-saving entry point (`:202` passes
  none), so the same failures are untested for notes, though the code path is shared. A
  directory-fsync failure after a successful rename is reported as failure while the new
  bytes are already live (`:172`), contradicting the header (`atomic_file_writer.hpp:65-66`)
  and ADR 0005; the buffer stays dirty with a stale revision and the next save reports a
  false conflict. Findings F-11 and F-19.

**T-S3. A save overwrites someone else's change** (an agent wrote between load and save).
- Status: **Partial.**
- Mitigation: every save compares the disk's content hash with the revision the buffer last
  loaded or wrote (`main_window.cpp:1767-1800`, `src/persistence/conflict_detector.cpp:16-53`);
  a difference refuses the plain write and requires `!`; the watcher reports changes as they
  happen (T-W1). ADR 0006.
- Proof: `tests/unit/conflict_detector_test.cpp:38-100`; main-window tests
  `keepsEditsAndRefusesPlainWriteWhenFileChangedUnderneath`,
  `refusesToOverwriteExternalChangesEvenBeforeTheWatcherNotices`.
- Closed (F-2, #39): the writer takes a `WritePrecondition` (absent, or a content hash) and
  re-checks it after the new bytes are durable and immediately before the rename; a change in
  between is refused as `ChangedSinceRead` with nothing replaced. ADR 0006 now states the two
  comparisons and names the instant that remains open. Test:
  `refusesToReplaceAFileThatChangedSinceItWasChecked` (`tests/integration/atomic_save_test.cpp`).

**T-S4. The path is swapped between containment check and write.**
- Status: **Unmitigated.**
- Detail: `insideRoot` canonicalises the parent (`atomic_file_writer.cpp:31-38`), then
  `mkstemp` and `rename` resolve the path by name again. Replacing a directory component with a
  symlink in that window defeats the check. No `openat`, directory fd, or `O_NOFOLLOW` is used
  on the write path.
- Impact: requires P2 to win a race against the user's own save. Narrowed by F-2's recheck
  (#39), which reads the destination through the bounded reader just before the rename, so a
  swap to a symlink or a non-regular file in the window is refused too. The directory-component
  swap between that check and the rename is the residue; recorded, not claimed away.

**T-S5. Properties of the original file are lost by the rename.**
- Status: **Partial, undocumented.**
- Detail: permission bits are preserved (`modeFor`, `:61-69`; test `atomic_save_test.cpp:74-96`)
  and writes go through a symlink (test `:98-125`). Owner and group are not (`rename` installs
  a new inode; no `fchown`), extended attributes and ACLs are not, and a hard-linked note is
  silently split from its other names. A read-only note (`0444`) is overwritten because only
  directory permission is needed. ADR 0005 covers permissions and symlinks only.
- Findings F-14 (document) and F-15 (read-only).

**T-S6. Line endings are changed on round trip.**
- Status: **Unmitigated, not a security issue.**
- Detail: no normalisation anywhere; CR characters round-trip literally. No test. Utilitas
  item for 8.3's limitations list. Finding F-25.

### 6.7 Session state and recovery (A2, A3, B5)

**T-P1. The session file or its directory is planted, symlinked, or corrupted.**
- Status: **Mitigated.**
- Mitigation: directories `0700` and file `0600` enforced on every save regardless of umask
  (`session_store.cpp:20-21`, `:45-67`, `:162`); symlinks refused at every directory level and
  on the file for both read and write (`:46-48`, `:157-159`, `:172-174`); a corrupt file is
  reported and never repaired in place (`session_snapshot_test.cpp:202-233`); the root recorded
  inside must match the root asked for (`session_snapshot.cpp:513-523`); stale temporaries are
  swept only by their own prefix, only regular files, only after 15 minutes (`:71-89`).
- Proof: `session_store_test.cpp:153-194`, `:241-261`, `:263-287`, `:289-319`, `:321-351`.

**T-P2. A snapshot makes the program open a file outside the root.**
- Status: **Mitigated.**
- Mitigation: absolute paths refused, then `resolveFile` (`session_snapshot.cpp:525-533`);
  buffers whose path is not under the root are dropped at capture (`main_window.cpp:415-418`).
- Proof: `tests/unit/session_snapshot_test.cpp:264-296`.

**T-P3. A hostile snapshot exhausts the parser.**
- Status: **Partial.**
- Mitigation: 256 KiB document cap read to one byte past (`session_snapshot.cpp:363-366`,
  `:498-500`), 256 buffers (`:451-454`), path 1–4096 bytes without NUL (`:135-155`), bounded
  dimensions and positions (`:172-179`, `:264-274`), unknown keys refused everywhere
  (`:44-52`), version checked first (`:381-397`).
- Proof: `session_snapshot_test.cpp:141-200`; 15 fixtures under `tests/fixtures/session/`.
- Gap: no nesting-depth limit in the program's own code (Qt's parser has an internal cap
  that nothing pins), no per-string limit besides the document cap, and the writer does not
  validate its own upper bounds (`:145-151` checks size only; dimensions are clamped at 1 but
  not at 32767). `docs/session-format.md:242-243` says the writer must. Finding F-26.

**T-P4. Recovery records leak unsaved writing** (A2 confidentiality).
- Status: **Accepted, documented.**
- Detail: records are plaintext note text (`recovery_store.hpp:32`), like Neovim's swap
  files and for the same reason; `docs/session-format.md:150-159` says so and claims no
  encryption. Directory `0700`, file `0600` (`recovery_store.cpp:23-24`, `:56-79`, `:261`);
  test `recovery_store_test.cpp:114-152`. Matt accepted the plaintext statement at the 7.3
  gate.
- Residue, bounded 2026-09-12: records for a workspace whose root no longer exists are kept
  seven days past the last write into its state directory, then removed, unannounced (ADR 0015,
  ADR 0016, `SessionStore::sweepVanishedRoots`). Records for a root that exists are kept until
  that root is opened, which is the only time any parked work is mentioned. Finding F-22, closed.

**T-P5. A planted or symlinked recovery record redirects a write or restores a lie.**
- Status: **Partial.**
- Mitigation: symlinked recovery directory refused (`recovery_store.cpp:57-59`, `:326-329`);
  symlinked record refused on write, load, remove, and list (`:256-259`, `:274-277`,
  `:306-310`, `:332-334`); traversal in a record's path refused (`:82-98`); only exact UUID
  file names are records (`:100-109`); 16 MiB cap on contents (`recovery_store.hpp:25`,
  `:236-240`, `:141-145`); restore re-validates against the live root (`:365-405`).
- Proof: `recovery_store_test.cpp:291-370`, `:408-467`.
- Gap 1: `RecoveryStore` checks the recovery directory but not its ancestors, and creates the
  parent with `create_directories` (`:62`), which follows a symlink; `SessionStore` checks every
  level (`session_store.cpp:128`). A symlinked `sessions/<id>/` sends plaintext note text
  outside the state directory. Closed (F-4, #36): three levels above the recovery directory are
  refused as symlinks on write and on list, without being created or re-moded. Test:
  `refusesASymlinkedDirectoryAboveTheStore` (`tests/integration/recovery_store_test.cpp`).
- Gap 2: a well-formed record planted by P5 is restored as a dirty buffer with the planter's
  text and the planter's in-root path; the status line reports a recovery but nothing marks
  the buffer as recovered from disk rather than typed, and `:w` writes it where the planter
  said. Same-user only, so P5 could have written the file directly; the harm is the user's
  misattribution, not new access. Finding F-15b.

**T-P6. Two instances on one root destroy each other's work.**
- Status: **Mitigated (F-7 closed, ADR 0012).**
- Mitigation (2026-09-11): a per-root `flock` on `sessions/<id>/instance.lock`
  (`src/session/instance_lock.cpp`), taken at start by `ApplicationController`. Only the
  holder restores, adopts, or checkpoints into existing records; any other instance restores
  structure only, reopens dirty notes clean, reports them as held elsewhere, and writes only
  its own new records. The kernel releases the lock on any exit. Tests:
  `secondInstanceLeavesTheFirstsRecordsAlone`, `theLockFollowsTheLiveInstance`
  (`tests/integration/session_restore_test.cpp`).
- Original detail: there was no lock of any kind. The snapshot is last-close-wins, documented
  (`docs/session-format.md:39-43`) and tested for non-corruption
  (`session_store_test.cpp:353-416`; `session_restore_test.cpp`
  `concurrentLaunchesLastCloseWinsWithoutCorruption`). But a second instance treats the
  first's live recovery records as orphans, because "referenced" is computed only against its
  own memory (`application_controller.cpp:88-96`), adopts them as dirty buffers, and then
  checkpoints into the same record id (`session_restorer.cpp:142`). Two writers on one file;
  one side's unsaved text is lost silently. Untested.
- Impact: A2 loss under an ordinary mistake by P1 (a second launch). Finding F-7.

### 6.8 Watching and concurrency (B7)

**T-W1. External changes go unnoticed and get overwritten.**
- Status: **Mitigated, with a silent failure mode.**
- Mitigation: file and parent directory watched, 150 ms coalescing, re-arm after rename or
  delete (`src/workspace/file_watcher.cpp:26-108`); and the save-time hash check catches what
  the watcher missed (T-S3).
- Proof: `tests/unit/file_watcher_test.cpp:36-119`.
- Gap: `QFileSystemWatcher::addPath` results are ignored (`file_watcher.cpp:39`, `:95`), so
  inotify exhaustion degrades silently to no live detection. No status message, no test.
  Finding F-23.

**T-W2. An external change reloads content into the editor without asking.**
- Status: **Accepted (ADR 0006).** The T-N4 and T-N6 caveats are closed by F-1: the reload
  re-validates the path and reads through the bounded reader, so it can no longer be made
  to read the wrong file or too much of one.
- Detail: a clean buffer is reloaded silently (`main_window.cpp:1409-1425`); a dirty one is
  flagged and plain `:w` refuses. The reload path is what makes T-N4 and T-N6 reachable with
  no user action; fixing those closes the sharp edge here.

### 6.9 The editor component (B3, B7)

**T-E1. The Vi command line reaches something dangerous.**
- Status: **Partial.**
- Detail: the program owns `:w` and its family (`src/editor/save_command.cpp:13-31`,
  `main_window.cpp:307-317`, `:1164-1186`) and routes them through the store; every other verb
  passes to KTextEditor (`:1189-1191`). KTextEditor's own command set in the linked library
  includes `set-*` (25 of them, including `set-indent-mode`, which selects a JavaScript
  indenter, and `set-remove-trailing-spaces`, which changes text on save), `reload`, `goto`,
  `char`, `date`, `print`. No `:!`, `:r!`, filter, `grep`, or `man` exists in the library; those
  belong to the Kate application, not the component. So no shell is reachable today, but by a
  property of upstream, not by a boundary this program drew. No test asserts any refusal;
  `vim_behaviour_test.cpp:205-215` proves pass-through works.
- Partial, tests added (F-6, #36): the `:` pass-through is unchanged and still has no deny-list;
  the properties it rests on are pinned by `neverGivesTheDocumentAUrl` and
  `ignoresModelinesInNoteText` (`tests/unit/ktext_editor_adapter_test.cpp`).

**T-E2. KTextEditor's JavaScript engine runs on content.**
- Status: **Inert.**
- Detail: `libQt6Qml` and the `KateScript*` machinery are linked and enabled; they run
  indenters and command scripts shipped with the library, never script from a note. Nothing
  in a note can supply script. Reachable only through `set-indent-mode` on the command line
  (T-E1). Finding F-6.

**T-E3. KTextEditor writes swap or backup files next to notes.**
- Status: **Inert (S1).**
- Detail: both features are on by default in the library and neither is disabled by the
  program; both key off the document's URL, and the document has none. A future call to
  `openUrl` would revive both silently. No test pins this.
- Finding F-6 (regression test).

**T-E4. A `kate:` modeline in a note changes editor behaviour.**
- Status: **Inert, verified by experiment 2026-09-11.**
- Pinned by test since #36 (`ignoresModelinesInNoteText`).
- Detail: a note beginning `<!-- kate: indent-width 7; tab-width 9; replace-tabs on;
  dynamic-word-wrap off; line-numbers on; remove-trailing-spaces all; hl C++; -->` was fed
  through the adapter's `loadText` and `setText`, followed by a highlighting-mode change and a
  modify-and-save cycle, with the same line also tried in the last lines. None of the seven
  values moved. The reader exists in the library and is not disabled; it does not run on this
  load path. The experiment was not committed. Finding F-6 asks for it as a permanent test.

**T-E5. Note text leaves the workspace through KTextEditor's own state.**
- Status: **Unmitigated, undocumented.**
- Detail: KTextEditor persists Vi registers and macros to `$XDG_CONFIG_HOME/katevirc`
  (`ViRegisterContents`, `ViRegisterNames`, `Macro Contents`). Yanked note text therefore sits
  in the user's config directory, outside the `0700` root-scoped state store the program built
  for exactly this kind of data. Observed on this machine (`~/.config/katevirc`, mode `0600`)
  and in the test sandbox (`build/dev/tests/xdg-config/katevirc`). Same user, `0600`, so the
  exposure is equal to the recovery records the user already accepted; the difference is that
  nobody has been told.
- Accepted (F-5, Matt's ruling 2026-09-11): documented in `docs/session-format.md` beside the
  recovery-record statement. Same user, same mode, same class of exposure as records; the
  difference was that nobody had been told, and now they have.

**T-E6. Encoding sniffing or mixed encodings corrupt a note.**
- Status: **Mitigated.**
- Mitigation: the program decodes strictly as UTF-8 and refuses on error
  (`main_window.cpp:79-88`); writes are UTF-8 (`document_store.cpp:62`); KTextEditor's prober
  never sees bytes (S1).

### 6.10 Denial of service and resource limits

**T-D1. Enumeration of a huge or deep tree.**
- Status: **Mitigated for search; Partial for the sidebar.**
- Detail: the index budgets 20,000 entries and marks truncation (`file_index.cpp:37-44`),
  caps names at 4096 and queries at 128 (`:76`), results at 200
  (`text_search.cpp:20`), and cancels through a stop token with a 100 ms debounce
  (`search_palette.cpp:66-136`). The sidebar is lazy (`file_tree_model.cpp:142-162`) but has no
  depth or count cap, and `indexForPath` fetches every level on the path eagerly on the UI
  thread during session restore (`:265-282`). No timeout exists anywhere; `docs/search.md`
  concedes shutdown can block on a stalled filesystem.
- Proof: `workspace_search_test.cpp`, `file_tree_model_test.cpp:101-136`.
- Finding F-17.

**T-D2. A huge note.** See T-N4. Finding F-1.

**T-D3. Recovery-record accumulation.** Bounded for vanished roots by ADR 0015; unbounded only
across roots that still exist, each of which the user can open. See T-P4. Finding F-22, closed.

### 6.11 Build, toolchain, and dependencies

**T-B1. Memory-safety bugs ship.**
- Status: **Partial.**
- Mitigation: `-Werror` with a strict warning set (`cmake/Warnings.cmake:1-22`); ASan and
  UBSan on the dev preset with `halt_on_error` (`cmake/Sanitizers.cmake`, `tests/CMakeLists.txt:465-482`);
  clang-tidy with `clang-analyzer-*`, `bugprone-*`, `performance-*`, `portability-*` as errors
  (`.clang-tidy`); PIE, full RELRO, `BIND_NOW`, NX stack, no RPATH verified by
  `scripts/security-check.sh` and confirmed in the release binary; stack protector present.
- Gaps: `_FORTIFY_SOURCE` is not set (zero `*_chk` symbols in the release binary),
  `_GLIBCXX_ASSERTIONS` is not set, `-fstack-clash-protection` and `-fcf-protection` are
  absent, and `security-check.sh` tests none of these, so the gap is invisible to the gate.
  `docs/development-baseline.md:56` runs the check on the dev preset, so the audited binary
  is the sanitizer Debug build, not the release artefact. `cert-*` is off in clang-tidy
  despite raw POSIX I/O in the persistence layer. The format-check list is hand-maintained and
  already omits at least five compiled sources. LeakSanitizer is disabled and leak checks are
  manual. F-8 closed (#37): `-fstack-clash-protection`, `-fcf-protection=full`,
  `_GLIBCXX_ASSERTIONS`, `_FORTIFY_SOURCE=3` outside Debug; the script verifies stack protector,
  full RELRO, CET marks and fortified calls, and the docs run it on the release preset. F-19
  remains.

**T-B2. Controls are advisory.**
- Status: **Unmitigated.**
- Detail: there is no CI; `.github/` holds only the PR template. Every gate runs when a
  person types it. The PR template's boxes are self-attested.
- Accepted (F-9, Matt's ruling 2026-09-11): manual gating is the process for this project.
  Every PR records the commands and their output; the reviewer runs what he wants to see again.

**T-B3. A vulnerable dependency ships unnoticed.**
- Status: **Unmitigated.**
- Detail: two direct dependencies with version floors (`CMakeLists.txt:23-24`, Qt 6.8, KF6
  TextEditor 6.9) and 138 shared objects in the runtime closure, including a JavaScript
  engine, the KIO network stack, D-Bus, KAuth, and KF6Crash, all pulled by KTextEditor. Nothing
  is vendored (good). No SBOM, no advisory watch, no pinning; Arch rolls all of it. The
  baseline document is dated 2026-08-29 and is already behind this host (kernel `7.2.3` vs
  `7.1.9`; `qt6-base 6.11.2-3` vs `-2`). Release minimums promised in
  `docs/development-baseline.md:33` have not been derived. Finding F-20.

**T-B4. Bundled third-party data lacks licence.**
- Status: **Mitigated.**
- Detail: the two syntax themes carry SPDX MIT headers naming the Breeze authors and the
  derivation (`src/editor/themes/*.theme`); data, not code, compiled as a Qt resource.

**T-B5. The program aborts with unsaved work.**
- Status: **Partial.**
- Detail: three `qFatal` paths remain in release (`main_window.cpp:683`, `:766`, `:770`), all
  internal-consistency asserts at startup before any buffer can be dirty in practice, but
  nothing prevents a later call site. Recovery records are the compensating control for any
  crash after the first checkpoint (2 s debounce). Finding F-21.

### 6.12 Logging

**T-G1. Note content or secrets reach logs.**
- Status: **Mitigated.**
- Detail: six logging sites in `src/`; none prints note text. Two print a path: a launch
  refusal (`main.cpp:58`) and a bad keymap (`main_window.cpp:679`). No log file is written, no
  message handler is installed, no logging categories exist. Qt's `QT_LOGGING_RULES` still
  governs the linked libraries, which is outside the program's control.

## 7. Experiments run for this document

| Date | Question | Method | Result |
| --- | --- | --- | --- |
| 2026-09-11 | Do `kate:` modelines fire on a `setText`-populated, URL-less document? | Throwaway test slot in `tests/unit/ktext_editor_adapter_test.cpp`, seven variables in first and last lines, through `loadText`, `setText`, a highlighting change, and modify-and-save; reverted after the run. | No value changed in any step. T-E4 is inert on this load path. |
| 2026-09-11 | Does KTextEditor persist register contents outside the workspace? | Inspected `~/.config/katevirc` and the test sandbox's `katevirc` (keys only). | Yes: `ViRegisterContents`, `ViRegisterNames`, `Macro Contents` present in both. T-E5. |
| 2026-09-11 | Is the release binary fortified? | `readelf -sW` on `build/release/src/omanotes` for `*_chk` symbols. | Only `__stack_chk_fail`. Not fortified. T-B1. |

## 8. Residual risk statement

With the findings in section 9 still open, the program is safe against a note that tries to
run code, fetch from the network, or point outside the root, and it keeps saved notes intact
under the failures it simulates. Its remaining exposure is to a writer inside the workspace
who can make it read the wrong file or too much of a file, to two copies of itself, and to
the absence of any enforced gate. None of those is exotic for the stated adversary (P2), and
the first is the one to close before daily use.

## 9. Findings for block 8.1.3

Severity: **Blocking** means fix before the Phase 8 gate; **Should-fix** means fix in 8.1.3
unless Matt defers with a recorded reason; **Low** may be deferred to the limitations list;
**Decide** needs a ruling before anyone writes code.

| Id | Severity | Finding | Threats | Smallest decision |
| --- | --- | --- | --- | --- |
| F-1 | ~~Blocking~~ **Closed 2026-09-11** | Open and reload read by name after validation, follow symlinks, and have no size cap; the watcher makes this reachable with no user action. | T-N4, T-N6, T-W2 | Done: `readNoteFile` (`src/persistence/note_reader.cpp`) with `O_NOFOLLOW`, regular-file check on the descriptor, 16 MiB cap matching the recovery ceiling; used by open, both reloads, and the conflict hash; reloads re-validate the path first. |
| F-2 | **Closed 2026-09-11** (#39) | Save-time and path-resolution races between check and rename; ADR 0006 overstated the guarantee. | T-S3, T-S4 | Done, both: a content or absence precondition re-checked just before the rename, and ADR 0006 corrected to name the residue. |
| F-3 | **Closed 2026-09-11** (#36) | Status line, sidebar heading, search status, and buffer prompts rendered untrusted names as rich text. | T-U1 | Done: plain text on each, tested with a markup-named note. |
| F-4 | **Closed 2026-09-11** (#36) | `RecoveryStore` did not refuse symlinked ancestor directories. | T-P5 | Done: three levels checked on write and list, tested with a symlinked `sessions/<id>`. |
| F-5 | **Accepted 2026-09-11** | KTextEditor persists yanked text and macros to `~/.config/katevirc`. | T-E5 | Matt's ruling: document. Recorded in `docs/session-format.md`, "Sensitive content". |
| F-6 | **Closed 2026-09-11** (#36), pass-through ruling open | Editor boundary properties were inert rather than tested. | T-E1–T-E4 | Done: tests pin no-URL and modelines-inert. Whether `set-*` and `reload` should be intercepted is not ruled; the `:` line stays as it is. |
| F-7 | ~~Decide~~ **Closed 2026-09-11** | Two instances on one root adopt each other's live recovery records and overwrite them. | T-P6 | Done, ADR 0012: per-root `flock`; the holder owns recovery, everyone else restores structure only and never touches existing records. |
| F-8 | **Closed 2026-09-11** (#37) | Hardening flags were absent and unchecked; the check ran on the dev binary. | T-B1 | Done: flags on every target, script checks each and says what a sanitizer build cannot show, docs run it on release. |
| F-9 | **Accepted 2026-09-11** | No CI; every gate is manual. | T-B2 | Matt's ruling: no CI. Manual gating with recorded output is the process. |
| F-10 | **Closed 2026-09-11** (#36) | Unchecked dereference of a failed root resolution; uncaught throw at startup. | T-R5 | Done: the notice returned nothing (the notice itself was removed by ADR 0016), startup exceptions exit 2 with a message. The handler itself is untested (needs an untimeable race). |
| F-11 | Low | Directory-fsync failure after rename reported as failure while bytes are live; stale revision causes a false conflict. | T-S2 | Approve treating it as success-with-warning and updating the header and ADR 0005. |
| F-12 | Low | Existence oracle in save-target errors; absolute paths in error text. | T-U2 | Approve reordering containment before the directory check and trimming paths to root-relative in messages. |
| F-13 | **Closed as ruled 2026-09-11** (#36) | Any directory becomes the root on one argument; root identity can drift after launch. | T-R2, T-R5 | Matt's ruling: notice, not refusal. Done for `/` and home. Pinning the root by descriptor was not asked for and is not done. |
| F-14 | Low | Symlink handling differs by layer; owner, ACLs, xattrs, hard links not preserved; hidden `.md` searchable but not listed. | T-N7, T-S5 | Approve recording all of it in ADR 0005 and `docs/search.md`. |
| F-15 | Decide | Read-only notes are overwritten (Vim refuses with E45). | T-S5 | Rule: refuse without `!`, or accept. |
| F-15b | Low | A planted recovery record restores with no provenance mark. | T-P5 | Approve a "recovered" mark in the tab or status until first save, or accept. |
| F-16 | Low | Theme files read unbounded on the UI thread. | T-C2 | Approve a 64 KiB cap like the keymap. |
| F-17 | Low | No depth or count cap on the sidebar; eager path fetch on restore; no timeouts. | T-D1 | Approve a per-directory entry cap and a note in the limitations list. |
| F-18 | **Closed 2026-09-11** (8.1.2) | Test drift: `hostile-schemes.md` unrendered, two schemes untested, fixture README overstated, no ctest timeout. | T-N1, T-N3, T-N4 | Done: the fixture is rendered by a test, both schemes covered, README corrected, every ctest has a timeout. |
| F-19 | **Closed in part 2026-09-11** (8.1.2) | Fault seam not plumbed through the note save path; `cert-*` off; format-check list incomplete; leak checks manual. | T-S2, T-B1 | Done: faults reach the note writer with five injected failures tested; the format list is discovered from the tree. Not done, deferred with the Low tier: `cert-*` and automated leak checks. |
| F-20 | ~~Decide~~ **Closed as ruled 2026-09-11**, row updated 2026-09-12 | Dependency policy has no mechanism; 138-object closure; baseline stale; minimums not derived. | T-B3 | Matt's ruling, recorded in `docs/dependency-review.md`: the closure listed, Arch's security tracker for the named packages as the advisory route, `arch-audit` refused as a dependency, floors in `CMakeLists.txt` as the only pins, the baseline refreshed. Listed in `docs/limitations.md`. |
| F-21 | Low | Three `qFatal` paths in release without a checkpoint. | T-B5 | Approve converting to a status message plus graceful exit, or accept as startup-only. |
| F-22 | ~~Decide~~ **Closed 2026-09-12** | Recovery records accumulate without bound for dead workspaces. | T-P4 | Matt's ruling: a seven-day bound for roots that no longer exist, silent (ADR 0016); roots that exist are kept. ADR 0015; `SessionStore::sweepVanishedRoots`, six store tests and two launch tests. |
| F-23 | Low | Watcher registration failures are silent. | T-W1 | Approve a status message when `addPath` fails. |
| F-24 | Low | Qt strips its options before validation; `--smoke-test` ships. | T-R3 | Approve documenting the Qt behaviour; rule on removing the smoke hook from release. |
| F-25 | Low | Line endings not normalised or tested. | T-S6 | Record in limitations; test CRLF round trip. |
| F-26 | Low | Snapshot writer does not enforce its own upper bounds; no JSON depth limit in program code. | T-P3 | Approve clamping at capture and an explicit depth check, with a depth-bomb fixture. |
| F-27 | Low | Reading-view copy puts HTML with workspace-relative image paths on the clipboard. | T-L2 | Rule: plain-text-only copy, or document. |

## 10. What this document does not cover

- Confidentiality against the same user or root: out of scope by the platform's model.
- The browser, `xdg-open`, and whatever the desktop does with an `http(s)` URL.
- KTextEditor and Qt internals beyond what the program configures and what was observed.
- Packaging and installation (task 8.2) and the public-release checklist (task 8.3).
