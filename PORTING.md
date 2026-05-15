# PORTING.md — rsync-native-windows

Maintenance notes for the Windows native port. Every change to an upstream
`.c` or `.h` file must be documented in this file.

## Pinned dependencies

| Component | Pin | Notes |
|---|---|---|
| rsync upstream | tag `v3.4.2` | Branch `upstream-tracking` mirrors this tag |
| gnulib | `62d5d65f1dd82e0a69e86e2c3848f0d6280ba19d` (2026-05-10) | Submodule at `third_party/gnulib` |
| vcpkg baseline | see `vcpkg/vcpkg.json` `builtin-baseline` | Pinned to release 2026.03.18+ |
| OpenSSL | `3.6.1#3` (via vcpkg overrides) | CVE-2026-34054 fix |

### Deviations from PLAN.md

- **gnulib remote**: PLAN.md specifies `https://git.savannah.gnu.org/git/gnulib.git`.
  We use the GitHub mirror `https://github.com/coreutils/gnulib.git` because
  `git.savannah.gnu.org` was unreachable from the porting environment. The
  GitHub mirror is read-only and tracks upstream gnulib master. To switch
  back, edit `.gitmodules`:

  ```
  [submodule "third_party/gnulib"]
      url = https://git.savannah.gnu.org/git/gnulib.git
  ```

- **gnulib source-base**: PLAN.md specifies `--source-base=lib --m4-base=m4`.
  We use `--source-base=gl --m4-base=gl/m4` because rsync's upstream `lib/`
  and `m4/` directories already contain rsync's own source files (e.g.
  `lib/md5.c`, `lib/getaddrinfo.c` from PostgreSQL). Putting gnulib polyfills
  in those dirs would clobber or mix with them.

- **gnulib `--libtool` dropped**: rsync uses a hand-written `Makefile.in`
  with no Automake or libtool. We pass plain `--no-libtool` (default), so
  gnulib generates `libgnu.a` rules instead of `libgnu.la`.

- **vcpkg baseline**: PLAN.md specifies "2026.03.18 or later". We pin
  `2026.04.27` (SHA `56bb2411609227288b70117ead2c47585ba07713`).

### Build-system mismatch (resolved 2026-05-11: gnulib dropped)

Original problem: rsync's `Makefile.in` is hand-written (autoconf-only,
no automake, no libtool). gnulib-tool generates `gl/Makefile.am` (4447
lines) which expects automake, plus dozens of `.in.h` files that need
m4 substitution before they're usable.

**Resolution**: option (d) — drop gnulib from the build entirely.
Only one source file consumed it (`win32/win_spawn.c::create_pipe_bidi`
from `gl/spawn-pipe.h`), and that's ~60 LOC of CreateProcess + CreatePipe
written directly. The other gnulib polyfills that the import pulled in
(posix_spawn, sigaction, select, ...) were redundant — Win32 native
APIs cover the same ground without an automake build pipeline.

The `gl/` submodule remains in the tree to preserve history and to leave
room for a fallback if we ever want polyfills we can't easily write
inline. It is NOT on the include path during compilation; nothing in
`gl/` is built or linked.

## File map (Windows-specific code)

- `win32/` — all native Windows replacements and stubs
- `third_party/gnulib/` — gnulib submodule (POSIX polyfills)
- In-tree `#ifdef WIN32_NATIVE` blocks: tracked in the table below

## Changes to upstream files

_Add an entry every time you modify an upstream `.c` or `.h` file._

