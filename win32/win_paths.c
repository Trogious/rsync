/* win32/win_paths.c — Phase 2 stub. Replaced by Phase 4. */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_paths.h"

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

char *win_long_path_prefix(const char *path)
{
    (void)path;
    return NULL;  /* implemented in Phase 4 */
}
#endif
