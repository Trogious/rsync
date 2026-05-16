/* win32/win_sock_io.c
 *
 * read / write / close shims that route Winsock SOCKETs to recv/send/
 * closesocket while passing through regular CRT fds to _read/_write/
 * _close. rsync's io.c assumes POSIX semantics where socket() fds are
 * interchangeable with pipe/file fds; on Windows that's not true.
 *
 * Detection: we can't safely call _get_osfhandle() on an unknown fd --
 * MSVC's CRT validates fd indices and aborts the process via the
 * invalid-parameter handler (exit code 127, no error output) for any
 * fd not in the CRT table. So instead of detecting at I/O time we
 * register sockets explicitly at the open site (socket.c) and look
 * them up via win_fd_register_socket / win_fd_is_socket.
 *
 * The registry is a small per-process bitmap; rsync never juggles more
 * than a handful of sockets so a 1024-entry static set is more than
 * enough. Mutex-protected; access is rare.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE

#include <errno.h>

#define WIN_SOCK_MAX  1024   /* highest fd value we'll track */

static unsigned char win_sock_map[WIN_SOCK_MAX];
static CRITICAL_SECTION win_sock_lock;
static int              win_sock_lock_inited = 0;

static void win_sock_lock_init(void)
{
    /* Cheap double-checked init. Worst case the InitializeCriticalSection
     * runs twice during a startup race -- no harm since it just zeroes
     * the same struct. */
    if (!win_sock_lock_inited) {
        InitializeCriticalSection(&win_sock_lock);
        win_sock_lock_inited = 1;
    }
}

void win_fd_register_socket(int fd)
{
    if (fd < 0 || fd >= WIN_SOCK_MAX) return;
    win_sock_lock_init();
    EnterCriticalSection(&win_sock_lock);
    win_sock_map[fd] = 1;
    LeaveCriticalSection(&win_sock_lock);
}

void win_fd_unregister_socket(int fd)
{
    if (fd < 0 || fd >= WIN_SOCK_MAX) return;
    win_sock_lock_init();
    EnterCriticalSection(&win_sock_lock);
    win_sock_map[fd] = 0;
    LeaveCriticalSection(&win_sock_lock);
}

int win_fd_is_socket_q(int fd)
{
    /* Lock-free read: the map is byte-granular and 0/1, so torn reads
     * resolve to one of those two values. Worst case we miss a brand-new
     * socket on the millisecond before the register call returns, which
     * isn't a real race in practice (open is sequential w.r.t. the
     * subsequent I/O on the same fd). */
    if (fd < 0 || fd >= WIN_SOCK_MAX) return 0;
    return win_sock_map[fd] != 0;
}

int win_read(int fd, void *buf, unsigned int count)
{
    if (win_fd_is_socket_q(fd)) {
        int r = recv((SOCKET)(intptr_t)fd, (char *)buf, (int)count, 0);
        if (r == SOCKET_ERROR) {
            int werr = WSAGetLastError();
            errno = (werr == WSAEWOULDBLOCK) ? EAGAIN
                  : (werr == WSAEINTR)       ? EINTR
                  : (werr == WSAECONNRESET)  ? ECONNRESET
                  : EIO;
            return -1;
        }
        return r;
    }
    return _read(fd, buf, count);
}

int win_write(int fd, const void *buf, unsigned int count)
{
    if (win_fd_is_socket_q(fd)) {
        int r = send((SOCKET)(intptr_t)fd, (const char *)buf, (int)count, 0);
        if (r == SOCKET_ERROR) {
            int werr = WSAGetLastError();
            errno = (werr == WSAEWOULDBLOCK) ? EAGAIN
                  : (werr == WSAEINTR)       ? EINTR
                  : (werr == WSAECONNRESET)  ? ECONNRESET
                  : EIO;
            return -1;
        }
        return r;
    }
    return _write(fd, buf, count);
}

int win_close(int fd)
{
    if (win_fd_is_socket_q(fd)) {
        win_fd_unregister_socket(fd);
        if (closesocket((SOCKET)(intptr_t)fd) == SOCKET_ERROR) {
            errno = EIO;
            return -1;
        }
        return 0;
    }
    return _close(fd);
}

#endif /* WIN32_NATIVE */
