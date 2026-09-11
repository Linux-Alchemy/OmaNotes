# ADR 0013: Distribution is the GitHub repository

- **Status:** Accepted (2026-09-11, Matt's ruling at the pause before Task 8.2)
- **Supersedes:** ADR 0002 (native package and companion plugin boundary)
- **Related:** ADR 0014 (licence), `PLAN.md` Task 8.2

## Context

ADR 0002 fixed the destination as an Arch package submitted to the AUR and the Omarchy Package
Repository, with an optional QML companion plugin for `omarchyplugins.com`, and the plan's
Task 8.2 was written to that: revalidate the submission routes, check the name, build in an
isolated packaging environment fit for a public repository.

At the pause after Task 8.1, Matt stated the actual goal: the application is for his own daily
use, it lives on his public GitHub repository, and anyone who wants it should be able to clone
the repository and install it. Submission to the AUR, the Omarchy Package Repository, or a
plugin directory is "maybe later", not a requirement of the first usable release.

The repository was already public at that point with no licence, no README, no install rules,
and no package, so "clone and install" was not yet true of it.

## Decision

The public GitHub repository is the distribution. Concretely:

- A `PKGBUILD` under `packaging/arch/` is the primary install route. A user clones the
  repository and runs `makepkg -si`; pacman owns the installed files, and `pacman -R`
  removes them cleanly. The package is built from a clean clone, never from a working tree.
- `cmake --install` on the release preset is the fallback for anyone who does not want a
  package. Both routes install the same files: the binary, a desktop entry, an icon, and the
  licence.
- Tagged releases on GitHub are source tags. Prebuilt binaries are not a supported route.
- The Omarchy-first principle stands: the package targets Omarchy's current package set and
  the floors in `CMakeLists.txt`; nothing else is tested.
- AUR submission, Omarchy Package Repository submission, and a companion plugin are deferred,
  not rejected. Each needs its own approval if it is ever wanted, and the PKGBUILD written here
  is the one such a submission would start from.

## Consequences

- Task 8.2 is reshaped: the revalidation and name-availability block becomes a decision block
  (this ADR and ADR 0014); the isolated packaging environment becomes a clean clone plus a lint,
  which needs no new dependency; install, launch, upgrade, and uninstall on Matt's machine stay
  exactly as planned, because they are the point.
- The README becomes the front door of the distribution and is written last, when the
  application is complete (Task 8.3).
- The name `omanotes` needs only to be free of collisions on the user's system, which it is
  in the official repositories today; wider availability checks wait for any future submission.
- Maintaining a public package on a rolling distribution, and answering for it, is not taken
  on. Anyone who installs from the repository builds against their own Qt and KTextEditor.
- The plan's Phase 8 checkpoint keeps the daily-use trial and its no-publication rule; "public
  release step" now means a tagged release at most.

## Rejected alternatives

- **AUR submission now:** nobody has asked for it, and it commits the project to maintaining
  a public package against every Qt and KTextEditor rebuild. The PKGBUILD makes it cheap
  later.
- **Prebuilt release binaries:** a binary linked against this week's Qt and KF6 breaks at the
  next Omarchy update. Source tags do not.
- **Flatpak or AppImage:** against the Omarchy-first principle, and the KDE Frameworks runtime
  weight is out of proportion for a single-user tool that already lives in Arch's package set.
- **Leaving ADR 0002 as it stood:** it would keep the plan gating work on a destination nobody
  intends to reach yet.
