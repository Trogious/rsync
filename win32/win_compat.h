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
/* MSVC's <io.h> would collide with rsync's local io.h under -I., so we
 * forward-declare the handful of functions we need from it instead. */
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>            /* localtime_s, the localtime_r shim wants it */
#include <sys/types.h>
#include <sys/stat.h>

/* From MSVC's <io.h> — re-declared because -I. lookup grabs the local
 * io.h. These prototypes match the CRT declarations exactly. */
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) intptr_t __cdecl _open_osfhandle(intptr_t osfhandle, int flags);
__declspec(dllimport) intptr_t __cdecl _get_osfhandle(int fd);
__declspec(dllimport) int      __cdecl _dup2(int fd1, int fd2);
__declspec(dllimport) int      __cdecl _close(int fd);
__declspec(dllimport) int      __cdecl _read(int fd, void *buf, unsigned cnt);
__declspec(dllimport) int      __cdecl _write(int fd, const void *buf, unsigned cnt);
#ifdef __cplusplus
}
#endif

/* MSVC's CRT lacks ssize_t. pid_t and mode_t may or may not be present
 * depending on the SDK version; use custom guards so we don't fight any
 * CRT-side typedef and never get skipped by a pre-existing CRT macro
 * that happens to share a name like _SSIZE_T_DEFINED. */
#include <basetsd.h>
#ifndef RSYNC_HAVE_SSIZE_T
typedef SSIZE_T ssize_t;
# define RSYNC_HAVE_SSIZE_T 1
#endif
#ifndef RSYNC_HAVE_PID_T
typedef int pid_t;
# define RSYNC_HAVE_PID_T 1
#endif
#ifndef RSYNC_HAVE_MODE_T
typedef unsigned short mode_t;
# define RSYNC_HAVE_MODE_T 1
#endif

/* Windows has no POSIX uid/gid concept. Treat everyone as uid 0. */
typedef int uid_t;
typedef int gid_t;
typedef int id_t;
#define geteuid()   (0)
#define getegid()   (0)
#define getuid()    (0)
#define getgid()    (0)
#define setuid(u)   (errno = ENOSYS, -1)
#define setgid(g)   (errno = ENOSYS, -1)

/* SUID/SGID mode bits — not meaningful on Windows, but referenced by
 * rsync's permission-bit logic. Define to 0 so the bits never match. */
#ifndef S_ISUID
# define S_ISUID 0
#endif
#ifndef S_ISGID
# define S_ISGID 0
#endif
#ifndef S_ISVTX
# define S_ISVTX 0
#endif

/* rsync.h, when HAVE_DIRENT_H is undefined, does `#define dirent direct'
 * and then dereferences di->d_name. Provide a minimal `struct direct'
 * so that compile units which never actually call opendir/readdir still
 * link. Real directory iteration on Windows lives in win_fs.c. */
struct direct {
    char d_name[260];
};

/* chown / lchown — no-op stubs (return success so --archive doesn't error). */
#define chown(p, u, g)  (0)
#define lchown(p, u, g) (0)

/* File mode bits absent from MSVC's sys/stat.h. Many of these never
 * occur on Windows (block devices, sockets, named pipes in the FS
 * sense), but rsync references the bits unconditionally for masking
 * and predicate macros. Provide values that match POSIX where it
 * matters; otherwise zero. */
#ifndef S_IFLNK
# define S_IFLNK 0120000
#endif
#ifndef S_IFBLK
# define S_IFBLK 0060000
#endif
#ifndef _S_IFBLK
# define _S_IFBLK S_IFBLK
#endif
#ifndef S_IFSOCK
# define S_IFSOCK 0140000
#endif
#ifndef S_ISLNK
# define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef S_ISBLK
# define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif
#ifndef S_ISSOCK
# define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif
#ifndef S_ISFIFO
# define S_ISFIFO(m) (((m) & S_IFMT) == _S_IFIFO)
#endif
#ifndef S_ISDIR
# define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
# define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISCHR
# define S_ISCHR(m)  (((m) & _S_IFMT) == _S_IFCHR)
#endif

/* Device-number macros — Windows has no concept of device major/minor;
 * stat() on Windows returns a single st_rdev that we don't decode.
 * Force-redefine so we override any prior decl/macro coming from MSVC's
 * sys/types.h (some SDK builds declare major as a function). */
