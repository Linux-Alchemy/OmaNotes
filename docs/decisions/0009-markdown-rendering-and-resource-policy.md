# ADR 0009: Markdown Rendering and Resource Policy

- **Status:** Accepted; implementation begins with Task 6.1
- **Date:** 2026-09-07

## Context

Task 6.1 adds a reading view that renders a note without changing its source.
The workspace is written to by external agents by design (Phase 4.3), so every
note — and every resource a note references — is untrusted input. Matt set the
product direction: minimal dependencies, Obsidian-style restraint, the vault
model for local resources, and clickable hyperlinks. He asked for and accepted
security amendments on images and link dispatch.

## Decision

**Markdown subset.** Render with Qt's own CommonMark support
(`QTextDocument::setMarkdown`) plus its GitHub extensions: tables, task lists,
fenced code, strikethrough. Raw HTML is never parsed: the `MarkdownNoHTML`
feature flag is set, so embedded HTML renders as inert text. There is no
sanitiser because there is nothing to sanitise. No footnotes, no math, no
plugins in v1. Fenced code may be highlighted with KSyntaxHighlighting, which
KTextEditor already ships. No new dependency is introduced.

**Images — the vault model.** Only workspace-relative paths, resolved through
the existing `WorkspaceRoot` policy — the same code that guards the sidebar
and saves, not a second implementation. Raster formats only: PNG, JPEG, GIF,
WebP. No SVG (QtSvg's attack surface is deferred behind its own future
decision). No remote URLs (`http`, `https`, protocol-relative), no absolute
filesystem paths, no `file://`, no `data:` URIs. A file over 10 MiB or an
image over 8192 pixels on a side degrades to a named placeholder rather than
rendering. Every refused resource shows a placeholder; none aborts rendering.

**Links.** On click, by scheme:

- `http` / `https` — open in the browser via `QDesktopServices::openUrl`.
  The URL never passes through a shell.
- Relative links to Markdown inside the root — open in Omanotes through the
  existing `file.open` command, exactly like a sidebar activation.
- Everything else — `file:`, `data:`, `javascript:`, `mailto:`, custom
  schemes — refused with a status-line message naming the scheme. `xdg-open`
  dispatches arbitrary schemes to arbitrary registered handlers, which would
  let a hostile note launch an application with chosen arguments.

Hovering a link shows its real target in the status line, because link text
and destination can disagree.

**Determinism and isolation.** The same note renders identically every time.
The reading view performs no network access under any input, never writes,
and never mutates the editor document — it is a projection. Resource loading
is intercepted (`loadResource`) so the policy holds even where Qt would
default to fetching.

## Consequences

- The malicious fixture set in `tests/fixtures/markdown/` encodes this policy;
  block 6.1.4 verifies against it and release builds must not weaken it.
- Notes written for Obsidian with vault-relative images and links behave as
  expected; notes relying on embedded HTML, SVG, or remote images render as
  text and placeholders rather than breaking.
- The scheme allowlist means a link can never do more than open the browser
  or another note. Loosening it (e.g. `mailto:`) is a deliberate future edit
  to this ADR, not a code tweak.
- Image caps trade completeness for a bounded memory ceiling on hostile input.

## Alternatives

- **cmark/md4c dependency** — a more complete dialect, but a new parser
  dependency for features v1 does not need. Rejected for restraint.
- **HTML sanitisation** — parse-and-filter is fragile and historically the
  source of renderer CVEs. Never parsing HTML is strictly safer. Rejected.
- **Unrestricted `xdg-open` on click** — the original "full stop" instinct.
  Rejected for the scheme-handler attack surface; Matt accepted the allowlist.
- **SVG support** — deferred, not rejected; requires its own decision with
  QtSvg's CVE history on the table.
