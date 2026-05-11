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

### Build-system mismatch (open issue)

Rsync's `Makefile.in` is hand-written (autoconf-only, no automake, no
libtool). gnulib-tool generates `gl/Makefile.am` which expects automake.

The Phase 2 work has to manually integrate gl/ into the rsync build:
- Add `AC_CONFIG_MACRO_DIRS([gl/m4])` to `configure.ac`
- Call `gl_EARLY` after `AC_PROG_CC` and `gl_INIT` later in `configure.ac`
- Add custom rules to `Makefile.in` to build `gl/libgnu.a` from the file
  list in `gl/Makefile.am`'s `libgnu_a_SOURCES`
- Only build/link gl/ when `WIN32_NATIVE` is detected (Linux/BSD/macOS
  builds should be unaffected)

**Deferred to first Windows session**: invoking `gl_EARLY` and `gl_INIT`
from configure.ac conditionally. The gnulib m4 macros use `AC_REQUIRE`
which expands at m4 time, so wrapping them in a `if test x$windows_native
= xyes` shell conditional doesn't actually make them conditional.
Pragmatic options to evaluate when we have Windows build feedback:
(a) call `gl_INIT` unconditionally and handle the AC_LIBOBJ fallout on
non-Windows by linking gl objects into a no-op archive;
(b) maintain a small `configure.ac.win` overlay that runs on Windows
only;
(c) build `gl/libgnu.a` outside autoconf entirely, with a hand-written
list of sources from `gl/Makefile.am`'s `libgnu_a_SOURCES`.

Option (c) is simplest if the `*.in.h → *.h` substitutions can be
pre-generated and committed; gnulib's `gnulib-tool --generate` mode or
running automake once locally can produce them.

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
| `pipe.c` | `piped_child()`: added `#ifdef WIN32_NATIVE` branch that calls `win_spawn_remote_shell()` (via gnulib `create_pipe_bidi`); the rest of the function body is the unchanged Unix fork/exec path under `#else`. | Phase 3 Site 1: replaces fork+exec(ssh) for remote-shell transport. |
| `pipe.c` | `local_child()`: added `#ifdef WIN32_NATIVE` branch that calls `win_reexec_self_as(WIN_ROLE_LOCAL_CHILD, ...)`; `#else` keeps the original Unix fork+pipe+child_main path. | Phase 3 Site 2: replaces fork+in-process-dispatch for local-to-local rsync. |
| `main.c` | `main()`: added `#ifdef WIN32_NATIVE` block at the top that calls `win_child_init(argc, argv)`; if it returns `>= 0` the process exits with that code (we were a re-exec'd child). | Phase 3: child-side hook for the re-exec machinery in `pipe.c::local_child` and (eventually) `do_recv`. |
| `main.c` | `do_recv()`: added `#ifdef WIN32_NATIVE` branch that prints an `RERR_UNSUPPORTED` error before the `do_fork()` call. Documented as a known limitation: downloads (FROM remote TO Windows) fail; uploads work. | Phase 3 Site 3 [DECIDE]: receiver/generator split needs `first_flist` + option-globals serialization (or thread-based redesign); deferred. |
| `main.c` | `shell_exec()`: added `#ifdef WIN32_NATIVE` branch returning `system(cmd)`; `#else` keeps the `fork()+execlp($RSYNC_SHELL)` path. | Phase 3 Site 4: no `fork()` on Windows; `system()` honors PATH-based shell resolution via cmd.exe. |
| `options.c` | `check_for_hostspec()`: added `#ifdef WIN32_NATIVE` block at the top that returns `NULL` (= local path) when the input is a drive-letter path (`C:`, `C:\..`, `C:/..`) or a UNC path (`\\server\share`, `\\?\..`, `\\.\..`). | Phase 4 Task 4.1: fixes the cwRsync colon-parsing bug (`C:\Users` parsed as host "C"). |
| `rsync.h` | Added `#ifdef WIN32_NATIVE` block that unconditionally defines `SUPPORT_LINKS`, `SUPPORT_HARD_LINKS`, and overrides `do_readlink(...)` to `win_readlink(...)`. The pre-existing macros remain under `#else`. | Phase 4: gnulib's `readlink` polyfill is a stub on Windows, so we route through our reparse-point reader. Hard links are always available on NTFS via `CreateHardLinkA`. |
| `syscall.c` | `do_symlink()`: added `#ifdef WIN32_NATIVE` branch calling `win_symlink()`. `do_link()`: extended the gate to include `WIN32_NATIVE` and added a branch calling `win_link()`. `do_chmod()`: extended the gate; wraps Windows path in `#ifdef WIN32_NATIVE` short-circuit at top of function. | Phase 4 Tasks 4.4–4.5: native symlink, hardlink, and mode-bit handling via `CreateSymbolicLinkA`, `CreateHardLinkA`, `SetFileAttributesA`. |
| `win32/rsync.manifest` | New file. Sets `longPathAware=true` (Win10 1607+) and `activeCodePage=UTF-8` (Win10 1903+); `asInvoker` execution level. | Phase 4 Task 4.2: enables paths longer than 260 characters and UTF-8 narrow-API path handling. Embedded into `rsync.exe` resource section via `mt.exe` in `build-windows.ps1`. |

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

## do_recv state preservation (open [DECIDE])

`main.c::do_recv()` forks to split the local side into a generator (parent)
and receiver (child) when downloading. After the fork on Unix, the child
keeps full memory access to:

- All option-parsed globals: `copy_links`, `tmpdir`, `backup_dir`,
  `preserve_hard_links`, `inc_recurse`, `chmod_modes`, `am_server`, dozens
  more.
- `first_flist` — the linked list of file metadata received from the
  remote sender, before the fork.

A CreateProcess-based re-exec does NOT carry this state. The Phase 3
stub currently errors out with `RERR_UNSUPPORTED`. Three design options
to evaluate when iterating on Windows:

1. **Serialize state.** Extend the state file written by `win_reexec.c`
   to include each option global and a serialized form of `first_flist`.
   Pros: matches upstream's process-isolation model. Cons: large surface,
   ongoing maintenance burden as new option globals are added upstream.

2. **Use threads.** On Windows, run the receiver in a worker thread of
   the same process. Globals are shared; no serialization. Pros:
   straightforward. Cons: upstream code uses `exit()` / `_exit()` in
   places where a thread would need to bail differently; signal handling
   differs; shared `io_*` state needs locks.

3. **Windows fork-clone API.** Use `RtlCloneUserProcess` /
   `NtCreateUserProcess` (mitchcapper's approach) to literally clone the
   process. Pros: preserves state for free. Cons: undocumented API,
   fragile across Windows versions, blocked by some EDR products.

Recommended path: try (2) first — bind a thread to "the receiver role"
and use a `Sleep`-free message channel between generator and receiver
via in-memory queues instead of pipes. Fallback to (1) if upstream's
`exit()`/`io_flush_msg` pattern can't be threaded cleanly.

## Conventions

- All Windows-specific code is gated by `#ifdef WIN32_NATIVE` (never `_WIN32`,
  which is too broad and matches Cygwin).
- New files live in `win32/` with a header comment block stating purpose and
  the upstream code path being replaced.
- Use `rprintf(FERROR, ...)` and `exit_cleanup(RERR_*)` rather than
  `fprintf(stderr, ...)` / `exit()` directly.
