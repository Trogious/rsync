/* win32/win_fs.c — Phase 2 stub. Replaced by Phase 4. */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_fs.h"
#include <errno.h>

int win_chmod(const char *path, mode_t mode)
{
    (void)path; (void)mode;
    return 0;  /* permissive: ignore mode bits for now */
}

int win_symlink(const char *target, const char *linkpath)
{
    (void)target; (void)linkpath;
    errno = ENOSYS;
    return -1;
}

ssize_t win_readlink(const char *path, char *buf, size_t bufsize)
{
    (void)path; (void)buf; (void)bufsize;
    errno = ENOSYS;
    return -1;
}

int win_link(const char *target, const char *linkpath)
{
    (void)target; (void)linkpath;
    errno = ENOSYS;
    return -1;
}

int win_utimens(const char *path, const struct timespec times[2])
{
    (void)path; (void)times;
    errno = ENOSYS;
    return -1;
}
#endif
