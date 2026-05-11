/* win32/win_ssh.c — Phase 2 stub. Replaced by Phase 5. */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_ssh.h"

const char *win_default_rsh(void)
{
    return "ssh.exe";  /* implemented in Phase 5 */
}
#endif
