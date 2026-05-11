# rsync-native-windows — Implementation PLAN

> **Audience:** Claude Code (autonomous coding agent).
> **Read this entire file before starting.** Execute phases in order.
> **STOP and ask the user** at any `[DECIDE]` marker. Do not guess.
> When code/commands are quoted, they are normative — use exactly that text unless the file explicitly says "adapt as needed".

---

## 0. Mission (one paragraph, do not deviate)

Produce `rsync.exe` for Windows that is:
1. Fully native (no `cygwin1.dll`, no `msys-2.0.dll`, no `lib*-*.dll`, no POSIX emulator of any kind at runtime).
2. Client-only (no daemon, no listening on TCP 873, no `rsyncd.conf`).
3. Statically linked against all third-party deps (OpenSSL, zstd, lz4, xxhash, zlib).
4. Compiled with MSVC `cl.exe` and static CRT (`/MT`).
5. Compatible with upstream rsync protocol 32 (rsync 3.4.x).
6. Built reproducibly via GitHub Actions.

This is a fork of the upstream rsync source, with Windows-specific patches concentrated under `#ifdef WIN32_NATIVE` and in a new `win32/` subdirectory. We track upstream releases; we do not rewrite the protocol.

---

## 1. Non-negotiables (hard constraints — do not relax without `[DECIDE]`)

- **Allowed runtime DLL dependencies (only):** `kernel32.dll`, `kernelbase.dll`, `ntdll.dll`, `advapi32.dll`, `user32.dll`, `ws2_32.dll`, `crypt32.dll`, `secur32.dll`, `bcrypt.dll`, `iphlpapi.dll`, `userenv.dll`, `shell32.dll`, `ole32.dll`, `oleaut32.dll`, `rpcrt4.dll`, `ucrtbase.dll`, `api-ms-win-*.dll` (UCRT API set forwarders).
- **Forbidden:** anything matching `cyg*.dll`, `msys-*.dll`, `lib*-*.dll`, `vcruntime*.dll` (means /MD not /MT), `msvcp*.dll`, `msvcr*.dll`.
- **No GPL violations.** Output is GPL-3.0+ (inherited from rsync). All source published in the fork repo.
- **No mingw.** MSVC `cl.exe` only. (clang-cl acceptable as fallback, but verify against MSVC first.)
- **No Cygwin or MSYS2 in the runtime path.** MSYS2 is acceptable *only* as the build-time shell for autotools.

---

## 2. Architecture decisions (locked)

| Item | Decision | Rationale (brief) |
|---|---|---|
| Upstream | `https://github.com/RsyncProject/rsync`, tag `v3.4.2` | Newest stable; protocol 32 |
| Build shell | MSYS2 (msystem=MSYS, NOT MINGW64) | autotools needs bash; resulting binary has zero MSYS dependency when compiled with MSVC |
| Compiler | MSVC 2022 (cl.exe) via VS Build Tools | Native ABI; no GCC runtime |
| CRT linkage | Static (`/MT`) | Required for fully static binary |
| Library linkage | Static for all third-party | Required by mission |
| Portability layer | gnulib (git submodule, pinned SHA) | Mature, has Windows posix_spawn since 2020, active maintenance |
| Dependency manager | vcpkg with manifest mode, `x64-windows-static` triplet | Pinned baseline for reproducibility |
| OpenSSL version | 3.6.1#3 or later (CVE-2026-34054 fix) | Pin via vcpkg manifest override |
| Compression | zstd, lz4, zlib (all static via vcpkg) | Negotiated by modern rsync |
| Hash for delta | xxhash static via vcpkg | rsync default since 3.2.0 |
| Argument parser | vendored popt (already in upstream `popt/` dir) | Avoid external dep |
| SSH transport | Spawn `C:\Windows\System32\OpenSSH\ssh.exe` | Built into Win10 1809+ |
| Daemon code | Excised, stubbed with error | Client-only mission |
| Target arch (v1.0) | x86_64 only | arm64 deferred |
| License | GPL-3.0+ | Inherited |

---

## 3. Upstream references (URLs I verified during research)

**Primary upstream source files to read for context (do not edit these in upstream form; we fork):**
- `https://github.com/RsyncProject/rsync/blob/master/pipe.c` — fork sites 1-2 (`piped_child`, `local_child`)
- `https://github.com/RsyncProject/rsync/blob/master/main.c` — fork sites 3-4 (`shell_exec`, `do_recv`)
- `https://github.com/RsyncProject/rsync/blob/master/socket.c` — fork sites 5-6 (`sock_exec`, `start_accept_loop`) — daemon-only
- `https://github.com/RsyncProject/rsync/blob/master/clientserver.c` — fork site 7 (`become_daemon`) — daemon-only
- `https://github.com/RsyncProject/rsync/blob/master/options.c` — `check_for_hostspec`, `parse_hostspec` (path/colon parsing)

**gnulib (will be submodule):**
- `https://git.savannah.gnu.org/git/gnulib.git`
- Key headers: `lib/spawn-pipe.h`, `lib/execute.h`, `lib/spawn.in.h`
- Manual: `https://www.gnu.org/software/gnulib/manual/gnulib.html`

**Reference port (architectural template — read but do not copy code wholesale):**
- `https://github.com/mitchcapper/WIN64LinuxBuild` — overall build pattern
- `https://github.com/mitchcapper/tar/compare/master...win32_enhancements` — fork-replacement diff for tar (the closest precedent)
- `https://github.com/mitchcapper/WIN64LinuxBuild/blob/master/repo_notes/tar_README.md` — tar port notes
- mitchcapper's analysis of rsync specifically: `https://github.com/RsyncProject/rsync/issues/658#issuecomment-by-mitchcapper`

**vcpkg:**
- `https://github.com/microsoft/vcpkg` — pin to release 2026.03.18 or later
- OpenSSL CVE advisory: `GHSA-p322-v6vw-vrq9` / `CVE-2026-34054`

**Built-in Windows OpenSSH (the SSH transport we shell out to):**
- Default install path: `C:\Windows\System32\OpenSSH\ssh.exe` (Win10 1809+, Win11, Server 2019+)

---

## 4. Repository setup (Phase 0 prerequisite)

The user will fork `RsyncProject/rsync` to their own GitHub org/account via the GitHub UI before invoking you. They will provide the fork URL.

`[DECIDE]` On first invocation, ask: "What is the URL of the rsync fork (e.g., `git@github.com:tomasz/rsync-win.git`)?"

Once you have the fork URL, execute:

```bash
# Clone the fork
git clone <FORK_URL> rsync-win
cd rsync-win

# Add upstream and disable pushes to it
git remote add upstream https://github.com/RsyncProject/rsync.git
git remote set-url --push upstream DISABLED

# Fetch tags
git fetch upstream --tags

# Create the work branch from v3.4.2
git checkout -b win32-port v3.4.2

# Create an upstream-tracking branch pinned to v3.4.2
git checkout -b upstream-tracking v3.4.2
git push -u origin upstream-tracking

# Switch back to work branch and push
git checkout win32-port
git push -u origin win32-port
```

`[USER ACTION REQUIRED]` Ask the user to set `win32-port` as the default branch in GitHub repository settings, and to add a branch protection rule on it.

---

## 5. Directory layout to create

```
rsync-win/                        # repo root (all paths below are relative to here)
├── (upstream rsync source files at root, unchanged unless gated by WIN32_NATIVE)
├── win32/                        # NEW — all our additions
│   ├── README.md
│   ├── win_compat.h              # umbrella header — included from rsync.h on Windows
│   ├── win_spawn.c               # piped_child replacement using gnulib spawn-pipe
│   ├── win_spawn.h
│   ├── win_reexec.c              # local_child + do_recv replacement (re-exec self)
│   ├── win_reexec.h
│   ├── win_child_init.c          # called from main() as first line; handles --_win_child=
│   ├── win_child_init.h
│   ├── win_paths.c               # drive-letter, UNC, long-path handling
│   ├── win_paths.h
│   ├── win_fs.c                  # symlinks, hardlinks, perm stubs, utimens
│   ├── win_fs.h
│   ├── win_ssh.c                 # OpenSSH path resolution
│   ├── win_ssh.h
│   └── stub_daemon.c             # error stubs for excised daemon functions
├── third_party/
│   └── gnulib/                   # git submodule
├── vcpkg/
│   ├── vcpkg.json                # manifest
│   └── vcpkg-configuration.json  # registries (if needed)
├── scripts/
│   ├── bootstrap-windows.ps1
│   ├── build-windows.ps1
│   ├── verify-static.ps1
│   ├── smoke-test.ps1
│   └── gnulib-import.sh          # invokes gnulib-tool with our module list
├── .github/
│   └── workflows/
│       ├── build.yml
│       ├── upstream-sync.yml
│       └── cve-watch.yml
├── PORTING.md                    # maintenance notes — every Windows change documented here
├── BUILD.md                      # how to build locally
├── KNOWN-ISSUES.md
├── CHANGELOG.md
└── PLAN.md                       # this file
```