| File | Change | Reason |
|---|---|---|
| `configure.ac` | Added `WIN32_NATIVE` autoconf check (AC_PREPROC_IFELSE for `_WIN32 && !__CYGWIN__ && !__MSYS__`); sets `AC_DEFINE WIN32_NATIVE` and `AC_SUBST WIN32_NATIVE=yes/no`; on Windows, appends Windows system libraries to `LIBS`. Inserted after `AC_PATH_PROG([PYTHON3])`. | Phase 2: gates all Windows-specific code paths. Linux/BSD/macOS builds unaffected (detection returns `no`). |
| `rsync.h` | Inserted `#ifdef WIN32_NATIVE` / `#include "win32/win_compat.h"` block immediately after `#include "config.h"`. | Phase 2: pulls in Windows API headers, POSIX type stubs, and `win32/win_*.h` declarations. Block is inert when `WIN32_NATIVE` is undefined. |
| `Makefile.in` | Added `WIN32_OBJS = @WIN32_OBJS@` line and appended `$(WIN32_OBJS)` to `OBJS`. | Phase 2: links the `win32/*.o` stubs into `rsync.exe`. Empty on Linux/macOS. |
| `configure.ac` | Inside the `WIN32_NATIVE` shell block, set `WIN32_OBJS` to the list of `win32/*.o` files and `AC_SUBST` it. | Phase 2: feeds the object list to `Makefile.in` via `@WIN32_OBJS@`. |
| `pipe.c` | `piped_child()`: added `#ifdef WIN32_NATIVE` branch that calls `win_spawn_remote_shell()` (via gnulib `create_pipe_bidi`); the rest of the function body is the unchanged Unix fork/exec path under `#else`. | Phase 3 Site 1: replaces fork+exec(ssh) for remote-shell transport. We use direct CreateProcess on ssh.exe rather than fork+exec because Windows has no real `exec` (the MSVC `_execvp` actually spawns a new process and exits, leaving an orphan). |
| `main.c` | `shell_exec()`: added `#ifdef WIN32_NATIVE` branch returning `system(cmd)`; `#else` keeps the `fork()+execlp($RSYNC_SHELL)` path. | Phase 3 Site 4: same reasoning — `system()` is the cleanest "launch external command" primitive. |
| `options.c` | `check_for_hostspec()`: added `#ifdef WIN32_NATIVE` block at the top that returns `NULL` (= local path) when the input is a drive-letter path (`C:`, `C:\..`, `C:/..`) or a UNC path (`\\server\share`, `\\?\..`, `\\.\..`). | Phase 4 Task 4.1: fixes the cwRsync colon-parsing bug (`C:\Users` parsed as host "C"). |
| `rsync.h` | Added `#ifdef WIN32_NATIVE` block that unconditionally defines `SUPPORT_LINKS`, `SUPPORT_HARD_LINKS`, and overrides `do_readlink(...)` to `win_readlink(...)`. The pre-existing macros remain under `#else`. | Phase 4: gnulib's `readlink` polyfill is a stub on Windows, so we route through our reparse-point reader. Hard links are always available on NTFS via `CreateHardLinkA`. |
| `syscall.c` | `do_symlink()`: added `#ifdef WIN32_NATIVE` branch calling `win_symlink()`. `do_link()`: extended the gate to include `WIN32_NATIVE` and added a branch calling `win_link()`. `do_chmod()`: extended the gate; wraps Windows path in `#ifdef WIN32_NATIVE` short-circuit at top of function. | Phase 4 Tasks 4.4–4.5: native symlink, hardlink, and mode-bit handling via `CreateSymbolicLinkA`, `CreateHardLinkA`, `SetFileAttributesA`. |
| `win32/rsync.manifest` | New file. Sets `longPathAware=true` (Win10 1607+) and `activeCodePage=UTF-8` (Win10 1903+); `asInvoker` execution level. | Phase 4 Task 4.2: enables paths longer than 260 characters and UTF-8 narrow-API path handling. Embedded into `rsync.exe` resource section via `mt.exe` in `build-windows.ps1`. |
| `main.c` | In the remote-shell discovery block (`!cmd` after env-var check): on Windows, call `win_default_rsh()` instead of using `RSYNC_RSH` compile-time default. | Phase 5: prefer Windows' built-in OpenSSH client at `%SystemRoot%\System32\OpenSSH\ssh.exe` when present, fall back to `ssh.exe` on PATH. |
| `options.c` | `parse_arguments` `case OPT_DAEMON`: on Windows, `rprintf(FERROR, ...)` and `exit_cleanup(RERR_UNSUPPORTED)`. | Phase 6: rejects `--daemon`, `--config`, `--dparam`, `--no-detach`. |
| `options.c` | `check_for_hostspec` URL branch: on Windows, error + `exit_cleanup(RERR_UNSUPPORTED)` before parsing `rsync://`. | Phase 6: rejects `rsync://` daemon URLs early. |
| `clientserver.c` | Entire file body wrapped in `#ifndef WIN32_NATIVE` ... `#endif` (after `#include "rsync.h"`). | Phase 6: daemon TCP / rsyncd / daemon-auth code. Stub entry points live in `win32/stub_daemon.c`. |
| `loadparm.c` | Entire file body wrapped in `#ifndef WIN32_NATIVE` ... `#endif`. | Phase 6: rsyncd.conf parsing — unused. |
| `access.c` | Entire file body wrapped in `#ifndef WIN32_NATIVE` ... `#endif`. | Phase 6: daemon access-control — unused. |
| `authenticate.c` | Entire file body wrapped in `#ifndef WIN32_NATIVE` ... `#endif`. | Phase 6: daemon auth — unused. |
| `socket.c` | `start_accept_loop` function only wrapped in `#ifndef WIN32_NATIVE`. | Phase 6: daemon accept loop. Rest of `socket.c` (open_socket_out, set_socket_options, etc.) still compiles for utility use. |

