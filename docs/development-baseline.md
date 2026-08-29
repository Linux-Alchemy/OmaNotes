# Development Baseline

> Captured on 2026-08-29 from the primary Omanotes development machine.

## Target environment

| Component | Tested baseline |
| --- | --- |
| Distribution | Omarchy 4.0.1-1 |
| Session | Wayland on Hyprland |
| Kernel | Linux 7.1.9-arch1-2 |
| Qt | qt6-base 6.11.2-2; qt6-declarative 6.11.2-1 |
| KTextEditor/KF6 | 6.29.0-1 |
| CMake | 4.4.2-1 |
| Ninja | 1.13.2-3 |
| Clang toolchain | 22.1.8-1 |
| GCC toolchain | 16.2.1+r23+gd564253eb6c8-1 |
| Extra CMake Modules | 6.29.0-1 |

These are the versions used to validate the initial walking skeleton. They are not yet the minimum supported versions; release minimums will be derived from the target Omarchy packaging environment after the KTextEditor integration is proven.

## Approved development dependencies

Phase 1 received explicit approval to install:

```text
cmake
ninja
ktexteditor
extra-cmake-modules
```

Qt 6, Clang, and GCC were already present. Dependency additions and upgrades remain approval-gated. Omanotes does not vendor Qt, KDE Frameworks, or toolchain binaries.

## Canonical local checks

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target clang-tidy
cmake --build --preset dev --target security-check
```

The development preset uses Clang, strict warnings, AddressSanitizer, and UndefinedBehaviorSanitizer. The release preset retains compiler and linker hardening without sanitizers.

CTest disables LeakSanitizer because the Codex command sandbox traces child
processes, which LeakSanitizer cannot inspect safely. AddressSanitizer and
UndefinedBehaviorSanitizer remain active in the automated test harness. Leak
detection is exercised when the development binary is launched directly outside
the traced harness during the manual checkpoint.

## Distribution constraint

Omanotes is a native desktop application and will follow Arch package conventions, with AUR and the Omarchy Package Repository as intended distribution routes. Omarchy shell plugins are QML repositories loaded into the long-running Quickshell process; they do not install native packages. Any future `omarchyplugins.com` entry must therefore be a separate, useful QML companion plugin that documents Omanotes as an external dependency.
