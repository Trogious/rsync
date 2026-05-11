/* win32/win_child_init.c — Phase 2 stub. Replaced by Phase 3. */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_child_init.h"

int win_child_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    return -1;  /* not a re-exec'd child */
}
#endif
