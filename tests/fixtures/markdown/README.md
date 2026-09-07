# Markdown rendering fixtures

The input side of ADR 0009. Every file here is fed to the reading view by
`tests/integration/markdown_render_test.cpp` (block 6.1.4). Expected outcomes
are stated per file; the common invariants for **every** fixture are:

- no process launch, no network access, no read outside the workspace root
- the editor document and its modified state are untouched
- rendering terminates and the app stays responsive

## Committed fixtures

| File | Contains | Expected |
|---|---|---|
| `supported-basics.md` | The supported subset: headings, emphasis, lists, task lists, tables, fenced code, blockquote, an https link, a relative note link, a local image | Renders fully; `assets/tiny.png` displays; links are clickable per policy |
| `hostile-raw-html.md` | `<script>`, `<iframe>`, `<img onerror>`, inline event handlers, HTML entities | HTML renders as inert text; nothing executes, nothing is hidden |
| `hostile-remote-resources.md` | `http`, `https`, and protocol-relative image URLs | Placeholders; zero network activity |
| `hostile-traversal.md` | `../` and absolute-path images, `file://` links and images | Placeholders and refused links; no out-of-root read |
| `hostile-schemes.md` | `javascript:`, `data:` (link and image), `vscode://`, `ssh://`, `magnet:`, `mailto:` | Every link refused with a status message naming the scheme; data: image shows a placeholder |
| `malformed.md` | Unclosed emphasis and fences, broken link syntax, mixed line endings, stray control characters | Renders best-effort as text; no crash, no hang |

## Generated at test runtime

These cannot or should not live in the repository; the test creates them in a
temporary workspace:

- **Symlink escape** — `assets/escape.png` symlinked to a file outside the
  root. Expected: placeholder; the target is never opened. (Symlinks do not
  survive archives and check out inconsistently, so it is built fresh.)
- **Decompression bomb** — a valid PNG whose pixel dimensions exceed the
  8192px cap, and a file larger than the 10 MiB cap. Expected: placeholder
  for each, bounded memory. (Multi-megabyte binaries do not belong in git.)
- **Pathological text** — a single multi-megabyte line, 10,000-row table,
  deeply nested blockquotes. Expected: rendering completes or truncates
  visibly; the UI thread is not wedged indefinitely.

`assets/tiny.png` is a hand-made 1×1 transparent PNG (68 bytes) for the happy
path — versioned, non-sensitive test data per the plan's fixtures policy.
