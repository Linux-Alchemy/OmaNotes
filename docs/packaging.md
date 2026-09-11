# Packaging

> Block 8.2.2. Distribution is the GitHub repository (ADR 0013): a package built from a clean
> clone is the primary route, `cmake --install` the fallback. AUR and Omarchy Package Repository
> submission are deferred.

## What an install ships

Four files, nothing else. Tests, fuzzers, and build output never leave the build tree.

| File | Purpose |
| --- | --- |
| `/usr/bin/omanotes` | The application |
| `/usr/share/applications/OmaNotes.desktop` | Launcher entry; its name matches the Wayland app_id the binary declares |
| `/usr/share/icons/hicolor/scalable/apps/OmaNotes.svg` | Icon |
| `/usr/share/licenses/omanotes-git/LICENSE` | MIT (ADR 0014); Arch policy puts it under the package name |

Runtime dependencies are `qt6-base`, `ktexteditor`, and `hicolor-icon-theme`. Everything else
KTextEditor pulls in (`docs/dependency-review.md`) arrives through `ktexteditor`.

## Route 1: the package

The PKGBUILD lives in `packaging/arch/` and follows the Arch `-git` convention: it clones the
repository afresh and builds the tip of `main`, so what you have checked out locally, including
uncommitted changes, is never what gets packaged. The version is
`<CMake version>.r<commit count>.g<short hash>`, read from `CMakeLists.txt` and git.

```sh
git clone https://github.com/Linux-Alchemy/OmaNotes.git
cd OmaNotes/packaging/arch
makepkg -si
```

`-s` installs the build dependencies (`git`, `cmake`, `ninja`, `clang`, `extra-cmake-modules`);
`-i` installs the result. The build has three stages, and the middle one is the point:

1. **build**: the `release` preset, Clang, with every hardening flag from `cmake/Warnings.cmake`
   plus makepkg's own.
2. **check**: the full test suite runs against the release build, offscreen, and the security
   check confirms the hardening marks on the binary about to be packaged. About fifteen seconds.
   A failure means no package. `makepkg --nocheck` skips it; do not.
3. **package**: `cmake --install` into the staging directory, then pacman takes over.

**Upgrade:** `git pull` in the clone, then `makepkg -si` again in `packaging/arch`. makepkg
notices the new commit through `pkgver()`, and pacman replaces the old package's files.

**Uninstall:** `sudo pacman -R omanotes-git`. pacman removes only the four files above. Your
notes are ordinary Markdown files it has never heard of, and the state under
`~/.local/state/omanotes/` and `~/.config/omanotes/` is not owned by the package, so it stays.
Delete those two directories yourself if you want a clean slate.

**Working state:** makepkg leaves `src/`, `pkg/`, a bare clone, and the built package in
`packaging/arch/`. They are gitignored. `makepkg -c` cleans up after itself.

A `-debug` companion package with the symbols is produced alongside; that is the default on
Arch and is optional to install.

## Route 2: without a package

For anyone who would rather not have pacman own it. Same files, same places, no record of what
was installed, so removal is by hand.

```sh
cmake --preset release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build --preset release
QT_QPA_PLATFORM=offscreen ctest --preset release --output-on-failure
sudo cmake --install build/release
```

The licence lands under `share/licenses/omanotes/` by default; `-DOMANOTES_LICENSE_DIR` moves
it.

## Lint

`namcap packaging/arch/PKGBUILD` and `namcap <the built package>` are the block's evidence
that the package follows Arch conventions. Both are run before a packaging change is merged.

## Not yet

- **Tagged releases.** There are no tags, so the package tracks `main`. When 8.3.3 cuts the
  first tag, a plain `omanotes` PKGBUILD with a tarball source can sit beside this one.
- **AUR.** This PKGBUILD is what an AUR submission would start from, if that is ever wanted.
