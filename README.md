# OmaNotes

> **Placeholder.** This README is a skeleton. Writing it properly is block 8.3.1 of `PLAN.md`
> and is the last thing to happen before the project is called done. Until then, `PLAN.md`,
> `OUTLINE.md`, and `docs/` are the documentation.

A keyboard-first Markdown note editor for Omarchy, built on Qt 6 Widgets and KDE's
KTextEditor with its Vi mode. Notes stay ordinary `.md` files in ordinary directories.

## Status

Under construction. Phases 1 to 7 of the build plan are accepted; Phase 8 (hardening,
packaging, documentation) is in progress.

## Building

Requires Qt 6.8+, KF6 TextEditor 6.9+, Extra CMake Modules, CMake 3.28+, Ninja, and Clang.
Versions the gates last passed on are in `docs/development-baseline.md`.

```sh
cmake --preset release && cmake --build --preset release
./build/release/src/omanotes .
```

## Installing

Packaging is block 8.2.2. Until it lands there is no install route; run the binary from the
build directory as above.

## Licence

MIT. See `LICENSE`.
