/* win32/win_fs.c
 *
 * Filesystem operations whose POSIX semantics don't have a direct
 * Windows equivalent. Wired up from syscall.c via #ifdef WIN32_NATIVE
 * gates.
 *
 * We rely on the UTF-8 active code page set in win32/rsync.manifest
 * so the *A APIs accept UTF-8 paths.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_fs.h"
#include "win32/win_paths.h"
#include <errno.h>
#include <time.h>

/* chmod: map user-write bit to !FILE_ATTRIBUTE_READONLY. Other mode
 * bits don't map cleanly so we ignore them. */
int win_chmod(const char *path, mode_t mode)
{
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return -1;
    }
    if (mode & 0200)
        attrs &= ~FILE_ATTRIBUTE_READONLY;
    else
        attrs |= FILE_ATTRIBUTE_READONLY;
    if (!SetFileAttributesA(path, attrs)) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int win_symlink(const char *target, const char *linkpath)
{
    if (!target || !linkpath) {
        errno = EINVAL;
        return -1;
    }
    /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE = 0x2 — needs Win10
     * 1703+ in Developer Mode (or admin). If the OS lacks support for
     * the flag, CreateSymbolicLinkA returns 0 and GetLastError gives
     * ERROR_INVALID_PARAMETER on older builds.
     *
     * SYMBOLIC_LINK_FLAG_DIRECTORY = 0x1 — set if target is a directory
     * (we don't actually stat it; default to file. rsync's -a flag tells
     * us via separate calls for dirs.)
     *
     * TODO(win-port): detect target type via GetFileAttributesA(target)
     * and set the DIRECTORY flag accordingly.
     */
    DWORD flags = 0x2;   /* unprivileged-create */
    if (!CreateSymbolicLinkA(linkpath, target, flags)) {
        DWORD err = GetLastError();
        if (err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_ACCESS_DENIED)
            errno = EACCES;
        else if (err == ERROR_ALREADY_EXISTS)
            errno = EEXIST;
        else
            errno = EIO;
        return -1;
    }
    return 0;
}

ssize_t win_readlink(const char *path, char *buf, size_t bufsize)
{
    /* Use GetFinalPathNameByHandleA: open the symlink with
     * FILE_FLAG_OPEN_REPARSE_POINT so we get the link itself, then
     * resolve its target. */
    HANDLE h = CreateFileA(path,
                           0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }
    /* VOLUME_NAME_DOS gives us a readable "C:\..." style; the result is
     * NUL-terminated; the returned length excludes the NUL. */
    DWORD n = GetFinalPathNameByHandleA(h, buf, (DWORD)bufsize, 0);
    CloseHandle(h);
    if (n == 0 || n >= bufsize) {
        errno = ENAMETOOLONG;
        return -1;
    }
    /* Strip the "\\?\" prefix if present — rsync's wire format doesn't
     * use it. */
    if (n >= 4 && memcmp(buf, "\\\\?\\", 4) == 0) {
        memmove(buf, buf + 4, n - 4 + 1);
        n -= 4;
    }
    return (ssize_t)n;
}

int win_link(const char *target, const char *linkpath)
{
    if (!target || !linkpath) {
        errno = EINVAL;
        return -1;
    }
    if (!CreateHardLinkA(linkpath, target, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            errno = EEXIST;
        else if (err == ERROR_ACCESS_DENIED)
            errno = EACCES;
        else if (err == ERROR_NOT_SAME_DEVICE)
            errno = EXDEV;
        else
            errno = EIO;
        return -1;
    }
    return 0;
}

/* Convert a POSIX timespec (seconds + nanoseconds since 1970-01-01) to
 * a Windows FILETIME (100ns ticks since 1601-01-01). */
static void timespec_to_filetime(const struct timespec *ts, FILETIME *ft)
{
    /* Epoch difference: 11644473600 seconds. */
    uint64_t ticks = ((uint64_t)ts->tv_sec + 11644473600ULL) * 10000000ULL
                   + (uint64_t)ts->tv_nsec / 100ULL;
    ft->dwLowDateTime  = (DWORD)(ticks & 0xFFFFFFFFULL);
    ft->dwHighDateTime = (DWORD)(ticks >> 32);
}

int win_utimens(const char *path, const struct timespec times[2])
{
    HANDLE h = CreateFileA(path,
                           FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }

    FILETIME atime, mtime;
    timespec_to_filetime(&times[0], &atime);
    timespec_to_filetime(&times[1], &mtime);
    BOOL ok = SetFileTime(h, NULL /* ctime — read-only in NTFS via this API */,
                          &atime, &mtime);
    CloseHandle(h);
    if (!ok) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

#endif /* WIN32_NATIVE */
