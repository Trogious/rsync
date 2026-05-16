/* win32/win_select.c
 *
 * select() shim that understands BOTH winsock SOCKETs and pipe HANDLEs
 * (anonymous pipes from piped_child / win_spawn_remote_shell).
 *
 * Why: rsync's io.c calls select(in_fd, out_fd, ...) heavily where in_fd
 * and out_fd come from piped_child(), which on Windows produces CRT fds
 * wrapping anonymous-pipe HANDLEs via _open_osfhandle. winsock select()
 * only handles SOCKETs, so it fails on pipe fds (with WSAENOTSOCK or
 * just zero readiness depending on the build) and rsync's dispatcher
 * hangs forever.
 *
 * Strategy:
 *   - Walk each fd_set; classify each fd via GetFileType(_get_osfhandle).
 *   - Partition into sockets and pipes.
 *   - Sockets: defer to winsock's real select() (passed-through fd_set).
 *   - Pipes: PeekNamedPipe(handle) tells us how many bytes are queued
 *     (i.e. readable) without blocking. There's no kernel "writable"
 *     notification for anonymous pipes, so we treat them as always
 *     writable — the upstream writer will block at write() time if the
 *     pipe is full, which is rsync's existing failure mode on Linux too.
 *   - If anything is immediately ready, return it. Otherwise poll on a
 *     10 ms interval until the timeout expires, re-checking each round.
 *
 * Caveats:
 *   - exceptfds for pipes always returns empty; we never report "error"
 *     status. rsync only uses it as a sanity check.
 *   - Mixed socket+pipe wait blocks the whole call on the poll cadence
 *     even though sockets could wake faster; acceptable until we hit
 *     a workload that proves it matters.
 *   - FD_SETSIZE (64 by default on winsock) is the per-set cap. rsync
 *     never sets more than a handful, so it's fine.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include <errno.h>

/* Avoid <io.h> (rsync has a local io.h that wins the -I. lookup); the
 * declarations we need are already in win32/win_compat.h. */

/* win_compat.h installs `#define select win_select` so every caller is
 * redirected here. Inside this file we need the *real* winsock select()
 * to dispatch the socket subset; undef the macro to expose it. */
#undef select

static int classify_fd(int fd, HANDLE *out_handle)
{
    /* Returns: 0 = invalid, 1 = socket, 2 = pipe, 3 = other (regular file/console).
     *
     * Check the explicit socket registry FIRST: rsync's rsync:// client
     * path passes raw SOCKET handles as fds. _get_osfhandle on a non-CRT
     * fd doesn't just return -1, it triggers MSVC's invalid-parameter
     * handler which aborts the whole process. The registry (populated
     * by win_fd_register_socket at socket() time) lets us recognise
     * sockets without ever asking the CRT.
     *
     * For real CRT fds we still use _get_osfhandle / GetFileType /
     * GetNamedPipeInfo to distinguish pipes vs files vs CRT-wrapped
     * sockets, exactly as before.
     */
    if (win_fd_is_socket_q(fd)) {
        *out_handle = (HANDLE)(intptr_t)fd;
        return 1;
    }
    intptr_t h = _get_osfhandle(fd);
    if (h == -1 || h == -2) {
        *out_handle = (HANDLE)(intptr_t)fd;
        return 1;
    }
    *out_handle = (HANDLE)h;
    DWORD ft = GetFileType((HANDLE)h);
    if (ft == FILE_TYPE_PIPE) {
        DWORD flags = 0;
        if (GetNamedPipeInfo((HANDLE)h, &flags, NULL, NULL, NULL))
            return 2;
        return 1;
    }
    return 3;
}

/* Check pipe readability without blocking. Returns 1 if data is
 * available or the pipe is closed (so read() will return 0 immediately
 * — which is also "ready" by select() semantics). */
static int pipe_readable(HANDLE h)
{
    DWORD avail = 0;
    if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
        return avail > 0;
    /* ERROR_BROKEN_PIPE means the peer closed; reads will return 0. */
    return (GetLastError() == ERROR_BROKEN_PIPE) ? 1 : 0;
}

int win_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
               struct timeval *timeout)
{
    /* nfds is unused on Windows — winsock's fd_set is an array, not a
     * bitmask. We iterate the fd_array directly. */
    (void)nfds;

    /* Snapshot the input sets so we can rebuild on each polling pass. */
    fd_set in_r, in_w, in_e;
    int has_r = readfds != NULL, has_w = writefds != NULL, has_e = exceptfds != NULL;
    if (has_r) in_r = *readfds; else FD_ZERO(&in_r);
    if (has_w) in_w = *writefds; else FD_ZERO(&in_w);
    if (has_e) in_e = *exceptfds; else FD_ZERO(&in_e);

