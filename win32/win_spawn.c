/* win32/win_spawn.c
 *
 * Replaces pipe.c::piped_child() on Windows. Uses gnulib's spawn-pipe to
 * launch the remote-shell process (ssh.exe) with bidirectional pipes.
 *
 * Upstream piped_child() forks then execvp's; gnulib's create_pipe_bidi
 * is fork+exec on POSIX and CreateProcess+pipes on Windows. We always
 * call through it; the Linux build of rsync still uses the original
 * fork path because this file is only compiled under WIN32_NATIVE.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_spawn.h"
#include <stdbool.h>
#include <errno.h>
#include "gl/spawn-pipe.h"

pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out)
{
    int fd[2];
    pid_t pid;

    /* gnulib's create_pipe_bidi parameters:
     *   progname        - used in error messages (we pass argv[0])
     *   prog_path       - the executable to spawn; NULL means search PATH
     *   prog_argv       - NULL-terminated argv
     *   dll_dirs        - extra DLL search dirs on Windows (none)
     *   directory       - cwd for child (NULL = inherit parent's)
     *   null_stderr     - redirect child stderr to NUL? (no — keep it
     *                     attached to parent's so ssh diagnostics reach
     *                     the user)
     *   slave_process   - kill child if parent dies (yes)
     *   exit_on_error   - call exit() on failure (no — return -1)
     *   fd              - [0]=fd to read child stdout, [1]=fd to write
     *                     child stdin
     */
    pid = create_pipe_bidi(
        argv[0],
        argv[0],
        (const char * const *)argv,
        NULL,
        NULL,
        false,
        true,
        false,
        fd);

    if (pid == (pid_t)-1) {
        if (errno == 0)
            errno = EAGAIN;
        return -1;
    }

    *f_in  = fd[0];
    *f_out = fd[1];
    return pid;
}

#endif /* WIN32_NATIVE */
