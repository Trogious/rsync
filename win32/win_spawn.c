/* win32/win_spawn.c — Phase 2 stub. Replaced by Phase 3 (gnulib spawn-pipe). */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_spawn.h"
#include <errno.h>

pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out)
{
    (void)argv; (void)f_in; (void)f_out;
    errno = ENOSYS;
    return (pid_t)-1;
}
#endif
