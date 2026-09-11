# ADR 0014: MIT licence

- **Status:** Accepted (2026-09-11, Matt's ruling at the pause before Task 8.2)
- **Resolves:** `OUTLINE.md` open question 7 (select the public licence before the first
  public release)
- **Related:** ADR 0013 (distribution is the GitHub repository)

## Context

The outline deferred the licence to "before the first public release". Under ADR 0013 the
public repository is the release, and it was public with no licence file, which in law means
all rights reserved: nobody could legally use, build, or modify what they cloned.

The stack constrains the choice only loosely. Qt 6 is used under the LGPL v3 and KDE
Frameworks 6 under the LGPL 2.1 or later, both dynamically linked, which any licence
satisfies as long as relinking stays possible; dynamic linking gives that for free. The two
syntax themes under `src/editor/themes/` are MIT-licensed data derived from Breeze with their
headers intact.

## Decision

OmaNotes is licensed under the MIT License. `LICENSE` at the repository root carries the text,
copyright Matt Klimo, 2026. Contributions are accepted under the same licence. Source files
do not need per-file headers; the root file governs.

## Consequences

- Anyone may use, modify, and redistribute the application, including commercially, with
  attribution. That is intended.
- The PKGBUILD declares `license=('MIT')` and, per Arch packaging policy for MIT, installs
  `LICENSE` to `/usr/share/licenses/omanotes/`. `cmake --install` does the same.
- The theme data's own MIT attribution stays in its headers; the licences are the same, so
  nothing else is needed.
- Any future contribution of GPL-only code cannot be accepted without relicensing the whole,
  so it will not be accepted.

## Rejected alternatives

- **GPL-3.0:** copyleft was not asked for and nothing in the stack requires it; it would also
  put the project under a stricter licence than the theme data it carries.
- **Apache-2.0:** the patent grant and the longer text buy nothing for a desktop note editor.
- **No licence:** the status quo, and the worst option: a public repository nobody may
  legally use.
