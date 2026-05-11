/* win32/win_reexec.c — Phase 2 stub. Replaced by Phase 3. */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_reexec.h"
#include <errno.h>

pid_t win_reexec_self_as(win_role_t role,
                         int argc, char **argv,
                         int *f_in, int *f_out)
{
    (void)role; (void)argc; (void)argv; (void)f_in; (void)f_out;
    errno = ENOSYS;
    return (pid_t)-1;
}
#endif
