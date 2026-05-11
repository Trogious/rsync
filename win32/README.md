# win32/

Windows-native code for the rsync port. Every file here is gated by
`#ifdef WIN32_NATIVE`, so this directory is invisible to Linux / BSD /
macOS builds. See `../PORTING.md` for the per-file edit map of the
upstream rsync source.

## File map

| File | Purpose |
|---|---|
| `win_compat.h` | Umbrella header. Pulled in from `rsync.h` on Windows. Provides Windows API includes (with `WIN32_LEAN_AND_MEAN`), POSIX type stubs (`pid_t`, `mode_t`, `ssize_t`, `uid_t`, `gid_t`), no-op macros for unsupported POSIX calls (`chown`, `setuid`, …), missing `S_IFLNK` / `S_ISLNK` / `S_ISFIFO`, and the `fork()` / `waitpid()` redirects to `win_fork()` / `win_waitpid()`. Forward-includes every other `win_*.h` in this directory. |
| `win_fork.{c,h}` | `fork()` via `RtlCloneUserProcess` (ntdll internal). The Cygwin / MSYS2 / mitchcapper-tar approach: child returns 0, parent gets child PID. Maintains a pid → process-HANDLE table so `win_waitpid` can find the child for `WaitForSingleObject`. |
| `win_spawn.{c,h}` | `win_spawn_remote_shell()` — wraps gnulib's `create_pipe_bidi` to spawn ssh.exe with bidirectional pipes. Used for `pipe.c::piped_child` (Phase 3 Site 1). |
| `win_paths.{c,h}` | Path classification: `win_is_drive_path` (`C:`, `D:\`, …), `win_is_unc_path` (`\\server\share`, `\\?\…`, `\\.\…`). In-place backslash → forward-slash normalization. `\\?\` long-path prefix generator. Used by `options.c::check_for_hostspec` to fix the cwRsync colon bug. |
| `win_fs.{c,h}` | Filesystem operations: `win_chmod` (maps user-write bit to `FILE_ATTRIBUTE_READONLY`), `win_symlink` (`CreateSymbolicLinkA` with unprivileged-create flag + warn-and-fallback-to-copy on permission failure; set `RSYNC_STRICT_SYMLINKS=1` to fail instead), `win_readlink` (`GetFinalPathNameByHandle` on the link), `win_link` (`CreateHardLinkA`), `win_utimens` (`SetFileTime` with FILETIME epoch conversion). |
| `win_ssh.{c,h}` | `win_default_rsh()` — resolves the default remote-shell command. Lookup order: `$RSYNC_RSH`, then `%SystemRoot%\System32\OpenSSH\ssh.exe` (Microsoft-shipped OpenSSH, Win10 1809+), then `ssh.exe` on `PATH`. |
| `stub_daemon.c` | Error stubs for daemon entry points (`start_daemon`, `daemon_main`, `start_accept_loop`, `start_socket_client`) whose upstream definitions live in `clientserver.c` / `socket.c`, both wrapped in `#ifndef WIN32_NATIVE`. The stubs `rprintf(FERROR, ...)` and exit with `RERR_UNSUPPORTED` (14). |
| `rsync.manifest` | Application manifest. Sets `longPathAware=true` and `activeCodePage=UTF-8`. Embedded into `rsync.exe`'s resource section by `mt.exe` in `scripts/build-windows.ps1`. |

## Adding a new Windows-only source file

1. Drop `win_foo.c` and `win_foo.h` in this directory; header comment
   should state the upstream code path being replaced.
2. Add `#include "win32/win_foo.h"` to `win_compat.h` so the rest of
   the source sees the declarations.
3. Append `win32/win_foo.o` to the `WIN32_OBJS` list in `configure.ac`.
4. Run `autoreconf -fiv` and rebuild (`scripts/build-windows.ps1`).
5. Add a row to the "Changes to upstream files" table in `../PORTING.md`
   describing any new `#ifdef WIN32_NATIVE` gates this required.

## Coding conventions

- All Windows-specific code is guarded by `#ifdef WIN32_NATIVE`, never
  `_WIN32` (which is too broad and matches Cygwin / MSYS2).
- Use rsync's `rprintf(FERROR|FWARNING|FINFO, ...)` and
  `exit_cleanup(RERR_*)`, not `fprintf(stderr, ...)` / `exit()`.
- Prefer the `A` (ANSI) Windows APIs with `activeCodePage=UTF-8`
  (set by the embedded manifest) rather than the `W` (wide) APIs.
  Saves the conversion churn at every call site.
- Returning `-1` with `errno` set is the contract for almost every
  function here — mirrors POSIX semantics so the upstream code paths
  can call our functions transparently.