#undef  major
#undef  minor
#undef  makedev
#define major(d)         ((unsigned)((d) >> 8) & 0xff)
#define minor(d)         ((unsigned)(d) & 0xff)
#define makedev(maj, min) (((maj) << 8) | ((min) & 0xff))

/* gettimeofday — POSIX; rsync uses it for log timestamps and progress
 * accounting. Implement via GetSystemTimeAsFileTime + epoch conversion. */
struct timezone;
int win_gettimeofday(struct timeval *tv, struct timezone *tz);
#undef  gettimeofday
#define gettimeofday(tv, tz) win_gettimeofday((tv), (tz))

/* Other POSIX shims. _stricmp/_strnicmp are declared by <string.h>;
 * _pipe/_commit are declared by <io.h> which we avoid (clashes with
 * rsync's local io.h). Provide our own prototypes for the latter two,
 * but DON'T re-prototype _stricmp/_strnicmp — that conflicts with the
 * already-imported declarations. */
int __cdecl _pipe(int *fds, unsigned int psize, int textmode);
int __cdecl _commit(int fd);
#define strcasecmp(a, b)       _stricmp((a), (b))
#define strncasecmp(a, b, n)   _strnicmp((a), (b), (n))
/* pipe(): create an anonymous pipe. Windows _pipe() needs size + flags. */
static __inline int win_pipe(int fds[2]) { return _pipe(fds, 65536, 0x8000 /* O_BINARY */); }
#define pipe(fds) win_pipe(fds)
/* fsync(): flush file buffers. fd -> HANDLE -> FlushFileBuffers; or use
 * _commit() which does the same thing via the CRT. */
#define fsync(fd) _commit(fd)
/* kill(): we never deliver actual signals to other processes from rsync.
 * The only real use is "is the process alive" (kill(pid, 0)). */
static __inline int win_kill(pid_t pid, int sig)
{
    if (sig != 0) { errno = ENOSYS; return -1; }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) { errno = ESRCH; return -1; }
    DWORD code;
    int alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive ? 0 : (errno = ESRCH, -1);
}
#define kill(p, s) win_kill((p), (s))
/* localtime_r(t, &tm) -> localtime_s(&tm, t).  Note arg order is swapped. */
static __inline struct tm *win_localtime_r(const time_t *t, struct tm *out)
{
    return (localtime_s(out, t) == 0) ? out : NULL;
}
#define localtime_r(t, out) win_localtime_r((t), (out))

/* lstat: MSVC has stat() but no lstat(). For non-symlink paths the two
 * are identical. For symlinks we'd want to NOT follow the link; MSVC's
 * stat() does follow reparse points by default. A perfect lstat would
 * use CreateFileA with FILE_FLAG_OPEN_REPARSE_POINT + GetFileInformation
 * BY_HANDLE; rsync mostly needs basic mode + size info though, so for
 * now we use stat()'s result. Symlink detection happens via
 * GetFileAttributesA elsewhere in win_fs.c. */
#define lstat(path, buf) stat((path), (buf))

/* utime() shim — MSVC's _utime() doesn't work on directories; rsync
 * tries to set times on the destination dir during pull and fails with
 * EACCES. Route through CreateFile + SetFileTime with FILE_FLAG_BACKUP_
 * SEMANTICS, which works for both files and directories. */
struct utimbuf {
    time_t actime;     /* access time */
    time_t modtime;    /* modification time */
};
static __inline int win_utime_shim(const char *path, const struct utimbuf *buf)
{
    HANDLE h = CreateFileA(path,
                           FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }
    FILETIME atime, mtime;
    unsigned long long a_ticks = ((unsigned long long)buf->actime  + 11644473600ULL) * 10000000ULL;
    unsigned long long m_ticks = ((unsigned long long)buf->modtime + 11644473600ULL) * 10000000ULL;
    atime.dwLowDateTime  = (DWORD)(a_ticks & 0xFFFFFFFFULL);
    atime.dwHighDateTime = (DWORD)(a_ticks >> 32);
    mtime.dwLowDateTime  = (DWORD)(m_ticks & 0xFFFFFFFFULL);
    mtime.dwHighDateTime = (DWORD)(m_ticks >> 32);
    BOOL ok = SetFileTime(h, NULL, &atime, &mtime);
    CloseHandle(h);
    if (!ok) { errno = EACCES; return -1; }
    return 0;
}
#define utime(p, b) win_utime_shim((p), (b))
#define HAVE_STRUCT_UTIMBUF 1

