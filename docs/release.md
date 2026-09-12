# Licence and releases

> Block 8.3.3. What OmaNotes is licensed under, and the exact steps to cut a tagged source
> release on GitHub when Matt approves one. No tag exists yet, and the Phase 8 checkpoint says
> he approves any tagged release separately from the daily-use trial.

## Licence

MIT, `LICENSE` at the repository root, copyright Linux-Alchemy (ADR 0014). It applies to
everything in the repository, the icon included. The package installs the licence file to
`/usr/share/licenses/omanotes-git/LICENSE`, where Arch policy wants it.

What the binary links is not the repository's to license: Qt 6 and KDE Frameworks 6 are used
under their own terms, dynamically linked, and are declared as runtime dependencies rather
than bundled. The two editor colour themes under `src/editor/themes/` are derived from KDE's
and carry the original copyright lines in the files themselves. `docs/dependency-review.md`
lists the closure.

## Versioning

The version lives in one place, the `VERSION` argument of `project()` in `CMakeLists.txt`,
currently `0.1.0`. Everything else reads it from there:

- The `-git` package appends the commit count and short hash: `0.1.0.r61.ga807572`.
- A tag names the same number with a `v` prefix: `v0.1.0`.

Bump it only on a release branch, never on a feature branch, so the number and the tag are
always cut from the same commit.

## Cutting a tagged source release

The repository is the distribution (ADR 0013): a release is a tag on `main` and a GitHub
release page that points at it. GitHub generates the source tarball from the tag; nothing is
uploaded and no binaries are attached.

1. **Matt approves the release.** The checkpoint requires it; nothing below happens without
   it.
2. **Branch `release/v0.1.0` from `main`.** If the number needs to change, change it in
   `CMakeLists.txt` and nowhere else. Add a Change Log entry to `PLAN.md` saying the release
   is being cut and what it contains, in the same voice as the rest of the log.
3. **Run the full gate on the branch** and record the output in the pull request, as any
   block does: dev build and tests under the sanitizers, release build and tests, format
   check, clang-tidy, security check, and a package build from a fresh clone with `makepkg`
   so the check step runs the release tests on the packaged binary.
4. **Open the PR, Matt merges it.** The merge commit on `main` is the release commit.
5. **Tag that commit, annotated, and push the tag:**

   ```sh
   git checkout main && git pull
   git tag -a v0.1.0 -m "OmaNotes 0.1.0"
   git push origin v0.1.0
   ```

6. **Create the GitHub release from the tag**, with notes drawn from the Change Log since the
   previous tag (for the first release, since the project began):

   ```sh
   gh release create v0.1.0 --title "OmaNotes 0.1.0" --notes-file release-notes.md
   ```

   The notes say what changed, what is known not to work (`docs/limitations.md`), and how
   to install (`docs/packaging.md`). No binaries are attached.
7. **Verify from the outside.** In a scratch directory, download the tarball GitHub made,
   unpack it, and follow `docs/packaging.md` route 2 from it; then clone the repository at
   the tag and follow route 1. Both must reach a working `omanotes` without any step that
   is not written down. That is block 8.3.4's contract applied to the tag.
8. **Optionally, a tarball PKGBUILD.** The `-git` package keeps tracking `main`. A plain
   `omanotes` package that builds the tagged tarball can sit beside it as
   `packaging/arch/omanotes/PKGBUILD`, with `source` pointing at
   `https://github.com/Linux-Alchemy/OmaNotes/archive/refs/tags/v0.1.0.tar.gz` and its
   checksum filled in by `updpkgsums`. It cannot be written before the tag exists, because
   the checksum needs the tarball. Lint it with `namcap` as 8.2.2 did.

## What a release is not

- Not a submission to the AUR or the Omarchy Package Repository, and not a plugin. Those are
  deferred, not rejected (ADR 0013); this document is what such a submission would start
  from.
- Not a promise of upgrade paths between session-format versions beyond what
  `docs/session-format.md` states. The format is versioned; readers refuse newer documents.
- Not a change to the `-git` package's behaviour. Users on it keep getting `main`.

## Between releases

`main` is always releasable: every merge has passed the full gate, and the package builds
from any commit of it. A tag adds a name and a page, not a quality level.
