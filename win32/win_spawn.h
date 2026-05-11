/* win32/win_spawn.h
 *
 * Replaces fork()+execvp(ssh) for the remote-shell transport on Windows.
 * Upstream code path: pipe.c::piped_child().
 */
#ifndef WIN_SPAWN_H
#define WIN_SPAWN_H

#include <sys/types.h>

/* Spawn a remote-shell process (typically ssh.exe) with bidirectional
 * pipes connecting parent <-> child.
 *
 *   argv   : NULL-terminated argv; argv[0] is the program to exec.
 *   f_in   : on success, set to fd reading the child's stdout.
 *   f_out  : on success, set to fd writing to the child's stdin.
 *
 * Returns the child PID on success; -1 with errno set on failure.
 */
pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out);

#endif /* WIN_SPAWN_H */
