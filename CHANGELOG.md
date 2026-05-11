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

### Phase 7 — CI workflows
- `.github/workflows/build.yml`: builds rsync.exe on `windows-latest`
  GitHub runner (MSVC + MSYS2 shell + vcpkg x64-windows-static),
  verifies static linkage with `dumpbin`, runs smoke tests, packages
  a `.7z` artifact, publishes a GitHub Release on `v*` tags.
- `.github/workflows/upstream-sync.yml`: weekly cron that mirrors
  upstream `RsyncProject/rsync` master to our `main` branch and
  opens a PR against `win32-port` if upstream advanced.
- `scripts/verify-static.ps1`: dumpbin-based allowlist check against
  forbidden runtimes (cyg*, msys-*, vcruntime*, etc.).
- `scripts/smoke-test.ps1`: `--version`, `--help`, local copy
  round-trip, daemon/rsync:// rejection checks.
- DEFERRED: actually running the workflow on GitHub Actions (needs
  the branch pushed to origin); CVE-watch workflow (mentioned in
  PLAN.md directory layout but no spec details yet).

### Phase 6 — Daemon excision (Linux portion)
- `options.c::parse_arguments` case `OPT_DAEMON`: on Windows, error +
  exit with `RERR_UNSUPPORTED`. Rejects `--daemon`, `--config`,
  `--dparam`, `--no-detach`.
- `options.c::check_for_hostspec` URL branch: on Windows, error +
  exit before parsing `rsync://` URLs.
- `clientserver.c`, `loadparm.c`, `access.c`, `authenticate.c`:
  entire file body wrapped in `#ifndef WIN32_NATIVE` ... `#endif`.
  Compiles to empty translation units on Windows.
- `socket.c::start_accept_loop`: wrapped in `#ifndef WIN32_NATIVE`.
  Rest of socket.c still compiles for utility use.
- `win32/stub_daemon.c`: fixed `start_accept_loop` return type
  (void, matches upstream); now calls `exit_cleanup(RERR_UNSUPPORTED)`.
- Linux full rebuild verified (all daemon files compile normally on
  Linux — wraps are inert).

### Phase 5 — SSH integration (Linux portion)
- `win32/win_ssh.c::win_default_rsh`: real implementation. Lookup
  order: `$RSYNC_RSH` → `%SystemRoot%\System32\OpenSSH\ssh.exe`
  (if present) → `ssh.exe` (PATH fallback).
- `main.c`: in the remote-shell-cmd discovery block, use
  `win_default_rsh()` on Windows instead of `RSYNC_RSH` macro.
  Effect: a user with neither `RSYNC_RSH` nor `-e` set gets the
  built-in Windows OpenSSH client automatically.
- Linux regression: clean rebuild verified.

### Phase 4 — Filesystem & path handling (Linux portion)
- `options.c::check_for_hostspec`: Windows branch returns NULL (local
  path) for drive-letter (`C:\..`, `C:/..`) and UNC (`\\..`) paths.
  Fixes the cwRsync colon-parsing bug.
- `rsync.h`: on Windows, unconditionally define `SUPPORT_LINKS` and
  `SUPPORT_HARD_LINKS`; override `do_readlink` macro to call
  `win_readlink` (gnulib's readlink is a stub on Windows).
- `syscall.c::do_symlink`, `do_link`, `do_chmod`: Windows branches
  call `win_symlink`/`win_link`/`win_chmod`. `do_link` gate extended
  to include `WIN32_NATIVE`.
- `win32/win_paths.c`: implemented `win_long_path_prefix` (resolves
  to absolute, adds `\\?\` or `\\?\UNC\` prefix).
- `win32/win_fs.c`: real implementations of `win_chmod` (readonly
  attribute), `win_symlink` (CreateSymbolicLinkA with unprivileged
  flag), `win_readlink` (GetFinalPathNameByHandle on the symlink),
  `win_link` (CreateHardLinkA), `win_utimens` (SetFileTime).
- `win32/win_compat.h`: typedefs for `mode_t` and `ssize_t`
  (MSVC CRT lacks them).
- `win32/rsync.manifest`: enables long-path-aware + UTF-8 active code
  page. Embedded into rsync.exe via mt.exe in build-windows.ps1.
- DEFERRED: actual Windows testing of symlink permission paths,
  symlink-to-dir handling (currently always creates file-symlink),
  utime nanosecond resolution.

### Phase 3 — Fork emulation (Linux portion, 3 of 4 sites)
- `pipe.c::piped_child`: Windows branch calls `win_spawn_remote_shell`
  (gnulib `create_pipe_bidi` — spawns ssh.exe with bidirectional pipes).
- `pipe.c::local_child`: Windows branch calls
  `win_reexec_self_as(WIN_ROLE_LOCAL_CHILD, ...)`.
- `main.c::main`: at the top, calls `win_child_init` to dispatch
  re-exec'd children before normal startup.
- `main.c::shell_exec`: Windows branch calls `system(cmd)` (no fork on
  Windows).
- `win32/win_spawn.c`: real implementation using `gl/spawn-pipe.h`.
- `win32/win_reexec.c`: CreateProcess machinery + binary state-file
  serialization + handle inheritance. Supports LOCAL_CHILD; RECEIVER /
  GENERATOR return `RERR_UNSUPPORTED`.
- `win32/win_child_init.c`: child-side marker detection
  (`--_win_child=<state>`), state load, dispatch.
- `main.c::do_recv` [DECIDE wall]: Phase 3 Site 3 cannot be completed
  in this Linux session — the receiver/generator split requires
  preserving in-memory state (`first_flist` and option globals).
  Stub-and-defer: Windows hits `RERR_UNSUPPORTED` with a clear message
  pointing at PORTING.md "do_recv state preservation". Downloads
  (FROM remote TO Windows) do not work yet; uploads do.
- Linux regression: `./rsync -av /tmp/src/ /tmp/dst/` still works.

### Phase 2 — WIN32_NATIVE guards + win32/ stubs (Linux portion)
- `configure.ac`: added `WIN32_NATIVE` detection via `AC_PREPROC_IFELSE`;
  on Windows, appends `-lws2_32 -ladvapi32 -liphlpapi -lcrypt32 -lsecur32
  -luserenv` to `LIBS`; substitutes `@WIN32_OBJS@`.
- `Makefile.in`: added `WIN32_OBJS = @WIN32_OBJS@` and included it in
  `OBJS`. Empty on Linux/macOS.
- `rsync.h`: include `win32/win_compat.h` when `WIN32_NATIVE` is defined.
- `win32/win_compat.h`: umbrella header — Windows API includes, POSIX
  type stubs (`uid_t`, `gid_t`, `pid_t`), forward `#include`s for
  `win_spawn.h`, `win_reexec.h`, `win_child_init.h`, `win_paths.h`,
  `win_fs.h`, `win_ssh.h`.
- `win32/win_*.{c,h}`: Phase 2 stubs returning `ENOSYS`. Phases 3-5
  fill them in.
- `win32/stub_daemon.c`: error stubs for `start_daemon`, `daemon_main`,
  `start_accept_loop`, `start_socket_client`. Phase 6 wraps the
  upstream daemon source in `#ifndef WIN32_NATIVE`.
- `scripts/build-windows.ps1`: orchestrates the Windows build
  (autoreconf inside MSYS2 bash, then `make rsync.exe` with MSVC).
- Linux regression test: `./configure --disable-md2man && make rsync`
  still produces a working binary.
- DEFERRED to Windows: actually compiling `rsync.exe`, `dumpbin
  /DEPENDENTS` verification, and any compile-error iteration.

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
