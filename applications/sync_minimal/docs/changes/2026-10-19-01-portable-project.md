# Change: Publish the synchronization prototype as a buildable project

- Date: 2026-10-19
- Type: Project packaging
- Related metric: `docs/spec.md`
- Commit: pending

## 1. Previous state

The source and coordinator/responder configurations were kept under a local
diagnostic workspace path. The build script used machine-specific paths.

## 2. Approach

Keep the tested source and role configurations, make build paths configurable,
and document the actual evidence boundary.

## 3. Changes

- Added this application directory with coordinator/responder configurations.
- Changed the build helper to use the repository board root and temporary
  output paths.
- Added build and verification documentation.

## 4. Validation

- Source and role configuration copied from the 2026-10-18 diagnostic build.
- Coordinator and responder both clean-built from the repository path on
  2026-10-19; no hardware was programmed during packaging.

## 5. Limitations

The TX1 dual-responder path and common-instrument GPIO edge measurement remain
unverified.