## Fork sites (from upstream rsync 3.4.2)

| # | Location | Replacement on Windows |
|---|---|---|
| 1 | `pipe.c::piped_child()` | `win_spawn_remote_shell()` via gnulib `create_pipe_bidi` |
| 2 | `pipe.c::local_child()` | `win_reexec_self_as(WIN_ROLE_LOCAL_CHILD, ...)` |
| 3 | `main.c::do_recv()` | `win_reexec_self_as(WIN_ROLE_RECEIVER, ...)` |
| 4 | `main.c::shell_exec()` | `system(cmd)` (no fork on Windows) |
| 5 | `socket.c::sock_exec()` | Daemon-only; excised |
| 6 | `socket.c::start_accept_loop()` | Daemon-only; excised |
| 7 | `clientserver.c::become_daemon()` | Daemon-only; excised |

## fork() and waitpid() on Windows

Sites 2 (`pipe.c::local_child`) and 3 (`main.c::do_recv`) need a true
`fork()` — both processes continue running upstream rsync code with
identical in-memory state (parsed option globals, `first_flist`,
allocated buffers). Re-exec can't reproduce that; threads can't
reproduce it without rewriting many upstream globals as thread-local.

Solution: implement POSIX `fork()` via `RtlCloneUserProcess`, the
ntdll internal API used by Cygwin, MSYS2, and mitchcapper's tar port.
`win_compat.h` redirects calls with two function-like macros:

```c
#define fork()             win_fork()
#define waitpid(p, s, o)   win_waitpid((p), (s), (o))
```

This means **no edits to `pipe.c::local_child`, `main.c::do_recv`,
`util1.c::do_fork`, or anywhere else fork/waitpid is called.** The
existing upstream code path runs as-is on Windows.

### win_fork implementation summary

`win32/win_fork.c`:

- Resolves `RtlCloneUserProcess` from `ntdll.dll` lazily (single
  `GetProcAddress` per process, cached).
- Calls with `RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES`.
- Child path: function returns `STATUS_PROCESS_CLONED` (0x129); we
  return 0 from `win_fork()`.
- Parent path: returns `STATUS_SUCCESS`, gets process info; we
  return the child's pid and stash the process HANDLE in a small
  pid→HANDLE table (mutex-protected, capacity 64).

