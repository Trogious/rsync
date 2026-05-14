/* win32/win_thread.c
 *
 * Spawn-a-thread-and-pretend-it's-fork helper. Used by main.c::do_recv
 * and pipe.c::local_child on Windows, in lieu of fork().
 *
 * Why: RtlCloneUserProcess (the win_fork() approach) deadlocks the
 * cloned child in practice — likely loader-lock state that doesn't
 * cleanly cross the clone boundary on modern Windows. Threads sidestep
 * the issue, at the cost of needing every global that diverges
 * between the two roles to be marked ROLE_TLS (__declspec(thread)).
 *
 * Lifecycle:
 *   - win_thread_fork(fn, arg) creates a thread running fn(arg).
 *   - The thread's HANDLE goes into the same pid->HANDLE table that
 *     win_fork.c maintains, keyed on the thread's Windows TID.
 *   - The caller gets that TID back as the "pid". They can pass it to
 *     win_waitpid() unchanged.
 *   - When fn() returns, the thread's exit code (POSIX-style encoded
 *     by win_register_external_child's wait path) is what waitpid
 *     reports.
 *
 * Caveats:
 *   - kill(pid, SIGUSR2) etc. do NOT work — we can't deliver a signal
 *     to a peer thread. do_recv's "kill(pid, SIGUSR2)" at shutdown is
 *     compiled to a no-op on Windows via win_compat.h's kill() shim
 *     (the equivalent shutdown signal is "thread function returns").
 *   - Threads share heap, fds, and any global not marked ROLE_TLS.
 *     Anything that diverges per role MUST be in ROLE_TLS storage.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include <errno.h>
#include <process.h>   /* _beginthreadex */

struct win_thread_ctx {
    win_thread_main_t fn;
    void             *arg;
};

/* unsigned __stdcall is the signature _beginthreadex expects, NOT the
 * DWORD WINAPI that CreateThread uses. The two ABI's differ on the
 * return type width — getting it wrong gives random stack corruption. */
static unsigned __stdcall win_thread_trampoline(void *p)
{
    struct win_thread_ctx *ctx = (struct win_thread_ctx *)p;
    win_thread_main_t fn = ctx->fn;
    void *arg = ctx->arg;
    free(ctx);
    int rc = fn(arg);
    return (unsigned)rc;
}

pid_t win_thread_fork(win_thread_main_t fn, void *arg)
{
    struct win_thread_ctx *ctx = (struct win_thread_ctx *)malloc(sizeof(*ctx));
    if (!ctx) { errno = ENOMEM; return (pid_t)-1; }
    ctx->fn  = fn;
    ctx->arg = arg;

    /* IMPORTANT: must use _beginthreadex, not CreateThread, so the new
     * thread gets proper CRT initialization. CreateThread starts a thread
     * with uninitialized per-thread CRT state, which causes silent
     * crashes the first time the thread calls a CRT function (fopen,
     * malloc, printf, errno, ...). _beginthreadex returns a HANDLE that
     * the caller must close. */
    unsigned tid = 0;
    uintptr_t h = _beginthreadex(NULL, 0, win_thread_trampoline, ctx, 0, &tid);
    if (!h) {
        free(ctx);
        errno = EAGAIN;
        return (pid_t)-1;
    }
    /* Stash the HANDLE under the TID so win_waitpid finds it. The TID
     * is the "pid" the caller will pass back. TIDs are 32-bit and
     * never collide with real Windows pids in the same process's
     * lifetime — different namespace — so there's no ambiguity. */
    win_register_external_child((DWORD)tid, (HANDLE)h);
    return (pid_t)tid;
}

#endif /* WIN32_NATIVE */
