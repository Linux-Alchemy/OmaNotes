# ADR 0002: Native Package and Companion Plugin Boundary

- **Status:** Accepted
- **Date:** 2026-08-29

## Context

The eventual destination includes GitHub, Omarchy users, and—where appropriate—the Omarchy plugin directory. Current Omarchy shell plugins are git repositories containing a root `manifest.json` and QML entry points. The plugin installer clones and validates them but deliberately does not build or install native applications.

## Decision

Keep this repository focused on the native Omanotes application. Package it using Arch conventions and pursue AUR and/or Omarchy Package Repository distribution only after testing and explicit release approval.

If Omanotes later has meaningful shell integration, create a separate QML companion-plugin repository. That plugin may provide a launcher, quick capture, recent-note surface, or status integration, and must document the native `omanotes` package as an external dependency.

## Consequences

- The application repository will not carry a fake shell-plugin manifest merely to appear in a directory.
- Packaging work must remain compatible with clean Arch package builds and current Omarchy conventions.
- The companion plugin is optional future scope and requires a separate proposal and approval.
- Publication to GitHub, AUR, the Omarchy Package Repository, or a plugin directory is never automatic.