Create these directories on the `win32-port` branch:

```bash
mkdir -p win32 third_party vcpkg scripts .github/workflows
touch PORTING.md BUILD.md KNOWN-ISSUES.md CHANGELOG.md
git add win32 third_party vcpkg scripts .github PORTING.md BUILD.md KNOWN-ISSUES.md CHANGELOG.md
git commit -m "Initialize win32-port directory skeleton"
```

---

## 6. Phase tracking

Track progress in `CHANGELOG.md` as you go. Each phase has a definition of "done" — do not advance until it's met. Use `git tag phase-N-complete` at each phase boundary.

| Phase | Goal | Done when |
|---|---|---|
| 0 | Fork & directory skeleton | Skeleton committed; submodule added |
| 1 | Build infrastructure | `vcpkg install` succeeds; gnulib imports cleanly |
| 2 | Initial compile | `rsync.exe` produced (may crash at runtime) |
| 3 | Fork emulation | `rsync -av localfile user@host:dst/` works |
| 4 | Filesystem & paths | `C:\path` parsed as local; symlinks work |
| 5 | SSH integration | Default ssh path found; arbitrary `-e` works |
| 6 | Daemon excision | `--daemon`, `--config`, `rsync://` all rejected with errors |
| 7 | CI | GitHub Actions builds artifact on every push |
| 8 | Validation | All 15 tests in the matrix pass |
| 9 | Ship | v1.0.0 tagged, release published |

---

## 7. PHASE 0 — Fork & directory skeleton

### Goal
Have a working git repository with the fork in place, branches set up, and the skeleton directory structure committed.

### Tasks

1. Execute the git commands in Section 4 (asking for fork URL first).
2. Create the directory structure from Section 5.
3. Add gnulib as a submodule pinned to a known-good commit:

   ```bash
   git submodule add https://git.savannah.gnu.org/git/gnulib.git third_party/gnulib
   cd third_party/gnulib
   # Pin to a recent commit. As of 2026-05, the gnulib master HEAD changes frequently.
   # Pick the most recent commit that mitchcapper's WIN64LinuxBuild CI passed against.
   # If you cannot determine a known-good SHA, use a commit from the past 30 days.
   git log -1 --format=%H > /tmp/gnulib_sha.txt
   cat /tmp/gnulib_sha.txt
   cd ../..
   git add third_party/gnulib .gitmodules
   git commit -m "Pin gnulib at $(cat /tmp/gnulib_sha.txt)"
   ```

   `[DECIDE]` After pinning, write the SHA to `PORTING.md` under a "Pinned dependencies" section. If the build fails later due to a gnulib issue, the user may need to bump this — surface this in your messages.

4. Write the initial `PORTING.md`:

   ```markdown
   # PORTING.md — rsync-native-windows

   ## Pinned dependencies
   - gnulib: `<SHA>`
   - vcpkg baseline: see `vcpkg/vcpkg.json`
   - rsync upstream: tag `v3.4.2`

   ## File map (Windows-specific code)
   - `win32/` — all native Windows replacements
   - In-tree `#ifdef WIN32_NATIVE` blocks: tracked below

   ## Changes to upstream files
   _Add an entry every time you modify an upstream `.c` or `.h` file._

   | File | Change | Reason |
   |---|---|---|
   | (none yet) | | |
   ```

5. Write the initial `BUILD.md` with placeholder content (filled in by Phase 7).

### Done when
- `git log --oneline` shows at least 3 commits on `win32-port`.
- `git submodule status` shows `third_party/gnulib` with the pinned SHA.
- The directory tree from Section 5 exists (even if many files are empty).
- `PORTING.md` exists with the pinned SHAs recorded.

Tag: `git tag phase-0-complete && git push --tags`

---

## 8. PHASE 1 — Build infrastructure

### Goal
Produce a vcpkg-based static dependency set and import the required gnulib modules. No compilation of rsync itself yet.

### Tasks

1. Create `vcpkg/vcpkg.json` with this exact content:

   ```json
   {
     "name": "rsync-win-deps",
     "version-string": "1.0.0",
     "builtin-baseline": "REPLACE_WITH_VCPKG_2026_03_18_OR_LATER_SHA",
     "dependencies": [
       { "name": "openssl", "default-features": false },
       { "name": "zstd",    "default-features": false },
       { "name": "lz4",     "default-features": false },
       { "name": "xxhash",  "default-features": false },
       { "name": "zlib",    "default-features": false }
     ],
     "overrides": [
       { "name": "openssl", "version": "3.6.1", "port-version": 3 }
     ]
   }
   ```

   `[DECIDE]` Look up the actual git SHA of vcpkg release `2026.03.18` (or newer) from `https://github.com/microsoft/vcpkg/releases`. Paste it into `builtin-baseline`. If you cannot determine the SHA, ask the user.

2. Create `scripts/bootstrap-windows.ps1`:

   ```powershell
   # scripts/bootstrap-windows.ps1
   # Sets up the build environment on Windows. Requires admin for one-time installs.
   $ErrorActionPreference = 'Stop'

   # 1. Install MSYS2 if missing
   if (-not (Test-Path 'C:\msys64\usr\bin\bash.exe')) {
       Write-Host "Installing MSYS2..."
       $url = 'https://github.com/msys2/msys2-installer/releases/latest/download/msys2-x86_64-latest.exe'
       $exe = "$env:TEMP\msys2-installer.exe"
       Invoke-WebRequest -Uri $url -OutFile $exe
       Start-Process -FilePath $exe -ArgumentList 'install --root C:\msys64 --confirm-command' -Wait
   }

   # 2. Update MSYS2 and install required packages
   & 'C:\msys64\usr\bin\bash.exe' -lc 'pacman -Syu --noconfirm'
   & 'C:\msys64\usr\bin\bash.exe' -lc 'pacman -S --noconfirm base-devel autotools make tar curl python git patch sed gawk gperf bison flex texinfo'

   # 3. Install Visual Studio Build Tools if missing
   # See: https://aka.ms/vs/17/release/vs_buildtools.exe
   # Required workloads: Microsoft.VisualStudio.Workload.VCTools

   # 4. Bootstrap vcpkg
   if (-not (Test-Path 'C:\vcpkg\vcpkg.exe')) {
       git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
       & C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   }

   Write-Host "Bootstrap complete. Open a 'x64 Native Tools Command Prompt for VS 2022' to build."
   ```

3. Install dependencies (run from VS Developer Command Prompt):

   ```cmd
   cd <repo-root>
   C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg
   ```

   Verify with:
   ```cmd
   C:\vcpkg\vcpkg.exe list
   ```
   Confirm every entry has `:x64-windows-static`. If any shows `:x64-windows` (dynamic), STOP — that's a transitive dependency bug (vcpkg issue #32154 history). Investigate which package pulled it in.

4. Create `scripts/gnulib-import.sh`:

   ```bash
   #!/usr/bin/env bash
   # scripts/gnulib-import.sh
   # Imports the gnulib modules we need into the source tree.
   # Run from the repo root inside MSYS2 shell.
   set -euo pipefail

   GNULIB_TOOL="third_party/gnulib/gnulib-tool"
   if [[ ! -x "$GNULIB_TOOL" ]]; then
       echo "ERROR: gnulib-tool not found. Did you initialize the submodule?"
       exit 1
   fi

   "$GNULIB_TOOL" --import \
       --dir=. \
       --lib=libgnu \
       --source-base=lib \
       --m4-base=m4 \
       --doc-base=doc \
       --tests-base=tests \
       --aux-dir=build-aux \
       --no-conditional-dependencies \
       --libtool \
       --macro-prefix=gl \
       posix_spawn \
       posix_spawnp \
       posix_spawn_file_actions_init \
       posix_spawn_file_actions_addclose \
       posix_spawn_file_actions_adddup2 \
       posix_spawn_file_actions_addopen \
       posix_spawnattr_init \
       posix_spawnattr_setflags \
       spawn-pipe \
       execute \
       wait-process \
       getopt-gnu \
       strdup \
       strndup \
       strerror_r \
       fnmatch \
       glob \
       sys_select \
       select \
       sys_socket \
       netinet_in \
       arpa_inet \
       canonicalize-lgpl \
       readlink \
       symlink \
       lstat \
       utimens \
       futimens \
       sigaction \
       sigprocmask \
       sleep \
       nanosleep
   ```

   Make it executable and run it:
   ```bash
   chmod +x scripts/gnulib-import.sh
   ./scripts/gnulib-import.sh
   ```

