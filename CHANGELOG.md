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

### Phase 1 — Build infrastructure (Linux portion)
- Added `vcpkg/vcpkg.json` manifest pinned to vcpkg release `2026.04.27`
  (SHA `56bb2411609227288b70117ead2c47585ba07713`); OpenSSL overridden to
  `3.6.1#3` (CVE-2026-34054 fix).
- Added `scripts/bootstrap-windows.ps1` for one-time host setup.
- Added `scripts/gnulib-import.sh` and ran it; imported 215 gnulib `.c`
  sources and 211 `.m4` macros into `gl/` and `gl/m4/`.
- Build-system integration (configure.ac, Makefile.in rules to compile
  `gl/libgnu.a`) is deferred to Phase 2 — see PORTING.md "Build-system
  mismatch".
- Windows-only step (`vcpkg install --triplet x64-windows-static`) is
  deferred to first Windows build.
