/* win32/win_paths.c
 *
 * Path classification and normalization for the native Windows build.
 * The 'is drive' / 'is UNC' checks are used in options.c to guard
 * against cwRsync's classic colon-parsing bug; the others support
 * filesystem syscalls in syscall.c.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_paths.h"
#include <stdlib.h>
#include <string.h>

int win_is_drive_path(const char *path)
{
    if (!path) return 0;
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':';
}

int win_is_unc_path(const char *path)
{
    return path && path[0] == '\\' && path[1] == '\\';
}

char *win_normalize_path(char *path)
{
    if (path) {
        for (char *p = path; *p; p++)
            if (*p == '\\') *p = '/';
    }
    return path;
}

/* Add the "\\?\" long-path prefix if path is at risk of hitting the
 * MAX_PATH (260) limit. The prefix bypasses the limit for filesystem
 * APIs that support it (most do). For UNC paths, the prefix form is
 * "\\?\UNC\server\share\..." instead of "\\?\\\server\share\...".
 *
 * If path is short enough OR already prefixed, returns NULL — caller
 * should use the original path as-is. Otherwise returns a malloc'd
 * string the caller must free().
 */
char *win_long_path_prefix(const char *path)
{
    if (!path) return NULL;

    size_t len = strlen(path);
    if (len < (MAX_PATH - 12))   /* leave headroom for 8.3 short-name expansion */
        return NULL;
    if (len >= 4 && memcmp(path, "\\\\?\\", 4) == 0)
        return NULL;             /* already prefixed */

    /* Resolve to absolute first — \\?\ requires an absolute path. */
    char abs_buf[32768];
    DWORD abs_len = GetFullPathNameA(path, sizeof(abs_buf), abs_buf, NULL);
    if (abs_len == 0 || abs_len >= sizeof(abs_buf))
        return NULL;

    const char *prefix;
    const char *body;
    if (abs_buf[0] == '\\' && abs_buf[1] == '\\') {
        /* UNC: "\\server\share\..." -> "\\?\UNC\server\share\..." */
        prefix = "\\\\?\\UNC\\";
        body   = abs_buf + 2;
    } else {
        /* Drive or local: "C:\..." -> "\\?\C:\..." */
        prefix = "\\\\?\\";
        body   = abs_buf;
    }

    size_t total = strlen(prefix) + strlen(body) + 1;
    char *out = (char *)malloc(total);
    if (!out) return NULL;
    snprintf(out, total, "%s%s", prefix, body);
    return out;
}

#endif /* WIN32_NATIVE */
