# ADR 0001: Platform and Toolkit

- **Status:** Accepted
- **Date:** 2026-08-29

## Context

Omanotes must feel native on Omarchy, make Vim-style editing foundational, and avoid implementing a text editor engine. The first release targets Arch Linux, Wayland, and Hyprland as shipped by Omarchy Quattro.

## Decision

Use C++20, Qt 6 Widgets, CMake, and KDE Frameworks 6 KTextEditor. Keep application policy outside the editor component behind a narrow adapter introduced in Phase 2.

The initial build uses Clang with strict warnings and sanitizers. GCC remains a compatibility compiler to be exercised before release.

## Consequences

- KTextEditor supplies the mature Vi input mode, document model, selection, undo, and native text-input behaviour.
- Qt supplies the window, filesystem-facing UI, status, tabs, and eventual Markdown reading view.
- Omanotes accepts KDE Framework runtime dependencies rather than copying editor functionality.
- Qt Widgets is preferred over QML for the native application because KTextEditor is a QWidget component.
- Other operating systems and display stacks are outside the initial scope.

## Rejected alternatives

- A custom editor engine: excessive complexity and security risk.
- Embedded Neovim: authentic behaviour at the cost of process/RPC/rendering complexity.
- CodeMirror in a webview: visually flexible but weaker native integration and a second application stack.
- GTKSourceView: credible, but its Vim input layer is less comprehensive for the project's primary requirement.
