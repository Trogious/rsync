/* win32/win_fork.h
 *
 * Native Windows fork() implementation using RtlCloneUserProcess (NT
 * internal API, used by Cygwin and mitchcapper's tar port).
 *
 * Unlike the re-exec approach, the cloned child shares the parent's
 * full memory image, file descriptor table, and process state.
 * No serialization needed — upstream rsync's fork-based code paths
 * (do_recv generator/receiver split, local_child) "just work".
 *
 * Also provides win_waitpid() because the MSVC CRT's _cwait only
 * knows about processes created via _spawn family; cloned children
 * aren't in that table.
 */
#ifndef WIN_FORK_H
#define WIN_FORK_H

#include <sys/types.h>

/* Clone this process. Returns 0 in the child, child PID in the
 * parent, (pid_t)-1 with errno set on error. Semantics match POSIX
 * fork() as closely as the Windows process model allows:
 *   - Child has identical virtual memory at the point of clone.
 *   - Child inherits the file descriptor table.
 *   - Child has a single thread (the one that called win_fork).
 *   - Signal-like state (CRT signal handlers) is replicated.
 *
 * Limitations:
 *   - Not all NT-loader state replicates cleanly across the clone
 *     boundary; in practice this affects loader locks and TLS
 *     destructors. rsync hits neither.
 *   - Some EDR products block RtlCloneUserProcess. If win_fork
 *     returns -1 with errno=ENOSYS, fall back to a different
 *     strategy (currently no fallback).
 */
pid_t win_fork(void);

/* Wait for a process previously returned by win_fork. The 'options'
 * argument supports WNOHANG; other flags are accepted but treated
 * as 0. Encodes exit code into *statusp using POSIX layout
 * (W_EXITCODE: low byte signal, next byte exit code). */
pid_t win_waitpid(pid_t pid, int *statusp, int options);

#endif /* WIN_FORK_H */
