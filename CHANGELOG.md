# Changelog

All notable changes to the Windows native port. Upstream rsync changes are
tracked in `NEWS.md` (inherited from upstream).

## Unreleased

### Windows: threads-instead-of-fork infrastructure for do_recv (2026-05-11)
- **Approach**: replace `fork()` in `do_recv` with a thread on Windows
  (`local_child`'s thread refactor is deferred). The receiver branch
  becomes a function that runs in a freshly-spawned thread; the
  parent thread continues as the generator. The two threads share the
  process's heap, fd table, and any global that isn't marked
  `ROLE_TLS` (`__declspec(thread)`).
- **Key learning — extern declarations of TLS variables matter**.
  When a `__declspec(thread)` variable is *defined* in one .c file
  but accessed via plain `extern int foo;` in another, MSVC emits
  regular data-section access code for the access — which goes to
  the wrong memory and crashes on write. Every `extern` declaration
  needs the matching `ROLE_TLS` qualifier. Updated across cleanup.c,
  exclude.c, flist.c, generator.c, io.c, log.c, main.c, progress.c,
  rsync.c, xattrs.c.
- **Globals marked ROLE_TLS**: `am_receiver`, `am_generator`
  (main.c), `sock_f_in`, `sock_f_out` (io.c), `iobuf` (io.c file-
  static struct — the I/O dispatcher's per-role state),
  `send_msgs_to_gen`, `output_needs_newline` (log.c). `am_sender`,
  `am_server`, `filesfrom_fd`, `munge_symlinks` (options.c) are NOT
  TLS because they appear in static initializers of `long_options[]`
  via `&am_server`, and MSVC forbids `__declspec(thread)` addresses
  in static initializers. Local_child's thread refactor (deferred)
  will need a different mechanism for those.
- **New files**:
  - `win32/win_thread.c` — `win_thread_fork(fn, arg)` uses
    `_beginthreadex` (NOT `CreateThread`; the latter skips CRT
    per-thread init, causing later `fopen`/`malloc` crashes).
    Returns the thread's TID as a fake pid; the HANDLE goes into the
    same pid→HANDLE table that `win_fork` maintains, so
    `win_waitpid()` works unchanged.
  - `ROLE_TLS` macro defined in `rsync.h` (empty on POSIX,
    `__declspec(thread)` on Windows via win_compat.h).
- **`do_recv` refactor**: the receiver branch extracted into
  `do_recv_receiver()`. POSIX path: fork, child calls
  `do_recv_receiver()` inside `exit_cleanup()`. Windows path:
  `win_thread_fork(do_recv_thread_main, args)` with a brief
  `Sleep(50)` to let the new thread reach its setup before the
  parent advances. `close(error_pipe[1])` deferred on Windows until
  after `wait_process_with_flush` because both threads share the fd
  table — closing the receiver's write end early would crash it.
- **`pid == 0` infinite-sleep loop at the end of the receiver
  branch**: skipped on Windows. POSIX rsync's receiver blocks
  forever waiting for `SIGUSR2` from the generator; the generator
  does `kill(pid, SIGUSR2)` at shutdown. We can't signal a thread,
  so on Windows the receiver returns cleanly after
  `read_final_goodbye`, and the generator's `kill(pid, SIGUSR2)`
  reduces to a no-op (our `kill` shim) while
  `wait_process_with_flush` does the actual join.
- **Status**: build is clean, push regression is clean. Pull gets as
  far as the receiver thread reaching `recv_files()` and the parent
  thread reaching `generate_files()`, then the process dies — either
  one of those (or something they call) touches a non-TLS-ed global
  that races, or hits some other shared-state bug we haven't
  bottomed out yet. Tracked as continuation of task #6.

### Windows SSH-push works end-to-end (2026-05-11)
- **Outcome**: `rsync.exe -av <localdir> user@host:/path/` transfers files
  over SSH to a Linux rsync server and verifies byte-exact on both ends.
  SHA-256 of a 100 KB binary file matches local↔remote. Idempotent
  re-push transfers 0 bytes (speedup ~1200x). Tested against a Pi
  running rsync 3.2.7 / OpenSSH on a non-default port via `RSYNC_RSH`.
- **Four blockers fixed to get there:**
  1. `win32/win_select.c` (new, ~170 LOC): `select()` shim that
     classifies each fd via `GetFileType` + `GetNamedPipeInfo`. Sockets
     defer to winsock select; pipes use `PeekNamedPipe` for read-readiness
     and assume always-writable. Polls at 10 ms cadence when nothing is
     ready. Plugged in via `#define select win_select` in win_compat.h
     and added to `WIN32_OBJS` in configure.ac. Without this, every
     call site in `io.c` (the I/O dispatcher) hung forever waiting for
     winsock select to recognize anonymous-pipe fds.
  2. `win_spawn.c`: bumped `CreatePipe` buffer hint from default (~4 KB)
     to 1 MB. Avoids blocking-write deadlocks during the file-list
     phase, where the sender's outgoing flow is bursty.
  3. `util1.c::change_dir`: under WIN32_NATIVE, accept `X:\…`, `X:/…`,
     and `\\…` as absolute paths in addition to `/…`. Without this,
     rsync prepended `curr_dir` to a Windows-absolute path and tried
     to chdir to a mangled `C:\cwd/C:\src/…` string. Also normalize
     `curr_dir` to forward-slash form immediately after `getcwd()` so
     subsequent path joins don't mix separators.
  4. `syscall.c::do_open_nofollow`: (a) force `O_BINARY` into the
     `open()` flags on Windows (MSVC defaults to text mode with CRLF
     translation — breaks rsync's byte-exact contract). (b) Skip the
     post-open `l_st.st_dev != f_st.st_dev || l_st.st_ino !=
     f_st.st_ino` symlink-race check on Windows: MSVC's stat()/fstat()
     don't return stable `st_dev`/`st_ino` values for the same file
     across calls, so the check spuriously triggers `EINVAL`.
- **Not yet working / not tested:**
  - **Pull** (`rsync user@host:/src/ C:\dst\`): probably still hangs.
    The local side becomes the *receiver*, which forks generator from
    receiver in `do_recv`. That fork goes through `win_fork()` 
    (`RtlCloneUserProcess`) — same code path as local-to-local, which
    we know deadlocks.
  - **Local copy** (`rsync C:\src\ C:\dst\`): two forks (`local_child`
    + `do_recv`). Same fork issue.
  - All the path-handling fixes here are minimum-viable — they cover
    "absolute Windows path as source for SSH push". Round-tripping
    permissions, owner/group naming, and symlink targets through the
    remote isn't exercised yet.

### Windows build brought up to first working `rsync.exe` (2026-05-11)
- **Outcome**: `scripts/build-windows.ps1` produces a 5.5 MB `rsync.exe`
  that passes `verify-static.ps1` (no Cygwin / MSYS / vcruntime DLLs;
  only ws2_32, advapi32, crypt32, user32, kernel32). `--version`,
  `--help`, `--daemon` rejection, and `rsync://` rejection all work.
- **Known regression**: local-to-local `rsync -av src/ dst/` hangs.
  Two `rsync.exe` processes spawn via `win_fork()` (RtlCloneUserProcess)
  and never make progress. Diagnosis deferred — likely the clone is
  fragile against modern Windows loader-lock state or our pipe IPC
  setup mismatches the cloned child's view of the FD table. Tracked as
  a follow-up; remote-via-ssh path is independent and untested here.
- **gnulib dropped from the build** (PORTING.md "Build-system
  mismatch" — resolved by option (d): replace the one consumer of
  `gl/spawn-pipe.h::create_pipe_bidi` with a ~60 LOC native
  CreateProcess + CreatePipe in `win32/win_spawn.c`). Avoids the
  4447-line `gl/Makefile.am`, `.in.h` m4 substitution, and gnulib's
  automake-only build pipeline. `gl/` submodule remains in the tree
  but is no longer compiled.
- **POSIX shim layer** (`win32/win_compat.h`) expanded from ~200 to
  ~430 lines: `struct passwd` / `struct group` / `struct direct`
  stubs (Windows has no /etc/passwd); `S_*` mode-bit and `S_IS*`
  predicate macros; POSIX fcntl constants + `struct flock`;
  `DIR`/`opendir`/`readdir`/`closedir` typedef backed by
  FindFirstFile in `win_fs.c`; `gettimeofday` via
  `GetSystemTimeAsFileTime`; `strcasecmp`/`strncasecmp` →
  MSVC's `_stricmp`/`_strnicmp`; `kill(pid, 0)` via
  `OpenProcess + GetExitCodeProcess`; `localtime_r` arg-order
  adapter; `pipe`/`fsync`/`alarm` shims; syslog no-op stubs;
  `major`/`minor`/`makedev` macros; `R_OK`/`W_OK`/`F_OK` constants;
  SIGALRM/SIGCHLD/SIGUSR1 etc. numeric defines; `WIFEXITED`/
  `WEXITSTATUS` decoders for our 0xFF00-encoded exit status from
  `win_waitpid`.
- **Daemon-only symbol stubs** (`win32/stub_daemon.c`): added
  `module_id`, `read_only`, `module_dir`, `module_dirlen`,
  `auth_user`, `full_module_path`, `undetermined_hostname`, plus the
  `lp_*` query functions (`lp_pid_file`, `lp_log_file`,
  `lp_syslog_facility`, `lp_refuse_options`, `lp_write_only`,
  `lp_reverse_lookup`, `lp_dont_compress`, `lp_name`,
  `lp_ignore_nonreadable`, `lp_munge_symlinks`, `lp_numeric_ids`,
  `lp_use_chroot`), `start_inband_exchange`, `set_env_num`,
  `reset_daemon_vars`, `set_dparams`, `base64_encode`,
  `namecvt_call`, `daemon_chmod_modes`. Signatures match `proto.h`
  exactly. All return harmless "client-mode" values so the linker
  resolves them but the daemon paths never actually run.
- **`AH_BOTTOM` injection** (`configure.ac`) makes every translation
  unit that includes `config.h` automatically pick up
  `win32/win_compat.h` on Windows. Without this, `lib/snprintf.c` and
  the popt files (which don't include `rsync.h`) miss the shims.
- **Autoconf cache pre-seeding** (`configure.ac`): rsync's configure
  hard-codes `<sys/socket.h>` / `<netdb.h>` in many type/feature
  probes, which always fail on MSVC. We pre-seed
  `ac_cv_type_socklen_t`, `ac_cv_type_struct_addrinfo`,
  `ac_cv_type_struct_sockaddr_storage`, `ac_cv_func_getaddrinfo`,
  `ac_cv_func_inet_ntop`/`inet_pton`, `ac_cv_func_sigaction`/
  `sigprocmask`, `ac_cv_func_memmove`/`strpbrk`/`getcwd`,
  `ac_cv_func_waitpid`/`fork`, `rsync_cv_HAVE_GETADDR_DEFINES`,
  `rsync_cv_HAVE_GETTIMEOFDAY_TZ`, etc. so the m4 probes
  short-circuit to the right answer instead of failing the test
  compile.
- **POSIX-header gating** in upstream files (one-line
  `#ifndef WIN32_NATIVE` around each unconditional include):
  `rsync.h` (pwd.h, grp.h, netinet/in.h, arpa/inet.h, syslog.h,
  dirent/direct alias), `socket.c` (netinet/tcp.h), `popt/system.h`
  (unistd.h), `popt/popt.c` (unistd.h), `popt/poptconfig.c`
  (unistd.h), `popt/popthelp.c` (sys/ioctl.h via POPT_USE_TIOCGWINSZ).
- **`win32/win_fs.c`**: added `win_opendir`/`win_readdir`/
  `win_closedir` (FindFirstFile/FindNextFile-backed) and
  `win_gettimeofday` (FILETIME → timeval, 1601→1970 epoch offset).
- **`win32/win_fork.c`**: added `win_register_external_child(pid, h)`
  so non-cloned CreateProcess children (the `ssh.exe` from
  `win_spawn_remote_shell`) share the same pid→HANDLE waitpid table
  as cloned children. Removed `<sys/wait.h>` include (W*-decoder
  macros now in win_compat.h).
- **`win32/win_spawn.c`**: rewritten without gnulib. Builds an
  MSVC-quoted command line, creates two anonymous pipes, sets the
  parent ends non-inheritable, `CreateProcessA(NULL, cmdline, ...)`,
  hands the resulting HANDLE to `win_register_external_child`,
  wraps the pipe ends in CRT fds via `_open_osfhandle`. ~150 LOC,
  zero gnulib.
- **`scripts/build-windows.ps1`**: invokes `aclocal -I m4` +
  `autoconf -o configure.sh` + `autoheader` directly instead of
  `autoreconf -fiv` (the latter generates `configure` not
  `configure.sh`, mismatching rsync's hand-written Makefile.in
  reconfigure rule). Touches `configure.sh.old`/`config.h.in.old`
  up-front so the first build doesn't trip the "configure.sh has
  CHANGED — re-run make reconfigure" guard. Uses `bash -c` instead of
  `bash -lc` so `/etc/profile` doesn't reset the MSVC PATH inherited
  from `vcvars64.bat`. Sets `MSYS2_PATH_TYPE=inherit`, propagates
  `INCLUDE` / `LIB` env vars to bash, and uses `$repoMsys` directly
  for the build-aux/compile path so bash doesn't see literal
  `$PWD`. Updated lib names to match vcpkg's
  x64-windows-static triplet (`zstd.lib` not `zstd_static.lib`;
  zlib comes from rsync's bundled `zlib/` not vcpkg, so no `zs.lib`).
- **`scripts/smoke-test.ps1`**: corrected expected `--daemon` exit
  code from 14 (PLAN.md guess) to 4 (`RERR_UNSUPPORTED` per
  `errcode.h`).
- **`build-aux/compile` and `build-aux/ar-lib`**: copied from
  MSYS2's automake-1.17 package. Required because rsync isn't an
  automake project, so `autoreconf -fiv --install` doesn't drop them
  in. configure.sh routes `cl.exe` invocations through these wrappers
  to translate unix-style `-c -o file.o` into MSVC-style `/c
  /Fofile.o`. Tracked in-tree so CI doesn't need automake.

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

### Post-revision cleanup
- `win32/win_fs.c::win_symlink`: implemented PLAN.md's default
  symlink policy **C** (warn + fallback) instead of policy B
  (hard error). On `ERROR_PRIVILEGE_NOT_HELD` or
  `ERROR_ACCESS_DENIED`, emit a one-shot `FWARNING` message and
  fall back to `CopyFileA(target, linkpath)` (with relative-target
  resolution against the link's directory). Override with
  `RSYNC_STRICT_SYMLINKS=1` to force a hard error instead.
  Matches the wording already in `KNOWN-ISSUES.md`.
- `win32/README.md` added — file map for the win32/ directory plus
  conventions for adding new Windows-only sources. Closes a gap
  versus PLAN.md section 5.

### Phase 3 revision — fork-clone replaces re-exec
- `win32/win_fork.{c,h}`: implements POSIX `fork()` on Windows via
  ntdll's `RtlCloneUserProcess` (the Cygwin / MSYS2 / mitchcapper-tar
  approach). Includes `win_waitpid` since the MSVC CRT's `_cwait`
  doesn't know about cloned children.
- `win32/win_compat.h`: `#define fork() win_fork()` and
  `#define waitpid(p,s,o) win_waitpid(...)`. All upstream rsync code
  that calls `fork()` / `waitpid()` (util1.c::do_fork, pipe.c
  ::local_child, main.c::do_recv, socket.c::sock_exec, etc.) gets
  the macro substitution transparently.
- Reverted Phase 3's earlier re-exec branches:
  - `pipe.c::local_child` is back to its upstream form (just `fork()`).
  - `main.c::do_recv` is back to its upstream form (just `do_fork()`).
  - `main.c::main` no longer has the `win_child_init` hook.
- Removed obsolete files: `win32/win_reexec.{c,h}`,
  `win32/win_child_init.{c,h}`.
- `configure.ac`: `WIN32_OBJS` updated to include `win_fork.o`,
  exclude `win_reexec.o` / `win_child_init.o`.
- **Effect: downloads (rsync FROM remote TO Windows) and
  local-to-local syncs on Windows now use the same code path as
  Linux**, no longer broken. `piped_child` (ssh spawn) still uses
  gnulib's `create_pipe_bidi` because that's a non-clone code path.
- `shell_exec` still uses `system()` on Windows — `fork()+execlp()`
  also works but `system()` is simpler and equivalent.
- Linux full rebuild verified.

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
