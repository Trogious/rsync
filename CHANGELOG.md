# Changelog

All notable changes to the Windows native port. Upstream rsync changes are
tracked in `NEWS.md` (inherited from upstream).

## Unreleased

### Phase 0 — Fork & directory skeleton
- Forked upstream `RsyncProject/rsync` at tag `v3.4.2`.
- Created `win32-port` work branch and `upstream-tracking` mirror branch
  from `v3.4.2`.
- Added `upstream` git remote (push disabled).
- Added gnulib submodule at `third_party/gnulib`, pinned to
  `62d5d65f1dd82e0a69e86e2c3848f0d6280ba19d` (2026-05-10).
- Created skeleton directories: `win32/`, `vcpkg/`, `scripts/`,
  `.github/workflows/`.
- Wrote `PORTING.md`, `BUILD.md`, `KNOWN-ISSUES.md`, this `CHANGELOG.md`.