`win_waitpid`: looks up the HANDLE in the table (or falls back to
`OpenProcess(pid)`), calls `WaitForSingleObject` with `INFINITE` or
zero-timeout (WNOHANG), reads exit code via `GetExitCodeProcess`,
encodes in POSIX `W_EXITCODE` layout.

### Known limitations

- `RtlCloneUserProcess` is undocumented. It has been stable since
  Windows XP and is exercised by Cygwin on every fork, so risk of
  breaking changes is low — but not zero.
- Some EDR products (CrowdStrike, SentinelOne in aggressive modes)
  block the clone. There's no graceful fallback in this build; the
  user sees `errno=EAGAIN` and rsync exits.
- We don't replicate the Windows signal-handler table across the
  clone. rsync's signal use is light (SIGINT for Ctrl-C handling),
  and the CRT installs SIGINT via SetConsoleCtrlHandler which
  inherits across CreateProcess descendants. Untested but should work.
- We don't support `waitpid(-1, ...)` for any-child wait. Easy to
  add if upstream uses it.

## Cross-thread state on Windows

rsync's generator and receiver are separate POSIX processes after
`fork()`; on Windows they're threads of one process sharing every
non-`ROLE_TLS` global. Several upstream patterns rely on
process-isolation:

- **Save-zero-restore** of `info_levels[INFO_FLIST]` and
  `info_levels[INFO_PROGRESS]` in `generate_files`. Skipped on Windows
  (`#ifndef WIN32_NATIVE` in generator.c). Without that, the
  generator's zero blanks the receiver thread's `INFO_GTE(PROGRESS, 1)`
  check and per-file `--progress` output silently disappears.

- **Flip-do-flip-back** of option globals in three places: generator's
  redo block (generator.c:2161–2199), receiver's per-file dispatch
  (receiver.c:646–665, 987), and sender's redo handling
  (sender.c:317–327). The variables involved fall into two groups:

  - `csum_length` (io.c:74) is `ROLE_TLS` — both generator and
    receiver legitimately need per-thread copies, and the variable has
    no popt entry so there's no MSVC constant-initializer constraint.
    `do_recv_args::snap_csum_length` carries the user-set initial
    value across the thread fork.

  - `make_backups`, `append_mode`, `sparse_files`, `update_only`,
    `ignore_times`, `size_only`, `ignore_existing`,
    `ignore_non_existing`, `max_size`, `min_size`, `always_checksum`
    are NOT TLS. Most are read only by generator-side code (update_only
    et al.), so the flip stays inside the generator's thread context.
    `make_backups`, `append_mode`, and `sparse_files` ARE read by the
    receiver, but they have `&var` entries in options.c's static
    `long_options[]` table and MSVC rejects address-of-TLS in
    constant initializers. The residual race window is narrow: the
    generator only flips them inside its redo block, which fires only
    on transfer-verification failure; converging with the receiver's
    own per-file flip requires both threads to mutate the same byte
    at the same instant on a corner-case path. Acceptable for now;
    revisit if it ever bites.

- **I/O byte counters** `total_data_read` and `total_data_written`
  (io.c) are `+=` from both threads in `read_buf` / `write_buf`. Both
  are `ROLE_TLS`; the receiver thread's accumulated values are written
  back to `do_recv_args::ret_total_data_*` at thread exit and folded
  into the main thread's counters before `--stats` reports. Without
  this the per-thread counters get lost on thread teardown and the
  reported transfer rate is wrong.

## Conventions

- All Windows-specific code is gated by `#ifdef WIN32_NATIVE` (never `_WIN32`,
  which is too broad and matches Cygwin).
- New files live in `win32/` with a header comment block stating purpose and
  the upstream code path being replaced.
- Use `rprintf(FERROR, ...)` and `exit_cleanup(RERR_*)` rather than
  `fprintf(stderr, ...)` / `exit()` directly.