/* select() — winsock's version only handles SOCKETs, but rsync's io.c
 * calls select() on pipe fds returned by piped_child / win_spawn. Route
 * through our shim in win32/win_select.c, which classifies each fd
 * (socket vs pipe) and dispatches accordingly. */
int win_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout);
#define select(n, r, w, e, t) win_select((n), (r), (w), (e), (t))

/* ROLE_TLS — thread-local storage qualifier for globals that diverge
 * after the receiver/generator or sender/server split. On Linux fork()
 * gives each side a private copy automatically; on Windows we run those
 * two paths in two THREADS of one process, so the divergent globals
 * must be per-thread. Inert on non-Windows builds.
 *
 * Apply to the DEFINITION of a global (in its .c file). The matching
 * `extern` declarations in headers don't need the qualifier on MSVC. */
#define ROLE_TLS __declspec(thread)

/* win_thread_fork(fn, arg) — Windows replacement for fork() in the
 * specific case where the "child" path is going to call a known
 * function with its own arguments (do_recv's receiver branch,
 * local_child's server branch). Returns:
 *   - On the calling thread, immediately, a "fake pid" that
 *     win_waitpid() can be called on (registered in the same
 *     pid -> HANDLE table that win_fork uses).
 *   - The new thread runs fn(arg). When it returns, the thread's
 *     exit code becomes the pid's wait result.
 * Errors return (pid_t)-1 with errno set. */
typedef int (*win_thread_main_t)(void *arg);
pid_t win_thread_fork(win_thread_main_t fn, void *arg);

/* Install a SetUnhandledExceptionFilter that logs the crash code +
 * thread id to recv-trace.log. Call once at startup; useful for
 * diagnosing thread-related crashes that bypass exit_cleanup. */
void win_install_crash_handler(void);

/* iobuf.in transfer across do_recv thread boundary. Forward declaration
 * here so do_recv (which lives in main.c) can pass a snapshot blob to
 * the receiver thread. See io.c::iobuf_snapshot_in_state for the
 * full description. */
struct iobuf_in_snapshot {
    char  *bytes;
    size_t len;
    size_t raw_input_ends_offset;
    int    in_multiplexed;
};
void iobuf_snapshot_in_state(struct iobuf_in_snapshot *snap);
void iobuf_restore_in_state(struct iobuf_in_snapshot *snap);

/* POSIX permission bits — MSVC's sys/stat.h has _S_IREAD/_S_IWRITE/
 * _S_IEXEC for owner only. Define the rest as zero (Windows ACLs
 * don't map cleanly onto rwx/group/other). */
#ifndef S_IRUSR
# define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
# define S_IWUSR _S_IWRITE
#endif
#ifndef S_IXUSR
# define S_IXUSR _S_IEXEC
#endif
#ifndef S_IRWXU
# define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#endif
#ifndef S_IRGRP
# define S_IRGRP 0
#endif
#ifndef S_IWGRP
# define S_IWGRP 0
#endif
#ifndef S_IXGRP
# define S_IXGRP 0
#endif
#ifndef S_IRWXG
# define S_IRWXG 0
#endif
#ifndef S_IROTH
# define S_IROTH 0
#endif
#ifndef S_IWOTH
# define S_IWOTH 0
#endif
#ifndef S_IXOTH
# define S_IXOTH 0
#endif
#ifndef S_IRWXO
# define S_IRWXO 0
#endif

/* fcntl: rsync's set_nonblocking + file-locking path. Windows has
 * neither, so we stub the constants and provide a fcntl() that
 * returns 0 (or applies a non-blocking ioctl when appropriate).
 * The functions in util1.c that lock files are only used by daemon
 * mode, which we've excised. */
#ifndef F_GETFL
# define F_GETFL 1
# define F_SETFL 2
# define F_GETFD 3
# define F_SETFD 4
# define F_GETLK 5
# define F_SETLK 6
# define F_SETLKW 7
# define F_DUPFD 8
# define F_RDLCK 0
# define F_WRLCK 1
# define F_UNLCK 2
# define O_NONBLOCK 04000
# define FNDELAY    O_NONBLOCK
# define FD_CLOEXEC 1
struct flock {
    short l_type;
    short l_whence;
    long  l_start;
    long  l_len;
    int   l_pid;
};
static __inline int win_fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }
# define fcntl(f, c, ...) win_fcntl((f), (c), ##__VA_ARGS__)
#endif

