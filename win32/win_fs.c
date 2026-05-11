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

/* Resolve `target` (the string the symlink points to) into an absolute
 * filesystem path we can pass to CopyFileA. If target is relative,
 * it's resolved against the directory containing linkpath — matching
 * POSIX symlink semantics. Returns 0 on success, -1 on overflow. */
static int resolve_link_target(const char *target, const char *linkpath,
                               char *out, size_t out_size)
{
    int target_abs = (target[0] == '\\' || target[0] == '/'
                      || (target[1] == ':' && (target[2] == '\\' || target[2] == '/')));
    if (target_abs) {
        if ((size_t)snprintf(out, out_size, "%s", target) >= out_size)
            return -1;
        return 0;
    }

    /* Relative: drop linkpath's last component, append target. */
    size_t len = strlen(linkpath);
    if (len >= out_size) return -1;
    memcpy(out, linkpath, len + 1);
    char *slash = strrchr(out, '\\');
    char *fslash = strrchr(out, '/');
    if (fslash > slash) slash = fslash;
    if (slash) {
        slash[1] = '\0';
    } else {
        out[0] = '\0';
    }
    size_t pos = strlen(out);
    if ((size_t)snprintf(out + pos, out_size - pos, "%s", target) >= out_size - pos)
        return -1;
    return 0;
}

int win_symlink(const char *target, const char *linkpath)
{
    static int permission_warning_emitted = 0;

    if (!target || !linkpath) {
        errno = EINVAL;
        return -1;
    }
    /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE = 0x2 — needs Win10
     * 1703+ in Developer Mode (or admin). If the OS lacks support for
     * the flag, CreateSymbolicLinkA returns 0 and GetLastError gives
     * ERROR_INVALID_PARAMETER on older builds.
     *
     * TODO(win-port): detect target type via GetFileAttributesA(target)
     * and set SYMBOLIC_LINK_FLAG_DIRECTORY (0x1) for directory targets.
     */
    DWORD flags = 0x2;   /* unprivileged-create */
    if (CreateSymbolicLinkA(linkpath, target, flags))
        return 0;

    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        errno = EEXIST;
        return -1;
    }

    /* Permission failure path. Default: warn once, fall back to copying
     * the target's contents into linkpath (so the file at least
     * transfers). Override with RSYNC_STRICT_SYMLINKS=1. */
    if (err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_ACCESS_DENIED) {
        const char *strict = getenv("RSYNC_STRICT_SYMLINKS");
        if (strict && *strict && *strict != '0') {
            errno = EACCES;
            return -1;
        }
        if (!permission_warning_emitted) {
            permission_warning_emitted = 1;
            rprintf(FWARNING,
                "rsync: cannot create symlinks without Developer Mode or admin.\n"
                "       Falling back to copying target content for the symlinks\n"
                "       we encounter. Set RSYNC_STRICT_SYMLINKS=1 to make this\n"
                "       failure fatal instead.\n");
        }

        char resolved[MAX_PATH * 4];
        if (resolve_link_target(target, linkpath, resolved, sizeof(resolved)) < 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
        /* CopyFileA on a directory fails; that's the right behavior for
         * us (we can't fold a directory into a file). For dangling
         * symlinks, CopyFileA returns ERROR_FILE_NOT_FOUND. */
        if (CopyFileA(resolved, linkpath, FALSE))
            return 0;

        DWORD copy_err = GetLastError();
        if (copy_err == ERROR_FILE_NOT_FOUND) errno = ENOENT;
        else if (copy_err == ERROR_ACCESS_DENIED) errno = EACCES;
        else errno = EIO;
        return -1;
    }

    /* Some other CreateSymbolicLink error. */
    errno = EIO;
    return -1;
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

/* Directory iteration. rsync uses opendir/readdir/closedir for recursive
 * traversal. We back it with FindFirstFile/FindNextFile. The directory
 * pattern is "<path>\\*" (FindFirst returns "." then ".." then entries). */
DIR *win_opendir(const char *path)
{
    if (!path) { errno = EINVAL; return NULL; }
    size_t len = strlen(path);
    /* Pattern buffer: path + "\\*" + NUL */
    char pattern[MAX_PATH * 4];
    if (len + 3 > sizeof(pattern)) { errno = ENAMETOOLONG; return NULL; }
    memcpy(pattern, path, len);
    /* Ensure trailing separator */
    char last = len ? path[len - 1] : '\0';
    size_t pos = len;
    if (last != '\\' && last != '/' && last != ':' && len != 0)
        pattern[pos++] = '\\';
    pattern[pos++] = '*';
    pattern[pos]   = '\0';

    DIR *d = (DIR *)calloc(1, sizeof(*d));
    if (!d) { errno = ENOMEM; return NULL; }
    d->handle = FindFirstFileA(pattern, &d->pending);
    if (d->handle == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        free(d);
        errno = (e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND) ? ENOENT : EIO;
        return NULL;
    }
    d->have_pending = 1;
    return d;
}

struct direct *win_readdir(DIR *d)
{
    if (!d) { errno = EINVAL; return NULL; }
    if (!d->have_pending) {
        if (!FindNextFileA(d->handle, &d->pending)) {
            /* End of dir or error — POSIX readdir returns NULL with errno
             * unchanged on end-of-dir. */
            return NULL;
        }
    }
    d->have_pending = 0;
    size_t n = strnlen(d->pending.cFileName, sizeof(d->entry.d_name) - 1);
    memcpy(d->entry.d_name, d->pending.cFileName, n);
    d->entry.d_name[n] = '\0';
    return &d->entry;
}

int win_closedir(DIR *d)
{
    if (!d) { errno = EINVAL; return -1; }
    if (d->handle != INVALID_HANDLE_VALUE)
        FindClose(d->handle);
    free(d);
    return 0;
}

/* gettimeofday — convert FILETIME (100ns ticks since 1601) into
 * struct timeval (s + us since 1970). */
int win_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    (void)tz;
    if (!tv) { errno = EINVAL; return -1; }
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    ticks -= 116444736000000000ULL;        /* 1601 -> 1970 (in 100ns) */
    tv->tv_sec  = (long)(ticks / 10000000ULL);
    tv->tv_usec = (long)((ticks % 10000000ULL) / 10);
    return 0;
}

#endif /* WIN32_NATIVE */
