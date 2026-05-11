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