/* DIR / opendir / readdir / closedir — provide a minimal POSIX-shape
 * directory iterator backed by FindFirstFileA / FindNextFileA. Code
 * paths that recurse into directories will exercise these. */
typedef struct WIN_DIR_S {
    HANDLE          handle;
    int             have_pending;
    WIN32_FIND_DATAA pending;
    struct direct   entry;
} DIR;
DIR    *win_opendir(const char *path);
struct direct *win_readdir(DIR *d);
int     win_closedir(DIR *d);
#define opendir(p)  win_opendir(p)
#define readdir(d)  win_readdir(d)
#define closedir(d) win_closedir(d)
/* rsync's d_name() helper in ifuncs.h expects `struct dirent` to exist.
 * We've redirected dirent to direct above (line 91); ifuncs.h will see
 * `struct dirent` is a typedef of `struct direct`. */
#ifndef _DIRENT_HAVE_D_TYPE
# define DT_UNKNOWN 0
# define DT_FIFO    1
# define DT_CHR     2
# define DT_DIR     4
# define DT_BLK     6
# define DT_REG     8
# define DT_LNK     10
# define DT_SOCK    12
#endif

/* struct passwd / struct group — Windows has no /etc/passwd. The lookups
 * are only used to map remote uid/gid names to local accounts; rsync's
 * --archive flag triggers this. We return NULL from get*pwuid / get*grgid
 * stubs so name->id resolution silently fails (numeric uids still work). */
struct passwd {
    const char *pw_name;
    const char *pw_passwd;
    uid_t       pw_uid;
    gid_t       pw_gid;
    const char *pw_gecos;
    const char *pw_dir;
    const char *pw_shell;
};
struct group {
    const char *gr_name;
    const char *gr_passwd;
    gid_t       gr_gid;
    char      **gr_mem;
};
static __inline struct passwd *getpwuid(uid_t u)        { (void)u; return NULL; }
static __inline struct passwd *getpwnam(const char *n)  { (void)n; return NULL; }
static __inline struct group  *getgrgid(gid_t g)        { (void)g; return NULL; }
static __inline struct group  *getgrnam(const char *n)  { (void)n; return NULL; }
static __inline void           endpwent(void)            { }
static __inline void           endgrent(void)            { }
static __inline struct passwd *getpwent(void)            { return NULL; }
static __inline struct group  *getgrent(void)            { return NULL; }

/* syslog — daemon-only, so we just provide constants + no-op stubs. */
#ifndef LOG_PID
# define LOG_PID     0
# define LOG_USER    0
# define LOG_DAEMON  0
# define LOG_LOCAL0  0
# define LOG_LOCAL1  0
# define LOG_LOCAL2  0
# define LOG_LOCAL3  0
# define LOG_LOCAL4  0
# define LOG_LOCAL5  0
# define LOG_LOCAL6  0
# define LOG_LOCAL7  0
# define LOG_EMERG   0
# define LOG_ALERT   1
# define LOG_CRIT    2
# define LOG_ERR     3
# define LOG_WARNING 4
# define LOG_NOTICE  5
# define LOG_INFO    6
# define LOG_DEBUG   7
static __inline void openlog(const char *id, int o, int f) { (void)id; (void)o; (void)f; }
static __inline void closelog(void)                          { }
static __inline void syslog(int p, const char *fmt, ...)     { (void)p; (void)fmt; }
#endif

/* TCP socket option constants come from <winsock2.h> via
 * <ws2tcpip.h>; netinet/tcp.h is unused on Windows. */

/* fork() and waitpid() are redirected to our NT-based implementations.
 * See win32/win_fork.c. */
#define fork()                       win_fork()
#define waitpid(p, s, o)             win_waitpid((p), (s), (o))

/* sigaction / sigprocmask: rsync uses these for SIGINT (Ctrl-C) cleanup
 * and SIGCHLD reaping. MSVC's CRT only exposes signal() and has no
 * concept of signal masks. We stub these to no-ops; SIGINT is delivered
 * to the CRT via the console control handler installed at startup, which
 * is wired into the same signal() table rsync registers.
 *
 * sigemptyset / sigaddset etc. operate on an opaque sigset_t we redefine
 * as an int; the values don't matter because sigprocmask is a no-op.  */
