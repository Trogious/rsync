# BUILD.md — rsync-native-windows

How to build `rsync.exe` from source.

> **Status:** placeholder. Filled in during Phase 7 once the build pipeline
> is validated end-to-end on Windows CI.

## Prerequisites (Windows host)

- Windows 10 1809+ / Windows 11 / Server 2019+ (x64)
- Visual Studio 2022 Build Tools with the "Desktop development with C++"
  workload (provides `cl.exe`, `lib.exe`, `link.exe`, `mt.exe`)
- MSYS2 (for the autotools build-time shell only — not a runtime dep)
- `git` with submodule support

## One-time bootstrap

From an elevated PowerShell prompt:

```powershell
.\scripts\bootstrap-windows.ps1
```

This installs MSYS2, the required pacman packages, and vcpkg (to `C:\vcpkg`).

## Build

From an "x64 Native Tools Command Prompt for VS 2022":

```cmd
git submodule update --init --recursive
C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
```

The build produces `rsync.exe` at the repo root.

## Verify

```powershell
.\scripts\verify-static.ps1 .\rsync.exe
.\scripts\smoke-test.ps1 .\rsync.exe
```

`verify-static.ps1` fails if `rsync.exe` depends on any DLL not on the
allowlist (see `scripts/verify-static.ps1` for the list).

## Cross-building from Linux

Not supported. The build uses MSVC `cl.exe` and Windows SDK tools that
have no Linux equivalent. Source edits can be made on any platform, but
the build itself must run on Windows.
