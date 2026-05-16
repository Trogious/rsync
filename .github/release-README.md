# rsync for native Windows

**Release:** @@RELEASE_TAG@@
**Source revision:** @@VERSION@@ (commit `@@COMMIT@@`)
**Built:** @@BUILD_DATE@@
**Architecture:** Windows x86_64 (Windows 10 1607 or newer)

A native Windows port of [rsync](https://rsync.samba.org/) — same protocol,
same wire format, same command-line — compiled with MSVC against a fully
static dependency set. **No Cygwin, no MSYS2 runtime, no DLL bundle.**
Drop `rsync.exe` anywhere on a Win10+ machine and it runs.

Connect via SSH (`rsync user@host:/path/ .`) or directly to a remote
rsync daemon (`rsync rsync://host/module/path .`). Running rsync ITSELF
as a daemon (`--daemon`) is not supported.

---

## Quick start

```
rsync.exe -av source/ user@host:/dest/
rsync.exe -av user@host:/source/ C:\dest\
rsync.exe -av C:\Users\me\photos\ D:\backup\photos\
```

UTF-8 filenames work transparently — the manifest baked into the binary
sets the process active codepage to UTF-8 (Win10 1903+).

For non-ASCII names when talking to a Linux peer whose filesystem is
already UTF-8, pass `--iconv=utf-8,utf-8` (rsync needs an explicit
character-set hint).

---

## What's enabled in this build

`rsync --version` reports:

```
Capabilities:
    64-bit files, 16-bit inums, 64-bit timestamps, 64-bit long ints,
    no socketpairs, symlinks, no symtimes, hardlinks, no hardlink-specials,
    no hardlink-symlinks, no IPv6, atimes, batchfiles, inplace, append,
    no ACLs, no xattrs, optional secluded-args, iconv, no prealloc, stop-at,
    no crtimes
Optimizations:
    SIMD-roll, asm-roll, openssl-crypto, no asm-MD5
Checksum list:
    xxh128 xxh3 xxh64 (xxhash) md5 md4 sha1 none
Compress list:
    zstd lz4 zlibx zlib none
```

Notable:

- **64-bit files** — handles files larger than 2 GiB (verified with a 64 GiB transfer round-trip).
- **inplace, append** — `--inplace` and `--append`/`--append-verify` work.
- **iconv** — `--iconv=...` works for non-ASCII filenames across platforms.
- **SIMD-roll** — AVX2 rolling-checksum (Haswell 2013+ / Excavator 2015+ minimum).
- **asm-roll** — hand-tuned AVX2 assembly for the inner rolling-checksum loop.
- **openssl-crypto** — MD5/SHA1/SHA256/SHA512 go through OpenSSL's optimized implementations.

---

## Modifications made to upstream rsync

This port targets MSVC + Windows native APIs without an emulation layer
(no Cygwin, no MSYS2). The full list of upstream-file changes is tracked
in `PORTING.md` in the source tree; the high-level themes:

### Build system

- New autoconf check sets `WIN32_NATIVE` when building under MSVC. Linux/BSD/macOS builds are untouched.
- `Makefile.in` substitutes a `WIN32_OBJS` list (the `win32/*.o` shim files) and a `WIN32_HEADERS` list (the umbrella `win32/win_compat.h` and friends) so make tracks edits to Windows-only sources.
- Build is driven by `scripts/build-windows.ps1`, which:
  - Sources `vcvars64.bat` so `cl.exe`/`lib.exe`/`link.exe` are on PATH.
  - Runs `autoreconf` inside MSYS2 to regenerate `configure.sh` and `config.h.in`.
  - Pre-seeds autoconf cache vars to short-circuit POSIX-only feature probes.
  - Configures + builds with `-MT` (static CRT) and links against vcpkg's `x64-windows-static` triplet.
  - Embeds `win32/rsync.manifest` via `mt.exe` (long-path + UTF-8 codepage).

### Process model

POSIX rsync forks at three sites; Windows has no `fork()`. This port handles each site differently:

- **SSH remote-shell child** (`pipe.c::piped_child`) — `CreateProcessA` on `ssh.exe`, pipes wired directly.
- **Local server child** (`pipe.c::local_child`) — re-exec the binary via `CreateProcessA` with role-marker env vars (the `local_child` path is unusual: the child runs `rsync --server --sender ...` against an in-process source).
- **Receiver thread** (`main.c::do_recv`) — Windows thread + thread-local storage for the role-divergent globals (`am_server`, `am_sender`, etc.) that POSIX rsync naturally gets per-process via fork.
  - `ROLE_TLS` macro in `win32/win_compat.h` (`__declspec(thread)`) is applied to the relevant globals' definitions; the matching `extern` declarations don't need it.
  - `win_thread_fork()` registers a fake pid in a pid→HANDLE table so `waitpid()` works unchanged.

`RtlCloneUserProcess` (the ntdll API Cygwin uses for fork) was tried first but deadlocks rsync's protocol-handshake state, so the thread approach was used instead.

### File I/O

- **`O_BINARY` everywhere** — MSVC's `open()` defaults to text mode (CRLF translation). `do_open`, `do_open_nofollow`, and `secure_relative_open` all set `O_BINARY` on Windows. The third was the source of a delta-transfer bug — the basis file was being opened in text mode, so any 0x0A bytes read out of it were silently mangled, and the reconstructed temp file failed its MD5 check.
- **64-bit offsets** — MSVC's `off_t` is 32-bit `long`. `win32/win_compat.h` overrides `SIZEOF_OFF64_T`/`HAVE_STRUCT_STAT64`/`HAVE_LSEEK64` before rsync.h's `OFF_T`/`STRUCT_STAT` macros are evaluated, and redirects `stat`/`fstat`/`lseek` (and their `64` variants) at MSVC's `_stat64`/`_fstat64`/`_lseeki64` functions.
- **`ftruncate` shim** — backed by MSVC's `_chsize_s(int fd, __int64 size)`. Bridges the errno-returning convention to POSIX 0/-1+errno. Enables `--inplace` and `--append`.

### Path handling

- **Backslash → forward slash** — `main()` rewrites backslashes in local path arguments to forward slashes (rsync's internal path code assumes POSIX). Skipped for arguments that look like `user@host:path` so remote paths aren't mangled.
- **Drive-letter parsing** — `check_for_hostspec()` recognises `C:\...`, `C:/...`, `\\server\share`, `\\?\...`, `\\.\...` as local rather than treating the colon as a remote-host separator.
- **Long paths** — the embedded manifest sets `longPathAware=true` and `activeCodePage=UTF-8` (Win10 1607+/1903+).

### POSIX shims (`win32/win_compat.h`)

A single umbrella header pulls in:

- `windows.h`, `winsock2.h`, `ws2tcpip.h`
- POSIX type stubs MSVC doesn't ship: `ssize_t`, `pid_t`, `mode_t`, `uid_t`, `gid_t`
- File-mode bits (`S_IFLNK`, `S_ISVTX`, `S_IRGRP`, ...) — zero-valued where Windows ACLs don't fit
- Macro redirects: `fork`→`win_fork`, `waitpid`→`win_waitpid`, `pipe`→`win_pipe`, `fsync`→`_commit`, `select`→`win_select`, `kill`→`win_kill`, `gettimeofday`→`win_gettimeofday`, `localtime_r`→`win_localtime_r`, `utime`→`win_utime_shim`, `ftruncate`→`win_ftruncate`, `lstat`→`stat`
- Signal/sigaction stubs (Windows has no signal masks; we route through `signal()`)
- Minimal `DIR`/`readdir` backed by `FindFirstFileA`/`FindNextFileA`
- `passwd`/`group` stubs that always return `NULL` (no `/etc/passwd`)
- `chown`/`lchown` → no-op (POSIX ownership doesn't map onto Windows ACLs in a useful way)

### Daemon-mode excision

`--daemon`, `--config`, `rsync://...`, and `host::module` are deliberately removed:

- `clientserver.c`, `loadparm.c`, `access.c`, `authenticate.c` — entire file bodies wrapped in `#ifndef WIN32_NATIVE`.
- `socket.c::start_accept_loop` only — the rest of `socket.c` (`open_socket_out`, `set_socket_options`) still compiles for outbound client use.
- `options.c` rejects `--daemon` and `rsync://` URLs at argument-parse time with `RERR_UNSUPPORTED` (exit code 4).
- `win32/stub_daemon.c` provides linker stubs for daemon-only symbols still referenced from common code paths (the daemon-mode references are dead at runtime; they exist only to satisfy the linker).

### Optimization paths

- **SIMD-roll** — MSVC has no `__attribute__((target(...)))` for runtime CPU dispatch, so `simd-checksum-x86_64.cpp` commits to AVX2 at compile time (Intel Haswell 2013+ / AMD Excavator 2015+ minimum). The "default" multiversion stubs are skipped under MSVC to avoid colliding with the real implementations.
- **asm-roll** — the upstream `simd-checksum-avx2.S` is GAS Intel-syntax + System V ABI and isn't accepted by `cl.exe` or `ml64.exe`. A hand-translated MASM file (`simd-checksum-avx2-win64.asm`) lives alongside it: same loop body, Win64 `PROC FRAME` prologue with proper `.pushreg` / `.savexmm128` unwind directives, and an argument-shuffle that maps Win64's `RCX/EDX/R8D/R9/[RSP+40]` into the SysV-positioned registers (`RDI/ESI/EDX/RCX/R8`) the body expects.

---

## Limitations

### Excised features

- **`--daemon` mode** — rsync as a TCP service (`rsyncd`). Exits with code 4 (`RERR_UNSUPPORTED`).
- **`--config=FILE`** — rsyncd.conf parsing.
- **ACLs (`-A` / `--acls`)** — Windows ACLs don't map cleanly onto the POSIX ACL model the protocol carries.
- **Extended attributes (`-X` / `--xattrs`)** — same reason.
- **IPv6** — outbound IPv4 SSH works; the Windows build doesn't currently negotiate IPv6 sockets.
- **POSIX ownership** — `chown` / `lchown` are no-ops. uid/gid metadata on the wire is silently dropped on the Windows side; `--owner`/`--group` flags don't error but have no effect.
- **SELinux contexts.**

When talking to a Linux peer, the wire protocol still negotiates ACL/xattr/ownership flags as if they were honored — they're just no-ops on the Windows endpoint. If you need bidirectional ACL fidelity, use rsync between two Linux endpoints and a separate copy step.

### Quirks

- **Drive-relative paths.** `C:foo` (drive `C:`'s working directory + `foo`) is parsed as a REMOTE host spec — `host = "C"`, `path = "foo"`. Matches cwRsync. To force local interpretation: `.\C:foo`. Absolute (`C:\foo`, `C:/foo`) and bare-drive (`C:`) forms are always local.
- **Symlink creation** needs one of: Developer Mode enabled (Win10 1703+), Administrator, or `SeCreateSymbolicLinkPrivilege`. Without any of those, rsync falls back to copying the target's content and emits a warning. Set `RSYNC_STRICT_SYMLINKS=1` to make symlink-permission failures fatal.
- **SSH client discovery** order: `$RSYNC_RSH` env var → `%SystemRoot%\System32\OpenSSH\ssh.exe` (Windows built-in) → `ssh.exe` on PATH. Override per-invocation with `-e <command>`.
- **`asm-MD5` is intentionally disabled.** The bundled rsync MD5 asm (Marc Bevand 2004) is ~5-8% faster than rsync's C reference, but OpenSSL's MD5 (already used in this build via `openssl-crypto`) is generally faster than both. Enabling `asm-MD5` would force rsync to bypass OpenSSL for MD5 and use the slower bundled asm — a regression on this build.
- **`mtime` granularity** is 1 second. Modifications inside the same second as the previous sync may be missed by the default size+mtime quick-check; use `-c` (checksum-based) or wait a second between sync and modify.

---

## License

rsync is GPLv3. Full text in `LICENSE.txt`. The Windows-specific code added by this port is also GPLv3.

---

## Building from source

See `BUILD.md` and `scripts/build-windows.ps1` in the source tree. Briefly:

1. Install Visual Studio 2022 Build Tools with the "Desktop development with C++" workload.
2. Install [MSYS2](https://www.msys2.org/) at `C:\msys64` (autotools shell only — not a runtime dep).
3. Install [vcpkg](https://vcpkg.io/) at `C:\vcpkg`, then:
   ```
   C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg
   ```
4. From an "x64 Native Tools Command Prompt for VS 2022":
   ```
   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
   ```
