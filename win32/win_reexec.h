/* win32/win_reexec.h
 *
 * Replaces fork()+in-process-dispatch on Windows by re-executing rsync.exe
 * with a marker flag that routes the child to a specific role.
 *
 * Upstream code paths:
 *   - pipe.c::local_child()         (WIN_ROLE_LOCAL_CHILD)
 *   - main.c::do_recv() child       (WIN_ROLE_RECEIVER)
 *   - main.c::do_recv() generator   (WIN_ROLE_GENERATOR, if needed)
 */
#ifndef WIN_REEXEC_H
#define WIN_REEXEC_H

#include <sys/types.h>

typedef enum {
    WIN_ROLE_LOCAL_CHILD = 1,
    WIN_ROLE_RECEIVER    = 2,
    WIN_ROLE_GENERATOR   = 3
} win_role_t;

/* Re-exec rsync.exe in the given role, inheriting fds via marker file.
 *
 *   role   : which entry point the child should dispatch to.
 *   argc   : count for child_argv.
 *   argv   : argv the child should see (will be serialized).
 *   f_in   : on success, set to fd reading the child's output.
 *   f_out  : on success, set to fd writing to the child.
 *
 * Returns child PID; -1 with errno set on failure.
 */
pid_t win_reexec_self_as(win_role_t role,
                         int argc, char **argv,
                         int *f_in, int *f_out);

#endif /* WIN_REEXEC_H */