typedef int sigset_t;
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};
#define SIG_BLOCK      0
#define SIG_UNBLOCK    1
#define SIG_SETMASK    2
#define SA_RESTART     0
#define SA_NOCLDSTOP   0
static __inline int win_sigemptyset(sigset_t *s)         { *s = 0; return 0; }
static __inline int win_sigfillset(sigset_t *s)          { *s = ~0; return 0; }
static __inline int win_sigaddset(sigset_t *s, int n)    { (void)n; *s |= 1; return 0; }
static __inline int win_sigdelset(sigset_t *s, int n)    { (void)n; *s &= ~1; return 0; }
static __inline int win_sigprocmask(int how, const sigset_t *set, sigset_t *old)
    { (void)how; (void)set; if (old) *old = 0; return 0; }
static __inline int win_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    if (oact) memset(oact, 0, sizeof(*oact));
    if (act && act->sa_handler) signal(sig, act->sa_handler);
    return 0;
}
#define sigemptyset(s)      win_sigemptyset(s)
#define sigfillset(s)       win_sigfillset(s)
#define sigaddset(s, n)     win_sigaddset((s), (n))
#define sigdelset(s, n)     win_sigdelset((s), (n))
#define sigprocmask(h, s, o) win_sigprocmask((h), (s), (o))
#define sigaction(s, a, o)   win_sigaction((s), (a), (o))

/* SIGPIPE doesn't exist on Windows; closed-pipe writes return ERROR_BROKEN_PIPE
 * which the CRT maps to EPIPE. Map the constant to a value that won't collide
 * with the real signals (SIGINT=2, SIGTERM=15, etc.). */
#ifndef SIGPIPE
# define SIGPIPE 13
#endif
#ifndef SIGHUP
# define SIGHUP 1
#endif
#ifndef SIGUSR1
# define SIGUSR1 10
#endif
#ifndef SIGUSR2
# define SIGUSR2 12
#endif
#ifndef SIGCHLD
# define SIGCHLD 17
#endif
#ifndef SIGSTOP
# define SIGSTOP 19
#endif
#ifndef SIGCONT
# define SIGCONT 18
#endif
#ifndef SIGALRM
# define SIGALRM 14
#endif

/* POSIX access() mode flags. MSVC's <io.h> uses different values (0=F_OK,
 * 2=W_OK, 4=R_OK, 6=R|W) — same numbers POSIX uses, but the macros are
 * named _F_OK etc. with leading underscores. Provide the unprefixed names. */
#ifndef F_OK
# define F_OK 0
#endif
#ifndef X_OK
# define X_OK 1
#endif
#ifndef W_OK
# define W_OK 2
#endif
#ifndef R_OK
# define R_OK 4
#endif
/* alarm() is part of POSIX; rsync uses it for connect_timeout in
 * socket.c. Windows has no signal-driven timers. We implement
 * connect timeouts elsewhere via select(); the alarm()/SIGALRM
 * call site is a no-op. */
static __inline unsigned int win_alarm(unsigned int s) { (void)s; return 0; }
#define alarm(s) win_alarm(s)

/* WIFEXITED / WEXITSTATUS / WTERMSIG don't exist in MSVC. win_waitpid
 * encodes exits as (code << 8); no signals possible. */
#ifndef WIFEXITED
# define WIFEXITED(s)   (((s) & 0x7f) == 0)
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#endif
#ifndef WIFSIGNALED
# define WIFSIGNALED(s) (0)
#endif
#ifndef WTERMSIG
# define WTERMSIG(s)    (0)
#endif
#ifndef WIFSTOPPED
# define WIFSTOPPED(s)  (0)
#endif
#ifndef WSTOPSIG
# define WSTOPSIG(s)    (0)
#endif
#ifndef WNOHANG
# define WNOHANG  1
# define WUNTRACED 2
#endif

/* Forward declarations from our win32/ replacements. */
#include "win32/win_spawn.h"
#include "win32/win_fork.h"
#include "win32/win_paths.h"
#include "win32/win_fs.h"
#include "win32/win_ssh.h"

#endif /* WIN32_NATIVE */
#endif /* WIN32_COMPAT_H */
