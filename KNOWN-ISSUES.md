# KNOWN-ISSUES.md — rsync-native-windows

## Unsupported in this build

The following upstream rsync features are intentionally excised or stubbed:

- `--daemon` mode (run as `rsyncd`)
- `--config=FILE` (rsyncd.conf parsing)
- ACLs (`-A` / `--acls`)
- Extended attributes (`-X` / `--xattrs`)
- chown / chgrp on the receiving Windows side
- SELinux contexts

ACL/xattr/POSIX-ownership flags will silently no-op on the Windows
side; the wire protocol still negotiates them when talking to a Linux
peer.

## Supported

- `rsync://host[:port]/module/path` URLs and `host::module/path` syntax
  for connecting OUT to a remote rsync daemon. Password auth via
  `RSYNC_PASSWORD` env var, `--password-file=FILE`, or interactive
  prompt. The daemon protocol is plain TCP -- there's no transport
  encryption; for sensitive transfers, tunnel through SSH
  (`ssh -L 8730:localhost:873 user@host` then
  `rsync rsync://localhost:8730/mod/path .`).

## Path parsing: drive-relative form

The Windows drive-relative form `C:foo` (where `foo` is resolved against the
current working directory of drive `C:`) is parsed as a REMOTE host
specification, not a local path. This matches cwRsync behavior. To force
local interpretation, prefix with `.\`:

```
rsync .\C:foo dst/    # local
rsync C:foo dst/      # parsed as host "C", path "foo"
```

Absolute drive paths (`C:\foo`, `C:/foo`) and bare drive references
(`C:`) are always treated as local — these are the common cases.

## Long paths

Long-path awareness (>260 characters) is enabled via the embedded
application manifest (`win32/rsync.manifest`). On systems without the
LongPathsEnabled registry setting, individual filesystem APIs may still
reject long paths; rsync transparently retries with the `\\?\` prefix
where possible.

## Symlinks

Creating symlinks requires either:

- Windows Developer Mode enabled (Windows 10 1703+), OR
- Running as Administrator, OR
- The user having `SeCreateSymbolicLinkPrivilege`

If symlink creation fails for permission reasons, `rsync` falls back to
copying the target's contents and emits a warning. To make this fatal
instead, set the env var `RSYNC_STRICT_SYMLINKS=1`.

## SSH client discovery

`rsync` searches for ssh in this order:

1. `$RSYNC_RSH` environment variable
2. `%SystemRoot%\System32\OpenSSH\ssh.exe` (Windows built-in OpenSSH)
3. `ssh.exe` on `PATH`

Override with `-e <command>` if needed.