5. Commit the result:
   ```bash
   git add lib/ m4/ build-aux/ doc/ tests/ scripts/gnulib-import.sh vcpkg/vcpkg.json
   git commit -m "Phase 1: Import gnulib modules and vcpkg manifest"
   ```

### Done when
- `C:\vcpkg\vcpkg list` shows openssl, zstd, lz4, xxhash, zlib all with `:x64-windows-static` triplet.
- `lib/` directory contains gnulib polyfill sources (you'll see files like `lib/spawn-pipe.c`, `lib/posix_spawn.c`).
- `m4/` directory has gnulib's autoconf macros.
- Build is reproducible — running `scripts/bootstrap-windows.ps1` on a fresh Windows box produces the same dep tree.

Tag: `git tag phase-1-complete && git push --tags`

---

## 9. PHASE 2 — Initial compile (rsync.exe may crash at runtime)

### Goal
Produce `rsync.exe` from the source. It does NOT need to work correctly at runtime — the goal is to prove the build pipeline. A clean compile + link with our static deps is the milestone.

### Tasks

1. Edit `configure.ac` (upstream file) to add Windows detection. Use `str_replace` to add this block after the `AC_INIT` macros:

   ```m4
   # Detect Windows native build
   AC_MSG_CHECKING([for Windows native build])
   AC_PREPROC_IFELSE(
       [AC_LANG_PROGRAM([[
           #if !defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
           # error not native windows
           #endif
       ]])],
       [windows_native=yes
        AC_DEFINE([WIN32_NATIVE], [1], [Native Windows build])
        AC_MSG_RESULT([yes])],
       [windows_native=no
        AC_MSG_RESULT([no])])
   AM_CONDITIONAL([WIN32_NATIVE], [test x$windows_native = xyes])

   if test x$windows_native = xyes; then
       LIBS="$LIBS -lws2_32 -ladvapi32 -liphlpapi -lcrypt32 -lsecur32 -luserenv"
   fi
   ```

   Record this change in `PORTING.md` under "Changes to upstream files".

2. Create `win32/win_compat.h` (umbrella header for Windows-specific types and stubs):

   ```c
   /* win32/win_compat.h
    * Umbrella header for the Windows native build. Included from rsync.h
    * when WIN32_NATIVE is defined.
    *
    * Goal: provide enough type/macro stubs to make the upstream code compile.
    * Behavior comes from the gnulib polyfills + the win32/win_*.c files.
    */
   #ifndef WIN32_COMPAT_H
   #define WIN32_COMPAT_H

   #if defined(WIN32_NATIVE)

   /* Windows headers */
   #ifndef WIN32_LEAN_AND_MEAN
   # define WIN32_LEAN_AND_MEAN
   #endif
   #include <windows.h>
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <io.h>

   /* Map missing POSIX types/macros to Windows equivalents.
    * gnulib provides most of these; we only stub what gnulib doesn't. */

   /* No setuid/setgid on Windows */
   typedef int uid_t;
   typedef int gid_t;
   #define geteuid() (0)
   #define getegid() (0)
   #define getuid()  (0)
   #define getgid()  (0)
   #define setuid(u) (errno = ENOSYS, -1)
   #define setgid(g) (errno = ENOSYS, -1)

   /* fork() does not exist. Any code path that calls fork() must be
    * #ifdef'd to use win_spawn_*() functions instead. We deliberately
    * leave fork() undeclared to catch missed sites at compile time. */

   /* chown / lchown — no-op stubs */
   #define chown(p, u, g)  (0)
   #define lchown(p, u, g) (0)

   /* File mode bits we lack */
   #ifndef S_IFLNK
   # define S_IFLNK 0120000
   #endif
   #ifndef S_ISLNK
   # define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
   #endif
   #ifndef S_ISSOCK
   # define S_ISSOCK(m) (0)
   #endif
   #ifndef S_ISFIFO
   # define S_ISFIFO(m) (0)
   #endif

   /* Forward declarations of our win32/ functions */
   #include "win32/win_spawn.h"
   #include "win32/win_reexec.h"
   #include "win32/win_paths.h"
   #include "win32/win_fs.h"
   #include "win32/win_ssh.h"

   #endif /* WIN32_NATIVE */
   #endif /* WIN32_COMPAT_H */
   ```

3. Add `#include "win32/win_compat.h"` to the top of `rsync.h` (upstream file), gated:

   ```c
   /* near the top of rsync.h, after the existing #includes */
   #if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
   # include "win32/win_compat.h"
   #endif
   ```

   Record in `PORTING.md`.

4. Create stub implementations for every `win32/win_*.c` file. Each stub must contain at minimum the function prototypes from its header, with bodies that return `errno = ENOSYS, -1` (or equivalent). This makes the link step succeed even before Phase 3.

   Skeleton for `win32/win_spawn.c`:

   ```c
   /* win32/win_spawn.c — Phase 2 stub. Phase 3 fleshes this out. */
   #include "rsync.h"
   #include "win32/win_spawn.h"

   pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out) {
       (void)argv; (void)f_in; (void)f_out;
       errno = ENOSYS;
       return (pid_t)-1;
   }
   ```

   Do the same for `win_reexec.c`, `win_paths.c`, `win_fs.c`, `win_ssh.c`, `win_child_init.c`, `stub_daemon.c`. Use empty implementations that compile.

5. Write `scripts/build-windows.ps1`:

   ```powershell
   # scripts/build-windows.ps1
   # Build rsync.exe with MSVC, static CRT, static deps from vcpkg.
   # Must be run from an x64 Native Tools Command Prompt for VS 2022 (or equivalent).
   $ErrorActionPreference = 'Stop'

   $repoRoot = (Get-Item $PSScriptRoot).Parent.FullName
   Set-Location $repoRoot

   $vcpkgInstalled = 'C:\vcpkg\installed\x64-windows-static'

   # Run autotools inside MSYS2
   & 'C:\msys64\usr\bin\bash.exe' -lc @"
   set -euo pipefail
   cd "$($repoRoot -replace '\\', '/')"
   export PATH=/usr/bin:/mingw64/bin
   autoreconf -fiv

   # CC wrapper from gnulib's build-aux turns gcc-style flags into MSVC flags
   export CC="`$PWD/build-aux/compile cl.exe -nologo"
   export AR="`$PWD/build-aux/ar-lib lib.exe -nologo"
   export CFLAGS='-MT -O2 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DWIN32_NATIVE -I'$vcpkgInstalled'/include'
   export LDFLAGS='-LIBPATH:'$vcpkgInstalled'/lib /DEFAULTLIB:libcmt /NODEFAULTLIB:msvcrt'
   export LIBS='libcrypto.lib zstd_static.lib lz4.lib xxhash.lib zlibstatic.lib ws2_32.lib advapi32.lib iphlpapi.lib crypt32.lib secur32.lib userenv.lib'

   ./configure \
       --host=x86_64-pc-windows \
       --build=x86_64-pc-msys \
       --disable-acl-support \
       --disable-xattr-support \
       --disable-md2man \
       --disable-locale \
       --disable-iconv-open \
       --enable-static

   make -j`$(nproc) rsync.exe
   "@

   if (-not (Test-Path 'rsync.exe')) {
       throw "rsync.exe was not produced"
   }
   Write-Host "Built rsync.exe — size $((Get-Item rsync.exe).Length) bytes"
   ```

6. Run the build:

   ```cmd
   powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1
   ```

   Iterate until `rsync.exe` is produced. Expect compilation errors that need fixing:
   - **Missing function**: add a gnulib module to `scripts/gnulib-import.sh` and re-run import. Examples: `getpwuid`, `getgrgid`, `readlink`, `lstat`.
   - **Conflict with Windows header**: gate the conflicting upstream code with `#ifndef WIN32_NATIVE`.
   - **Type mismatch**: usually a `pid_t` vs `HANDLE` or `uid_t` issue — extend `win_compat.h` stubs.
   - **Unresolved symbol at link**: missing library in `LIBS` env var or wrong vcpkg lib name. Check `vcpkg/installed/x64-windows-static/lib/` for actual `.lib` filenames.

   `[DECIDE]` If you hit a compile error that requires modifying upstream rsync source (not just `#ifdef` gating), STOP and ask the user. The strategy is to minimize upstream-source changes.

7. Verify the binary has no forbidden DLL dependencies:

   ```powershell
   dumpbin /DEPENDENTS rsync.exe
   ```

   Output should contain only DLLs from the allowed list in Section 1. If it shows any `cyg*`, `msys-*`, `lib*-*.dll`, `vcruntime*.dll`, or `msvcr*.dll`, the build is broken — investigate before declaring Phase 2 done.

### Done when
- `scripts/build-windows.ps1` runs to completion and produces `rsync.exe`.
- `dumpbin /DEPENDENTS rsync.exe` shows only allowed Windows system DLLs.
- `rsync.exe --version` runs without crashing (version output may or may not be sensible).
- It is OK if `rsync.exe -av src/ dst/` crashes — Phase 3 fixes that.

Tag: `git tag phase-2-complete && git push --tags`

---

## 10. PHASE 3 — Fork emulation (the hard part)

### Goal
Replace the 4 fork sites that the client uses. After this phase, basic upload (push from Windows to Linux server) and download (pull from Linux server to Windows) both work.

### The 4 fork sites (verified from rsync 3.4.2 source)

| # | Location | What it does | Replacement |
|---|---|---|---|
| 1 | `pipe.c::piped_child()` | fork + execvp(ssh) | `win_spawn_remote_shell()` using gnulib `create_pipe_bidi` |
| 2 | `pipe.c::local_child()` | fork + call `child_main()` in-process | `win_reexec_self_as_role()` |
| 3 | `main.c::do_recv()` | fork to split receiver/generator | `win_reexec_self_as_role()` (same machinery) |
| 4 | `main.c::shell_exec()` | fork + execlp($RSYNC_SHELL) | Collapse to `system(cmd)` always |

### Task 3.1 — Implement `win_spawn_remote_shell` (site 1)

`win32/win_spawn.h`:

```c
#ifndef WIN_SPAWN_H
#define WIN_SPAWN_H
#include <sys/types.h>

/* Spawn a remote-shell process (ssh) with bidirectional pipes.
 * On success: returns child pid, fills *f_in and *f_out with fds.
 * On failure: returns -1, errno set. */
pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out);

#endif
```

`win32/win_spawn.c`:

```c
/* win32/win_spawn.c
 * Replaces pipe.c::piped_child() on Windows.
 * Uses gnulib's spawn-pipe to spawn ssh with bidirectional pipes.
 */
#include "rsync.h"
#include "win32/win_spawn.h"
#include "lib/spawn-pipe.h"

pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out) {
    int fd[2];
    pid_t pid;

    /* progname is for error messages; use argv[0] */
    /* prog_path NULL means "search PATH" */
    /* null_stderr=false: child's stderr goes to parent's stderr */
    /* slave_process=true: child gets killed if we die */
    /* exit_on_error=false: return -1 on failure instead of exit() */
    pid = create_pipe_bidi(
        argv[0],        /* progname */
        argv[0],        /* prog_path */
        argv,           /* prog_argv */
        false,          /* null_stderr */
        true,           /* slave_process */
        false,          /* exit_on_error */
        fd);

    if (pid == (pid_t)-1) {
        return -1;
    }

    *f_in  = fd[0];     /* read from child stdout */
    *f_out = fd[1];     /* write to child stdin */
    return pid;
}
```

Then patch `pipe.c::piped_child()` to call this on Windows. Use `str_replace` to gate the existing body:

```c
pid_t piped_child(char **command, int *f_in, int *f_out)
{
#ifdef WIN32_NATIVE
    return win_spawn_remote_shell(command, f_in, f_out);
#else
    /* ... original body unchanged ... */
#endif
}
```

Record in `PORTING.md`.

### Task 3.2 — Re-exec machinery (sites 2 & 3)

This is the harder one. The pattern: when upstream code wants to fork into an in-process child (where the child continues executing a specific function instead of `exec`'ing another binary), we instead `CreateProcess` our own `rsync.exe` with a marker flag, and the child detects the flag at startup and dispatches to the right role.

`win32/win_reexec.h`:

```c
#ifndef WIN_REEXEC_H
#define WIN_REEXEC_H
#include <sys/types.h>

typedef enum {
    WIN_ROLE_LOCAL_CHILD,   /* pipe.c::local_child */
    WIN_ROLE_RECEIVER,      /* main.c::do_recv → receiver */
    WIN_ROLE_GENERATOR      /* (if needed; do_recv's parent continues as generator) */
} win_role_t;

/* Re-exec ourselves in the given role, with the given file descriptors
 * inherited via the marker file. Returns child pid, fills f_in/f_out.
 *
 * The caller passes argc/argv that the child should see. State is
 * serialized to a temp file and the path passed via env var. */
pid_t win_reexec_self_as(win_role_t role,
                          int argc, char **argv,
                          int *f_in, int *f_out);

#endif
```

`win32/win_reexec.c` (skeleton — fill in the serialization details):

```c
/* win32/win_reexec.c
 * Implements fork-of-self-with-different-function-pointer.
 *
 * Strategy:
 *   1. Create anonymous pipes for parent<->child communication.
 *   2. Serialize parsed options + role into a temp file.
 *   3. CreateProcessW with the same exe + a marker flag pointing at the
 *      temp file. Mark the pipe handles as inheritable.
 *   4. Parent retains its ends; child end is passed via STARTUPINFO.
 *
 * The child, in win_child_init.c, detects the marker, loads the state,
 * and dispatches to the right entry point.
 */
#include "rsync.h"
#include "win32/win_reexec.h"
#include "win32/win_child_init.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>

#define WIN_CHILD_FLAG "--_win_child"
#define WIN_CHILD_ENV  "RSYNC_WIN_CHILD_STATE"

static int serialize_state_to_tempfile(win_role_t role,
                                       int argc, char **argv,
                                       HANDLE in_handle, HANDLE out_handle,
                                       char *out_path, size_t out_path_len) {
    /* Get a temp file path */
    char temp_dir[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp_dir)) return -1;
    if (!GetTempFileNameA(temp_dir, "rsy", 0, out_path)) return -1;

    FILE *f = fopen(out_path, "wb");
    if (!f) return -1;

    /* Format: role, in_handle, out_handle, argc, argv[0]..argv[argc-1] */
    fprintf(f, "ROLE=%d\n", (int)role);
    fprintf(f, "IN=%llu\n", (unsigned long long)(uintptr_t)in_handle);
    fprintf(f, "OUT=%llu\n", (unsigned long long)(uintptr_t)out_handle);
    fprintf(f, "ARGC=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        /* Length-prefixed to handle args with newlines */
        fprintf(f, "ARG=%zu:%s\n", strlen(argv[i]), argv[i]);
    }
    fclose(f);
    return 0;
}

pid_t win_reexec_self_as(win_role_t role,
                         int argc, char **argv,
                         int *f_in, int *f_out) {
    /* 1. Create pipes */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE to_child_r, to_child_w, from_child_r, from_child_w;

    if (!CreatePipe(&to_child_r,   &to_child_w,   &sa, 0)) return -1;
    if (!CreatePipe(&from_child_r, &from_child_w, &sa, 0)) {
        CloseHandle(to_child_r); CloseHandle(to_child_w);
        return -1;
    }
    /* Parent's ends should not be inherited */
    SetHandleInformation(to_child_w,   HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(from_child_r, HANDLE_FLAG_INHERIT, 0);

    /* 2. Serialize state */
    char state_path[MAX_PATH];
    if (serialize_state_to_tempfile(role, argc, argv,
                                    to_child_r, from_child_w,
                                    state_path, MAX_PATH) < 0) {
        CloseHandle(to_child_r); CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        return -1;
    }

    /* 3. Build command line: <self.exe> --_win_child=<state_path> */
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    char cmdline[MAX_PATH * 4];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s=%s",
             self_path, WIN_CHILD_FLAG, state_path);

    /* 4. Spawn */
    STARTUPINFOA si = { sizeof(si) };
    /* Child's stdin/stdout are inherited as-is (the actual pipe handles
     * are passed via state file and dup'd in the child). */
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        0, NULL, NULL, &si, &pi)) {
        DeleteFileA(state_path);
        CloseHandle(to_child_r); CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        errno = EAGAIN;
        return -1;
    }

    /* Close handles the child owns */
    CloseHandle(to_child_r);
    CloseHandle(from_child_w);
    CloseHandle(pi.hThread);

    /* Convert HANDLEs to fds for the caller */
    *f_in  = _open_osfhandle((intptr_t)from_child_r, _O_BINARY);
    *f_out = _open_osfhandle((intptr_t)to_child_w,   _O_BINARY);

    return (pid_t)pi.dwProcessId;
}
```

`win32/win_child_init.c` — the child-side detection:

```c
/* win32/win_child_init.c
 * Called as the very first line of main() on Windows.
 * If we were spawned with --_win_child=<state_path>, load state and
 * dispatch to the appropriate role function.
 */
#include "rsync.h"
#include "win32/win_reexec.h"
#include "win32/win_child_init.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>

#define WIN_CHILD_FLAG "--_win_child="

/* Forward declarations of upstream entry points we dispatch to */
extern int child_main(int argc, char *argv[]);   /* pipe.c */
extern int do_recv_receiver_entry(int f_in, int f_out, char *local_name); /* see below */

int win_child_init(int argc, char **argv) {
    /* Quick scan for our marker */
    int marker_idx = -1;
    const char *state_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], WIN_CHILD_FLAG, strlen(WIN_CHILD_FLAG)) == 0) {
            marker_idx = i;
            state_path = argv[i] + strlen(WIN_CHILD_FLAG);
            break;
        }
    }
    if (marker_idx < 0) return -1;  /* not a child — proceed with normal main */

    /* Load state */
    FILE *f = fopen(state_path, "rb");
    if (!f) return -1;

    int role;
    unsigned long long in_handle, out_handle;
    int child_argc;
    char **child_argv = NULL;

    if (fscanf(f, "ROLE=%d\n", &role) != 1) goto err;
    if (fscanf(f, "IN=%llu\n", &in_handle) != 1) goto err;
    if (fscanf(f, "OUT=%llu\n", &out_handle) != 1) goto err;
    if (fscanf(f, "ARGC=%d\n", &child_argc) != 1) goto err;

    child_argv = malloc(sizeof(char*) * (child_argc + 1));
    for (int i = 0; i < child_argc; i++) {
        size_t arglen;
        if (fscanf(f, "ARG=%zu:", &arglen) != 1) goto err;
        child_argv[i] = malloc(arglen + 1);
        if (fread(child_argv[i], 1, arglen, f) != arglen) goto err;
        child_argv[i][arglen] = '\0';
        fgetc(f);  /* trailing newline */
    }
    child_argv[child_argc] = NULL;
    fclose(f);
    DeleteFileA(state_path);  /* clean up */

    /* Re-setup stdin/stdout from the inherited handles */
    int fd_in  = _open_osfhandle((intptr_t)(HANDLE)(uintptr_t)in_handle,  _O_BINARY);
    int fd_out = _open_osfhandle((intptr_t)(HANDLE)(uintptr_t)out_handle, _O_BINARY);
    if (fd_in < 0 || fd_out < 0) return -1;
    _dup2(fd_in,  0);   /* stdin */
    _dup2(fd_out, 1);   /* stdout */

    /* Dispatch */
    switch (role) {
    case WIN_ROLE_LOCAL_CHILD:
        return child_main(child_argc, child_argv);
    case WIN_ROLE_RECEIVER:
        /* For do_recv: enter as receiver. Globals will be reconfigured
         * by the upstream code. */
        am_receiver = 1;
        send_msgs_to_gen = am_server;
        return child_main(child_argc, child_argv);
    default:
        return -1;
    }

err:
    if (f) fclose(f);
    DeleteFileA(state_path);
    if (child_argv) {
        for (int i = 0; i < child_argc; i++) free(child_argv[i]);
        free(child_argv);
    }
    return -1;
}
```

`win32/win_child_init.h`:

```c
#ifndef WIN_CHILD_INIT_H
#define WIN_CHILD_INIT_H

/* Returns -1 if not a re-exec'd child (caller proceeds with normal main).
 * Returns the child's exit code if it was a child (caller should exit with it). */
int win_child_init(int argc, char **argv);

#endif
```

Patch `main.c::main()` — first line:

```c
int main(int argc, char *argv[]) {
#ifdef WIN32_NATIVE
    {
        int child_rc = win_child_init(argc, argv);
        if (child_rc >= 0) return child_rc;
    }
#endif
    /* ... rest of original main ... */
}
```

Then patch `pipe.c::local_child()`:

```c
pid_t local_child(int argc, char **argv, int *f_in, int *f_out,
                  int (*child_main)(int, char*[])) {
#ifdef WIN32_NATIVE
    (void)child_main;  /* always dispatched via win_child_init */
    return win_reexec_self_as(WIN_ROLE_LOCAL_CHILD, argc, argv, f_in, f_out);
#else
    /* ... original body ... */
#endif
}
```

And patch `main.c::do_recv()`:

```c
static int do_recv(int f_in, int f_out, char *local_name) {
#ifdef WIN32_NATIVE
    int error_pipe[2];
    if (fd_pair(error_pipe) < 0) {
        rsyserr(FERROR, errno, "pipe failed in do_recv");
        exit_cleanup(RERR_IPC);
    }
    /* On Windows, re-exec ourselves as the receiver. The generator
     * continues in the current process. */
    pid_t pid = win_reexec_self_as(WIN_ROLE_RECEIVER,
                                    cooked_argc, cooked_argv,
                                    /* receiver reads error_pipe[1] and inherits f_in/f_out */
                                    /* see win_reexec for handle plumbing */
                                    NULL, NULL);
    if (pid < 0) {
        rsyserr(FERROR, errno, "fork failed in do_recv");
        exit_cleanup(RERR_IPC);
    }
    /* ... rest of generator code unchanged ... */
#else
    /* ... original body ... */
#endif
}
```

`[DECIDE]` The `do_recv` patch is the trickiest: the upstream code uses globals (`am_receiver`, `send_msgs_to_gen`, etc.) set after the fork in the child, and pipe descriptors are inherited via memory not file descriptors. The skeleton above is a starting point — you will likely need to extend the state serialization to include enough globals. Stop and ask the user when you hit the wall on state preservation.

### Task 3.3 — Replace `shell_exec` (site 4)

Patch `main.c::shell_exec()`:

```c
int shell_exec(const char *cmd) {
#ifdef WIN32_NATIVE
    /* No fork() on Windows; just use system(). The RSYNC_SHELL env var
     * is honored implicitly via cmd.exe's shell resolution. */
    return system(cmd);
#else
    /* ... original body ... */
#endif
}
```

### Task 3.4 — Stub the daemon-only sites (5, 6, 7)

Create `win32/stub_daemon.c`:

```c
/* win32/stub_daemon.c
 * Provides error stubs for daemon-only functions that we excise.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE

int start_daemon(int f_in, int f_out) {
    (void)f_in; (void)f_out;
    rprintf(FERROR, "rsync daemon mode is not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

int daemon_main(void) {
    rprintf(FERROR, "rsync daemon mode is not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

int start_accept_loop(int port, int (*fn)(int, int)) {
    (void)port; (void)fn;
    rprintf(FERROR, "rsync daemon mode is not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

int start_socket_client(char *host, int remote_argc, char *remote_argv[],
                        int argc, char *argv[]) {
    (void)host; (void)remote_argc; (void)remote_argv; (void)argc; (void)argv;
    rprintf(FERROR, "rsync:// daemon connections are not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

#endif
```

Then `#ifdef WIN32_NATIVE` out the bodies of these functions in `clientserver.c` and `socket.c`. This is finicky — the cleanest approach is to wrap the *entire* file in `#ifndef WIN32_NATIVE` (since we're providing replacements in `stub_daemon.c`).

### Done when
- `rsync -av <local-file> user@<remote-host>:/tmp/` successfully transfers a file from Windows to a Linux rsync server.
- `rsync -av user@<remote-host>:/tmp/<file> .` successfully downloads.
- Verify by SHA256-comparing source and destination.

Tag: `git tag phase-3-complete && git push --tags`

---

## 11. PHASE 4 — Filesystem & path handling

### Goal
Make Windows paths work correctly. Specifically: drive letters, UNC, long paths, symlinks, and timestamps.

### Task 4.1 — Drive-letter / UNC patch in `check_for_hostspec` (the cwRsync bug)

Patch `options.c::check_for_hostspec()`. Add this block at the top of the function, before any existing code:

```c
char *check_for_hostspec(char *s, char **host_ptr, int *port_ptr) {
    char *path;

#ifdef WIN32_NATIVE
    /* Windows drive letter: "X:" optionally followed by '\' or '/' or
     * end-of-string. Treat as local. This avoids cwRsync's classic bug
     * of interpreting "C:\Users\..." as host "C" with path "\Users\...". */
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':' &&
        (s[2] == '\0' || s[2] == '\\' || s[2] == '/')) {
        *host_ptr = NULL;
        return NULL;
    }
    /* UNC path: \\server\share or \\?\... long-path prefix or \\.\... */
    if (s[0] == '\\' && s[1] == '\\') {
        *host_ptr = NULL;
        return NULL;
    }
#endif

    /* ... rest of function unchanged ... */
```

Document the edge case in `KNOWN-ISSUES.md`:

```markdown
## Path parsing: drive-relative form

The Windows drive-relative form `C:foo` (where `foo` is resolved against the
current working directory of drive C:) is intentionally parsed as a REMOTE
host specification, not local. This matches cwRsync behavior. To force local
interpretation, prefix with `.\`:

    rsync .\C:foo dst/    # local
    rsync C:foo dst/      # parsed as host "C", path "foo"
```

### Task 4.2 — Long-path support

Add a Windows application manifest. Create `win32/rsync.manifest`:

```xml
<?xml version='1.0' encoding='UTF-8' standalone='yes'?>
<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>
  <application xmlns='urn:schemas-microsoft-com:asm.v3'>
    <windowsSettings>
      <longPathAware xmlns='http://schemas.microsoft.com/SMI/2016/WindowsSettings'>true</longPathAware>
      <activeCodePage xmlns='http://schemas.microsoft.com/SMI/2019/WindowsSettings'>UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
```

Embed it into the binary via `mt.exe`. Add to `scripts/build-windows.ps1` after the `make` step:

```powershell
& "$env:WindowsSdkVerBinPath\x64\mt.exe" -manifest win32/rsync.manifest -outputresource:"rsync.exe;1"
```

### Task 4.3 — Path normalization in `win_paths.c`

```c
/* win32/win_paths.h */
#ifndef WIN_PATHS_H
#define WIN_PATHS_H

/* Convert backslashes to forward slashes for internal use.
 * rsync internally uses forward slashes; user-facing paths and
 * filesystem calls use whatever Windows accepts. */
char *win_normalize_path(char *path);

/* Add \\?\ prefix if path exceeds MAX_PATH (260 chars).
 * Returns malloc'd string; caller frees. */
char *win_long_path_prefix(const char *path);

/* True if path starts with a Windows drive letter (X:) */
int win_is_drive_path(const char *path);

/* True if path is a UNC path (\\server\share) */
int win_is_unc_path(const char *path);

#endif
```

Implement in `win32/win_paths.c`. Apply normalization at the boundary where rsync hands paths to system calls — gnulib's polyfills handle most of this, but verify with the test matrix.

### Task 4.4 — Symlinks via `CreateSymbolicLinkW`

Implement in `win32/win_fs.c`. Use `CreateSymbolicLinkW` with `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` (requires Win10 1703+ in Developer Mode or admin). Falls back to copying with a warning if unprivileged creation fails.

`[DECIDE]` Symlink behavior on Windows is contentious. Three options:
- **A**: Silent fallback to file copy on permission failure (user-friendly, but breaks `--archive` semantics).
- **B**: Hard error on permission failure (correct but annoying for users without Dev Mode).
- **C**: Warning + fallback (compromise).

Default to **C**. Ask the user if they prefer otherwise.

### Task 4.5 — Permissions / ownership stubs

In `win32/win_fs.c`:

```c
/* chmod is mostly a no-op. We map the user-write bit to FILE_ATTRIBUTE_READONLY. */
int win_chmod(const char *path, mode_t mode) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return -1;
    if (mode & 0200) attrs &= ~FILE_ATTRIBUTE_READONLY;
    else             attrs |=  FILE_ATTRIBUTE_READONLY;
    return SetFileAttributesA(path, attrs) ? 0 : -1;
}
/* chown/lchown are already no-ops via win_compat.h */
```

Wire up in `syscall.c` (upstream file) with `#ifdef WIN32_NATIVE` gates.

### Task 4.6 — Timestamps

Map `utime`/`utimens` to `SetFileTime` with FILETIME conversion. gnulib's `utimens` module already does this on Windows — verify by testing `-t` flag.

### Done when
- `rsync -av C:\Users\test\src\ user@host:/tmp/dst/` works (no "Could not resolve hostname c" error).
- `rsync -av user@host:/etc/ C:\Temp\etc-backup\` works.
- Round-trip preserves mtime within 1 second.
- Long paths (>260 chars) round-trip without truncation.
- `--links` flag preserves symlinks (with the chosen fallback behavior).

Tag: `git tag phase-4-complete && git push --tags`

---

## 12. PHASE 5 — SSH integration

### Goal
Make `ssh` discovery sensible on Windows, defaulting to the built-in OpenSSH.

### Task 5.1 — Default ssh path resolution

Implement `win32/win_ssh.c`:

```c
#include "rsync.h"
#include "win32/win_ssh.h"
#include <windows.h>

const char *win_default_rsh(void) {
    static char buf[MAX_PATH];

    /* 1. Honor existing rsync env var (no change in semantics) */
    DWORD n = GetEnvironmentVariableA("RSYNC_RSH", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return buf;

    /* 2. Try Windows built-in OpenSSH */
    char winroot[MAX_PATH];
    n = GetEnvironmentVariableA("SystemRoot", winroot, sizeof(winroot));
    if (n > 0 && n < sizeof(winroot)) {
        snprintf(buf, sizeof(buf), "%s\\System32\\OpenSSH\\ssh.exe", winroot);
        if (GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES) return buf;
    }

    /* 3. Fall back to PATH lookup */
    return "ssh.exe";
}
```

Patch `options.c` (or wherever `RSYNC_RSH` is read) to call `win_default_rsh()` instead of the hardcoded `"ssh"` on Windows.

### Task 5.2 — Remote command quoting

rsync already escapes the remote command using POSIX sh rules (`safe_arg()` in upstream code). Keep this — the remote side runs a Unix shell. The local-side `CreateProcessW` quoting (for the `ssh.exe` argv) follows MSVC CRT rules; gnulib's `spawn-pipe` handles this correctly. No change needed.

### Done when
- Without `RSYNC_RSH` set, `rsync.exe` finds and uses `C:\Windows\System32\OpenSSH\ssh.exe`.
- With `RSYNC_RSH=plink`, it uses `plink`.
- With `-e "ssh -p 2222"`, it passes the port flag correctly.

Tag: `git tag phase-5-complete && git push --tags`

---

## 13. PHASE 6 — Daemon excision

### Goal
Cleanly remove all daemon code paths so they can't accidentally be invoked.

### Tasks

1. In `options.c`, reject `--daemon`, `--config`, and `--no-detach` with an error message on Windows:

   ```c
   #ifdef WIN32_NATIVE
   case OPT_DAEMON:
       rprintf(FERROR, "--daemon is not supported in this Windows build\n");
       exit_cleanup(RERR_UNSUPPORTED);
   #endif
   ```

2. In `options.c::parse_arguments` or wherever URLs are parsed, reject `rsync://` URLs early:

   ```c
   #ifdef WIN32_NATIVE
   if (strncasecmp(arg, "rsync://", 8) == 0) {
       rprintf(FERROR, "rsync:// daemon URLs are not supported in this Windows build\n");
       exit_cleanup(RERR_UNSUPPORTED);
   }
   #endif
   ```

3. `#ifdef`-out `clientserver.c`, `loadparm.c`, `access.c`, `authenticate.c` so they compile to empty translation units on Windows. The stubs in `win32/stub_daemon.c` provide any symbols still referenced.

4. Update `KNOWN-ISSUES.md`:

   ```markdown
   ## Unsupported in this build
   - `--daemon` mode (run as rsyncd)
   - `--config=FILE` (rsyncd.conf)
   - `rsync://host/module/path` URLs (daemon connections)
   - Connecting to a remote daemon via `host::module` syntax

   All of the above are intentional. Use rsync-over-ssh instead.
   ```

### Done when
- `rsync --daemon` exits with a clear error message and `RERR_UNSUPPORTED` status.
- `rsync rsync://example.com/mod/path /tmp/` exits with a clear error message.
- The binary is smaller than the pre-excision build (proves dead code is gone).

Tag: `git tag phase-6-complete && git push --tags`

---

## 14. PHASE 7 — CI

### Goal
Automate the build on GitHub Actions. Every push to `win32-port` produces an artifact; every tag matching `v*` produces a GitHub Release.

### Task 7.1 — Main build workflow

Create `.github/workflows/build.yml`:

```yaml
name: build-rsync-native-windows

on:
  push:
    branches: [win32-port]
    tags: ['v*']
  pull_request:
  workflow_dispatch:

permissions:
  contents: write

jobs:
  build:
    runs-on: windows-latest
    timeout-minutes: 60

    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Set up MSVC environment
        uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: x64

      - name: Set up MSYS2 (build environment only — not runtime)
        uses: msys2/setup-msys2@v2
        with:
          msystem: MSYS
          update: true
          install: >-
            base-devel autotools make tar curl python git
            patch sed gawk gperf bison flex texinfo

      - name: Cache vcpkg
        uses: actions/cache@v4
        with:
          path: |
            C:\vcpkg\installed
            C:\vcpkg\downloads
          key: vcpkg-x64-windows-static-${{ hashFiles('vcpkg/vcpkg.json') }}

      - name: Install vcpkg dependencies (static)
        shell: pwsh
        run: |
          C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg

      - name: Import gnulib modules
        shell: msys2 {0}
        run: |
          chmod +x scripts/gnulib-import.sh
          ./scripts/gnulib-import.sh

      - name: Build
        shell: pwsh
        run: pwsh ./scripts/build-windows.ps1

      - name: Verify fully static
        shell: pwsh
        run: pwsh ./scripts/verify-static.ps1 rsync.exe

      - name: Smoke test
        shell: pwsh
        run: pwsh ./scripts/smoke-test.ps1 rsync.exe

      - name: Archive
        shell: pwsh
        run: |
          $version = git describe --tags --always
          $archive = "rsync-native-${version}-windows-x86_64.7z"
          New-Item -ItemType Directory stage | Out-Null
          Copy-Item rsync.exe stage/
          Copy-Item COPYING stage/LICENSE-rsync.txt
          Copy-Item PORTING.md stage/PORTING.txt
          Copy-Item KNOWN-ISSUES.md stage/KNOWN-ISSUES.txt
          & 'C:\Program Files\7-Zip\7z.exe' a -mx=9 $archive .\stage\*
          (Get-FileHash $archive -Algorithm SHA256).Hash | Out-File "$archive.sha256"

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: rsync-native-windows-x86_64
          path: |
            rsync-native-*-windows-x86_64.7z
            rsync-native-*-windows-x86_64.7z.sha256

      - name: Release
        if: startsWith(github.ref, 'refs/tags/v')
        uses: softprops/action-gh-release@v2
        with:
          files: |
            rsync-native-*-windows-x86_64.7z
            rsync-native-*-windows-x86_64.7z.sha256
          generate_release_notes: true
```

### Task 7.2 — Verification script

Create `scripts/verify-static.ps1`:

```powershell
param([Parameter(Mandatory=$true)][string]$Binary)
$ErrorActionPreference = 'Stop'

$raw = & dumpbin /DEPENDENTS $Binary
$deps = $raw | Where-Object { $_ -match '\.dll$' } | ForEach-Object { $_.Trim().ToLower() }

# Allowed Windows system DLLs
$allowedExact = @(
    'kernel32.dll', 'kernelbase.dll', 'ntdll.dll',
    'advapi32.dll', 'user32.dll', 'ws2_32.dll',
    'crypt32.dll', 'secur32.dll', 'bcrypt.dll',
    'iphlpapi.dll', 'userenv.dll', 'shell32.dll',
    'ole32.dll', 'oleaut32.dll', 'rpcrt4.dll',
    'ucrtbase.dll'
)
$allowedPattern = 'api-ms-win-*'

# Forbidden patterns (any match = build broken)
$forbiddenPatterns = @(
    'cyg*', 'msys-*', 'lib*-*.dll',
    'vcruntime*.dll', 'msvcp*.dll', 'msvcr*.dll'
)

$bad = @()
foreach ($d in $deps) {
    foreach ($p in $forbiddenPatterns) {
        if ($d -like $p) {
            $bad += "FORBIDDEN: $d (matches $p)"
        }
    }
    $ok = $allowedExact -contains $d
    if (-not $ok -and $d -like $allowedPattern) { $ok = $true }
    if (-not $ok) {
        $bad += "UNKNOWN: $d (not in allowlist)"
    }
}

if ($bad.Count -gt 0) {
    Write-Error "Static link verification FAILED:`n$($bad -join "`n")"
    exit 1
}
Write-Host "OK: $Binary depends only on allowed Windows system DLLs."
```

### Task 7.3 — Smoke test

Create `scripts/smoke-test.ps1`:

```powershell
param([Parameter(Mandatory=$true)][string]$Binary)
$ErrorActionPreference = 'Stop'

# 1. --version runs without crashing
& $Binary --version
if ($LASTEXITCODE -ne 0) { throw "rsync --version failed" }

# 2. --help runs
& $Binary --help | Out-Null
if ($LASTEXITCODE -ne 0) { throw "rsync --help failed" }

# 3. Local-to-local copy works
$src = Join-Path $env:TEMP "rsync-smoke-src"
$dst = Join-Path $env:TEMP "rsync-smoke-dst"
Remove-Item -Recurse -Force $src, $dst -ErrorAction SilentlyContinue
New-Item -ItemType Directory $src | Out-Null
"test content" | Out-File "$src\file.txt"

& $Binary -av "$src\" "$dst\"
if ($LASTEXITCODE -ne 0) { throw "rsync local copy failed" }
if (-not (Test-Path "$dst\file.txt")) { throw "destination file missing" }

# 4. Daemon mode rejected
& $Binary --daemon 2>$null
if ($LASTEXITCODE -ne 14) {
    # RERR_UNSUPPORTED = 14
    Write-Warning "Expected exit 14 (RERR_UNSUPPORTED) for --daemon, got $LASTEXITCODE"
}

Write-Host "Smoke tests passed."
```

### Task 7.4 — Upstream sync workflow

Create `.github/workflows/upstream-sync.yml`:

```yaml
name: upstream-sync

on:
  schedule:
    - cron: '0 6 * * 1'  # Mondays 06:00 UTC
  workflow_dispatch:

permissions:
  contents: write
  pull-requests: write

jobs:
  sync:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Fetch upstream
        run: |
          git remote add upstream https://github.com/RsyncProject/rsync.git
          git fetch upstream

      - name: Update main mirror branch
        run: |
          git checkout -B main upstream/master
          git push origin main --force-with-lease

      - name: Open PR if new tag
        uses: peter-evans/create-pull-request@v7
        with:
          branch: upstream-update
          title: 'Sync with upstream rsync'
          body: 'Automated weekly upstream sync.'
          base: win32-port
```

### Done when
- A push to `win32-port` triggers a successful build on GitHub Actions.
- Artifact `rsync-native-*-windows-x86_64.7z` is downloadable from the Actions run.
- Tagging `v0.1.0-test` creates a GitHub Release with the artifact attached.

Tag: `git tag phase-7-complete && git push --tags`

---

## 15. PHASE 8 — Validation

### Goal
Run the full test matrix against a real Linux rsync server. The user provides ssh access to a test server.

`[DECIDE]` Ask the user for ssh access details to a Linux test server. If they don't have one, suggest spinning up a free-tier VM (e.g., Oracle Cloud, Hetzner Cloud) and pause until they have one ready.

### Test matrix (all must pass)

For each test, document command + expected behavior + actual outcome in `tests/validation-log.md`.

| # | Test | Command |
|---|---|---|
| 1 | Basic upload | `rsync -avz C:\test\src\ user@host:/tmp/dst/` |
| 2 | Basic download | `rsync -avz user@host:/tmp/src/ C:\test\dst\` |
| 3 | `--delete` honored | upload then add file in src; sync with `--delete`; file should appear in dst |
| 4 | `--link-dest` hardlinks | two-snapshot backup with `--link-dest=../prev` |
| 5 | `--partial --progress` resumes | interrupt 2GB transfer; restart with `--partial`; should resume |
| 6 | Large file (>2GB) | upload a 3GB sparse file; verify size & content |
| 7 | Many small files (>100k) | rsync a directory of 100,000 1KB files |
| 8 | Long path (>260 chars) | create deeply nested directory; round-trip |
| 9 | UTF-8 filenames | files with Cyrillic, CJK, emoji names; round-trip |
| 10 | Symlinks | `rsync -av --links` with a symlinked directory |
| 11 | Hardlinks | `rsync -avH` with hard-linked files |
| 12 | Ctrl-C interrupt | start large transfer; Ctrl-C; verify graceful exit |
| 13 | Bandwidth limit | `--bwlimit=1000` should actually limit to ~1MB/s |
| 14 | Compression negotiation | `-z` against a 3.4.x server should negotiate zstd |
| 15 | Checksum mode | `--checksum --cc=sha256` should use OpenSSL EVP |

### Comparison baseline
Run the same tests with the MSYS2 build of rsync 3.4.2 for comparison. Document differences in `tests/validation-log.md`.

### Done when
- All 15 tests in `tests/validation-log.md` are marked PASS.
- Performance within 20% of the MSYS2 baseline for upload and download.

Tag: `git tag phase-8-complete && git push --tags`

---

## 16. PHASE 9 — Ship

### Goal
Cut v1.0.0.

### Tasks

1. Write a proper `README.md` (rename current placeholder):

   ```markdown
   # rsync-native-windows

   A native Windows port of rsync. Single static executable, no Cygwin
   or MSYS2 runtime dependency.

   ## Status
   v1.0.0 — client only, protocol 32 compatible.

   ## Install
   Download `rsync-native-*.7z` from the [Releases](../../releases) page.
   Extract `rsync.exe` to any directory in your PATH.

   ## Requires
   - Windows 10 1809+ / Windows 11 / Server 2019+
   - For SSH transport: built-in OpenSSH (installed by default on supported OS)

   ## What's supported
   - Client mode (push and pull)
   - SSH transport (over built-in OpenSSH)
   - Local-to-local copies
   - Protocol 32 (rsync 3.4.x)
   - All standard rsync options: -a, -v, -z, --delete, --link-dest, --partial, --progress, --bwlimit, --checksum, etc.
   - Static binary: no DLLs to install

   ## What's NOT supported (intentional)
   - Daemon mode (`--daemon`)
   - rsync:// URLs
   - ACLs (`-A`)
   - Extended attributes (`-X`)
   - chown/chgrp on the receiving Windows side
   - SELinux contexts

   See [KNOWN-ISSUES.md](KNOWN-ISSUES.md) for details.

   ## License
   GPL-3.0-or-later (inherited from upstream rsync). See `LICENSE-rsync.txt`.

   ## Building
   See [BUILD.md](BUILD.md).

   ## Contributing
   Patches welcome via PR. Windows-specific changes go in `win32/`.
   Cross-platform fixes should be sent upstream to [RsyncProject/rsync](https://github.com/RsyncProject/rsync) first.
   ```

2. Write a proper `BUILD.md` (filled in based on what Phases 1-7 actually need).

3. Fill out `CHANGELOG.md`:

   ```markdown
   # Changelog

   ## v1.0.0 (UNRELEASED)
   - First release. Based on upstream rsync v3.4.2.
   - Native Windows port with MSVC, no Cygwin/MSYS2 runtime dependency.
   - Client only; daemon mode excised.
   - All hashing (MD4/MD5/SHA*/xxhash) statically linked via OpenSSL + xxhash.
   - Compression: zstd, lz4, zlib all statically linked.
   - SSH transport via Windows built-in OpenSSH.
   ```

4. Tag and push:

   ```bash
   git tag -a v1.0.0 -m "First release"
   git push origin v1.0.0
   ```

   The CI will build, run verify-static, run smoke-test, and publish the release.

### Done when
- v1.0.0 is tagged.
- GitHub Release page shows the artifact with SHA256.
- A fresh Windows VM can download the artifact, extract it, and successfully `rsync -av` to a Linux server.

---

## A. Reference: complete file paths and where edits go

| Action | File | Type |
|---|---|---|
| Top-level Windows detect | `configure.ac` | M4 |
| Include Windows compat | `rsync.h` | C header |
| Fork site 1 (piped_child) | `pipe.c` | C |
| Fork site 2 (local_child) | `pipe.c` | C |
| Fork site 3 (do_recv) | `main.c` | C |
| Fork site 4 (shell_exec) | `main.c` | C |
| Child entry hook | `main.c` (first line of main) | C |
| Path parser (colon bug) | `options.c::check_for_hostspec` | C |
| Daemon stubs | `clientserver.c`, `socket.c`, `loadparm.c`, `access.c`, `authenticate.c` | C (mostly wrapped) |
| Symlink syscalls | `syscall.c` | C |
| Permission stubs | `syscall.c` | C |

All new code in `win32/`. All edits to upstream files use `#ifdef WIN32_NATIVE` and are documented in `PORTING.md`.

---

## B. Failure modes and `[DECIDE]` points (consolidated)

| Trigger | Action |
|---|---|
| Cannot determine vcpkg release SHA | Ask user |
| Cannot determine gnulib pinned SHA | Ask user (try most recent commit, document risk) |
| Compile error requires editing upstream `.c` body (not just adding `#ifdef`) | Stop and ask user; propose the minimal change |
| Symlink permission policy choice | Default to warning+fallback; ask if user prefers stricter |
| Test server access for Phase 8 | Ask user |
| Performance >20% slower than MSYS2 baseline | Investigate; if it can't be fixed, ask user whether to ship anyway |
| Phase 3 do_recv state serialization wall | Stop and ask user; the receiver/generator split is the most fragile piece |
| Any forbidden DLL in `verify-static.ps1` output | STOP — do not advance; this means the static link is broken |
| Phase advances out of order | Don't do it; phases have ordering for a reason |

---

## C. Code style conventions for this project

- All Windows-specific code: `#ifdef WIN32_NATIVE`, never `#ifdef _WIN32` (the latter is too broad — Cygwin defines it too).
- All new files in `win32/` have a header comment block stating purpose and the upstream code path being replaced.
- Every edit to an upstream file appends a row to the table in `PORTING.md`.
- Wide-character (UTF-16) APIs only when necessary; prefer the `A` variants with UTF-8 active code page (set in manifest).
- Memory: match upstream style (use `new_array`, `free_array` if rsync code does).
- Errors: use rsync's `rprintf(FERROR, ...)` and `exit_cleanup(RERR_*)`, not stderr/exit directly.

---

## D. What NOT to do

- Do not "improve" upstream rsync code. Minimize touches. Future merges from upstream are the constraint.
- Do not pull in additional vcpkg dependencies without `[DECIDE]`. Every dep is a supply-chain attack surface.
- Do not try to implement a real `fork()` on Windows (winnie/process-cloning libraries exist but are fragile across Windows versions).
- Do not enable ACL or xattr support; the Linux semantics don't map cleanly.
- Do not support arm64 in v1.0 — defer to v1.1.
- Do not bundle ssh.exe; we rely on the Windows-shipped OpenSSH.
- Do not commit vcpkg, MSYS2, or VS Build Tools binaries to the repo.

---

## E. Maintenance after v1.0

- Weekly: `upstream-sync.yml` runs; review the PR if it opens.
- Monthly: check vcpkg for OpenSSL CVE fixes; bump `vcpkg.json` if needed.
- On upstream rsync release: bump `upstream-tracking` branch to new tag, rebase `win32-port` onto it, fix conflicts in `#ifdef WIN32_NATIVE` blocks, re-run validation matrix.
- On rsync CVE: cherry-pick the fix to `win32-port`, run validation matrix, cut a `v1.0.x` release.

---

## F. Glossary

- **upstream**: `RsyncProject/rsync` on GitHub. The canonical rsync source.
- **fork**: the user's GitHub fork of upstream (where this code lives).
- **win32-port**: the work branch in the fork.
- **upstream-tracking**: a branch in the fork pinned to a specific upstream tag.
- **gnulib**: GNU's portability shim library. Submoduled at `third_party/gnulib/`.
- **vcpkg**: Microsoft's C++ package manager. Used for static deps.
- **MSVC `/MT`**: static C runtime linkage (vs `/MD` dynamic).
- **WIN32_NATIVE**: the C macro that gates all our Windows-specific code.
- **RERR_UNSUPPORTED**: rsync's error code 14 (used for features we excised).
- **Fork site**: a location in upstream code that calls `fork()` — we replace these.

---

## G. Initial commit message templates

Use these for consistent history:

```
Phase 0: Initialize win32-port directory skeleton
Phase 1: Import gnulib modules and vcpkg manifest
Phase 2: Add WIN32_NATIVE compile guards and win32/ stubs
Phase 3a: Implement piped_child replacement (win_spawn_remote_shell)
Phase 3b: Implement re-exec machinery (local_child, do_recv)
Phase 3c: Replace shell_exec with system() on Windows
Phase 3d: Excise daemon code paths
Phase 4a: Drive-letter and UNC support in check_for_hostspec
Phase 4b: Long-path manifest and prefix support
Phase 4c: Symlink, hardlink, and timestamp handling
Phase 5: SSH default path resolution via win_default_rsh
Phase 6: Reject daemon flags and rsync:// URLs
Phase 7: GitHub Actions build, verify, release
Phase 8: Validation log (15 tests passing)
Phase 9: v1.0.0 release
```

---

END OF PLAN.md
