/* win32/win_compat.h
 *
 * Umbrella header for the native Windows build. Included from rsync.h
 * when WIN32_NATIVE is defined. Provides:
 *   - Windows API includes (with WIN32_LEAN_AND_MEAN)
 *   - POSIX type/macro stubs not provided by MSVC's CRT
 *   - Forward declarations from win32/win_*.h
 *
 * Most actual POSIX behavior comes from the gnulib polyfills in gl/.
 * This header only fills the gaps gnulib doesn't cover.
 */
#ifndef WIN32_COMPAT_H
#define WIN32_COMPAT_H

#if defined(WIN32_NATIVE)

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>

/* MSVC's CRT does not define pid_t, mode_t, or ssize_t. gnulib's
 * generated sys_types.h does, but we typedef here too in case this
 * header is consumed before gnulib's generated headers are on the
 * include path. */
#ifndef _PID_T_DEFINED
typedef int pid_t;
# define _PID_T_DEFINED
#endif
#ifndef _MODE_T_DEFINED
typedef unsigned short mode_t;
# define _MODE_T_DEFINED
#endif
#ifndef _SSIZE_T_DEFINED
# include <basetsd.h>
typedef SSIZE_T ssize_t;
# define _SSIZE_T_DEFINED
#endif

/* Windows has no POSIX uid/gid concept. Treat everyone as uid 0. */
typedef int uid_t;
typedef int gid_t;
#define geteuid()   (0)
#define getegid()   (0)
#define getuid()    (0)
#define getgid()    (0)
#define setuid(u)   (errno = ENOSYS, -1)
#define setgid(g)   (errno = ENOSYS, -1)

/* chown / lchown — no-op stubs (return success so --archive doesn't error). */
#define chown(p, u, g)  (0)
#define lchown(p, u, g) (0)

/* File mode bits absent from MSVC's sys/stat.h. */
#ifndef S_IFLNK
# define S_IFLNK 0120000
#endif
#ifndef S_ISLNK
# define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef S_ISSOCK
# define S_ISSOCK(m) (0)
#endif
#ifndef S_ISFIFO
# define S_ISFIFO(m) (((m) & S_IFMT) == _S_IFIFO)
#endif

/* fork() and waitpid() are redirected to our NT-based implementations.
 * See win32/win_fork.c. */
#define fork()                       win_fork()
#define waitpid(p, s, o)             win_waitpid((p), (s), (o))

/* Forward declarations from our win32/ replacements. */
#include "win32/win_spawn.h"
#include "win32/win_fork.h"
#include "win32/win_paths.h"
#include "win32/win_fs.h"
#include "win32/win_ssh.h"

#endif /* WIN32_NATIVE */
#endif /* WIN32_COMPAT_H */
