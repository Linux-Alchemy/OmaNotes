# Dependency review

> Block 8.1.2, 2026-09-11. Refresh this table after every `omarchy update` that touches Qt, KDE
> Frameworks, or the toolchain, and re-run the six gates on the result.

## What OmaNotes declares

| Dependency | Floor in CMake | Installed today | Why |
| --- | --- | --- | --- |
| Qt 6 (Widgets, Test) | 6.8 | qt6-base 6.11.2-3 | The toolkit; ADR 0001 |
| KF6 TextEditor | 6.9 | ktexteditor 6.29.0-1 | The editor component and its Vi mode; ADR 0001 |
| Extra CMake Modules | none | 6.29.0-1 | Build-time only, pulled by KF6 |
| CMake, Ninja | 3.28 | 4.4.3-2, 1.13.2-3 | Build |
| Clang | none | 22.1.8-1 | The pinned compiler in every preset; GCC 16.2.1 is present but unused |
| glibc | none | 2.44 | `_FORTIFY_SOURCE=3` needs 2.34 or newer |

Nothing is vendored. The two syntax themes under `src/editor/themes/` are MIT-licensed
data derived from Breeze, with their headers intact.

## What actually loads

The release binary links two libraries directly and 138 shared objects transitively, all
through KF6 TextEditor. The ones that matter to the threat model, because they are attack
surface the application never asked for:

- `libQt6Qml`: a JavaScript engine, used by KTextEditor for indenters and command scripts.
  OmaNotes never feeds it script from a note (threat model T-E2).
- `libQt6Network`, `libKF6KIOCore/Widgets/Gui`: network-capable I/O. OmaNotes has no
  network code and never hands KTextEditor a URL (S1, S4); the property rests on not calling
  those APIs, not on a sandbox.
- `libQt6DBus`, `libKF6GlobalAccel`, `libKF6Notifications`: IPC the application does not
  register on.
- `libKF6AuthCore`: KAuth, the polkit helper mechanism KTextEditor uses for "save as root".
  Unreachable because the application owns `:w` and never enters KTextEditor's save path.
- `libKF6Crash`: crash handling; may hand a core to DrKonqi on a `qFatal`.

The full list is `ldd build/release/src/omanotes`.

One observation from the fuzz harnesses (block 8.1.2): under AddressSanitizer, Qt's JSON parser
trips the allocation-mismatch check on any object with more than one key. The temporary buffer
`std::stable_sort` uses to sort keys is, in GCC 16's libstdc++ as compiled into `qt6-base
6.11.2-3`, taken with `operator new` and returned with `free`. It is not reachable from
OmaNotes code and not something this project can fix; the harnesses run with
`alloc_dealloc_mismatch=0` and every other check on. Worth re-checking after the next Qt or GCC
rebuild, since it will either vanish or become a report worth filing upstream.

## How an advisory would be noticed

There is no automated feed. The route is:

1. Arch's security tracker, <https://security.archlinux.org>, filtered to `qt6-base`,
   `qt6-declarative`, `ktexteditor`, `kio`, `kauth`, and `clang`.
2. `arch-audit`, when installed, lists installed packages with open CVEs in one command. It
   is not installed on the baseline machine today; installing it is a one-line decision for
   Matt, not a project dependency.
3. Omarchy's own update cadence rolls all 138 objects; after an update, the six gates run
   and the security check confirms the release binary still carries its hardening.

## Policy

- Floors are set in `CMakeLists.txt` and are the only pins. There is no lockfile because
  Arch has no such thing; the package built in 8.2 declares the same floors.
- Adding or upgrading a dependency needs Matt's approval before the change (plan protocol,
  item 4). Transitive changes arrive with the distribution and are reviewed by re-running
  the gates.
- The baseline table in `docs/development-baseline.md` records the versions the gates last
  passed on; it was refreshed on 2026-09-11 with this review.
