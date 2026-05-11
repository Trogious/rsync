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

## File map (Windows-specific code)

- `win32/` — all native Windows replacements and stubs
- `third_party/gnulib/` — gnulib submodule (POSIX polyfills)
- In-tree `#ifdef WIN32_NATIVE` blocks: tracked in the table below

## Changes to upstream files

_Add an entry every time you modify an upstream `.c` or `.h` file._

| File | Change | Reason |
|---|---|---|
| (none yet) | | |

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

## Conventions

- All Windows-specific code is gated by `#ifdef WIN32_NATIVE` (never `_WIN32`,
  which is too broad and matches Cygwin).
- New files live in `win32/` with a header comment block stating purpose and
  the upstream code path being replaced.
- Use `rprintf(FERROR, ...)` and `exit_cleanup(RERR_*)` rather than
  `fprintf(stderr, ...)` / `exit()` directly.