    /* Compute deadline in milliseconds from now. INFINITE if timeout==NULL. */
    DWORD timeout_ms = INFINITE;
    if (timeout) {
        long ms = timeout->tv_sec * 1000L + timeout->tv_usec / 1000L;
        timeout_ms = (ms < 0) ? 0 : (DWORD)ms;
    }
    DWORD start = GetTickCount();
    const DWORD poll_interval = 10;

    for (;;) {
        fd_set out_r, out_w, out_e;
        FD_ZERO(&out_r); FD_ZERO(&out_w); FD_ZERO(&out_e);
        int ready = 0;

        /* Socket subsets — feed to real winsock select() with a 0-timeout
         * non-blocking poll. */
        fd_set sock_r, sock_w, sock_e;
        FD_ZERO(&sock_r); FD_ZERO(&sock_w); FD_ZERO(&sock_e);
        int have_sockets = 0;
        int sock_max = 0;
#define COLLECT_SOCKETS(in, out)                                         \
        for (u_int i = 0; i < (in).fd_count; i++) {                      \
            int fd = (int)(in).fd_array[i];                              \
            HANDLE h;                                                    \
            int kind = classify_fd(fd, &h);                              \
            if (kind == 1) {                                             \
                FD_SET((SOCKET)fd, &(out));                              \
                have_sockets = 1;                                        \
                if (fd > sock_max) sock_max = fd;                        \
            }                                                            \
        }
        if (has_r) COLLECT_SOCKETS(in_r, sock_r);
        if (has_w) COLLECT_SOCKETS(in_w, sock_w);
        if (has_e) COLLECT_SOCKETS(in_e, sock_e);
#undef COLLECT_SOCKETS
        if (have_sockets) {
            struct timeval zero_tv = { 0, 0 };
            int sr = select(sock_max + 1,
                            sock_r.fd_count ? &sock_r : NULL,
                            sock_w.fd_count ? &sock_w : NULL,
                            sock_e.fd_count ? &sock_e : NULL,
                            &zero_tv);
            if (sr > 0) {
                for (u_int i = 0; i < sock_r.fd_count; i++) {
                    FD_SET((SOCKET)sock_r.fd_array[i], &out_r); ready++;
                }
                for (u_int i = 0; i < sock_w.fd_count; i++) {
                    FD_SET((SOCKET)sock_w.fd_array[i], &out_w); ready++;
                }
                for (u_int i = 0; i < sock_e.fd_count; i++) {
                    FD_SET((SOCKET)sock_e.fd_array[i], &out_e); ready++;
                }
            }
        }

        /* Pipe subsets — PeekNamedPipe for read-readiness; pipes are
         * always considered writable. */
        if (has_r) {
            for (u_int i = 0; i < in_r.fd_count; i++) {
                int fd = (int)in_r.fd_array[i];
                HANDLE h;
                if (classify_fd(fd, &h) == 2 && pipe_readable(h)) {
                    FD_SET((SOCKET)fd, &out_r);
                    ready++;
                }
            }
        }
        if (has_w) {
            for (u_int i = 0; i < in_w.fd_count; i++) {
                int fd = (int)in_w.fd_array[i];
                HANDLE h;
                if (classify_fd(fd, &h) == 2) {
                    /* Anonymous pipes have no "writable" event; assume
                     * yes (rsync's writer will block at write() if the
                     * pipe is genuinely full). */
                    FD_SET((SOCKET)fd, &out_w);
                    ready++;
                }
            }
        }
        /* exceptfds for pipes: never set. */

        if (ready > 0) {
            if (has_r) *readfds   = out_r;
            if (has_w) *writefds  = out_w;
            if (has_e) *exceptfds = out_e;
            return ready;
        }

        /* Nothing ready. Honor the timeout. */
        if (timeout_ms == 0) {
            if (has_r) FD_ZERO(readfds);
            if (has_w) FD_ZERO(writefds);
            if (has_e) FD_ZERO(exceptfds);
            return 0;
        }
        if (timeout_ms != INFINITE) {
            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= timeout_ms) {
                if (has_r) FD_ZERO(readfds);
                if (has_w) FD_ZERO(writefds);
                if (has_e) FD_ZERO(exceptfds);
                return 0;
            }
            DWORD remaining = timeout_ms - elapsed;
            Sleep(remaining < poll_interval ? remaining : poll_interval);
        } else {
            Sleep(poll_interval);
        }
    }
}

#endif /* WIN32_NATIVE */
