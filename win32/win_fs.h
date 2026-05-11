/* win32/win_fs.h
 *
 * Filesystem operations that don't have direct POSIX equivalents on
 * Windows: symlink/hardlink creation, mode-bit mapping, file times.
 * Used from syscall.c (upstream) under WIN32_NATIVE gates.
 */
#ifndef WIN_FS_H
#define WIN_FS_H

#include <sys/types.h>
#include <sys/stat.h>

/* Map a POSIX mode (only user-write bit consulted) to the Windows
 * FILE_ATTRIBUTE_READONLY flag. Other mode bits are ignored. */
int win_chmod(const char *path, mode_t mode);

/* Create a symbolic link via CreateSymbolicLinkW with the
 * SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE flag. On permission
 * failure: if RSYNC_STRICT_SYMLINKS env var is set, returns -1; else
 * falls back to copying target's contents (with a warning) and returns 0. */
int win_symlink(const char *target, const char *linkpath);

/* Read a symbolic link via DeviceIoControl(FSCTL_GET_REPARSE_POINT).
 * Behaves like POSIX readlink(). */
ssize_t win_readlink(const char *path, char *buf, size_t bufsize);

/* Create a hard link via CreateHardLinkW. */
int win_link(const char *target, const char *linkpath);

/* Set file access/modification times. atimes and mtimes only; ctime is
 * not settable on NTFS without raw API. */
struct timespec;
int win_utimens(const char *path, const struct timespec times[2]);

#endif /* WIN_FS_H */
