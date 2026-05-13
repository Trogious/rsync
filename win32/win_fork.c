/* win32/win_fork.c
 *
 * Implements POSIX-style fork() on native Windows via ntdll's
 * RtlCloneUserProcess. Same approach used by Cygwin, MSYS2, and
 * mitchcapper's tar port; works on Windows 7 SP1 through Windows 11
 * + Server 2022.
 *
 * Status code returned to the cloned child is STATUS_PROCESS_CLONED
 * (0x00000129); the parent gets STATUS_SUCCESS.
 *
 * Children are tracked in a small in-process table so that
 * win_waitpid can find the process HANDLE to wait on — the MSVC CRT
 * only knows about processes created via _spawn family, not those
 * we clone.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_fork.h"
#include <errno.h>
#include <process.h>
/* WNOHANG and the W_EXITCODE / WIFEXITED / WEXITSTATUS macros are
 * provided by win32/win_compat.h (no <sys/wait.h> on Windows). */

/* ---- RtlCloneUserProcess prototypes (ntdll internal) ---- */

typedef long NTSTATUS_t;

#define RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES  0x00000002u
#define STATUS_PROCESS_CLONED                    ((NTSTATUS_t)0x00000129L)
#define STATUS_SUCCESS                           ((NTSTATUS_t)0x00000000L)

typedef struct _WIN_CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} WIN_CLIENT_ID;

/* SECTION_IMAGE_INFORMATION is large and version-dependent. We don't
 * read it, so a sufficiently-sized opaque blob is enough. The struct
 * is ~76 bytes on x64; we allocate 128 for headroom. */
typedef struct _WIN_RTL_USER_PROCESS_INFORMATION {
    ULONG  Size;
    HANDLE Process;
    HANDLE Thread;
    WIN_CLIENT_ID ClientId;
    char   ImageInformation_opaque[128];
} WIN_RTL_USER_PROCESS_INFORMATION;

typedef NTSTATUS_t (NTAPI *PFN_RtlCloneUserProcess)(
    ULONG                                 ProcessFlags,
    void *                                ProcessSecurityDescriptor,
    void *                                ThreadSecurityDescriptor,
    HANDLE                                DebugPort,
    WIN_RTL_USER_PROCESS_INFORMATION *    ProcessInformation
);

static PFN_RtlCloneUserProcess load_rtl_clone(void)
{
    static PFN_RtlCloneUserProcess cached = NULL;
    static volatile LONG resolved = 0;

    if (InterlockedCompareExchangeAcquire(&resolved, 1, 0) == 0) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll)
            cached = (PFN_RtlCloneUserProcess)(uintptr_t)
                GetProcAddress(ntdll, "RtlCloneUserProcess");
    }
    return cached;
}

/* ---- pid -> HANDLE table for win_waitpid ---- */

#define WIN_FORK_MAX_CHILDREN 64

static struct {
    DWORD  pid;
    HANDLE handle;
} child_table[WIN_FORK_MAX_CHILDREN];
static int                  child_count = 0;
static CRITICAL_SECTION     child_table_lock;
static volatile LONG        child_table_initialized = 0;

static void ensure_table_init(void)
{
    if (InterlockedCompareExchangeAcquire(&child_table_initialized, 1, 0) == 0)
        InitializeCriticalSection(&child_table_lock);
}

static void register_child(DWORD pid, HANDLE handle)
{
    ensure_table_init();
    EnterCriticalSection(&child_table_lock);
    if (child_count < WIN_FORK_MAX_CHILDREN) {
        child_table[child_count].pid    = pid;
        child_table[child_count].handle = handle;
        child_count++;
    } else {
        /* Out of slots. Close the handle; future waitpid will fall
         * back to OpenProcess() and lose the exit code if the pid
         * has been recycled — but at least we don't leak the handle. */
        CloseHandle(handle);
    }
    LeaveCriticalSection(&child_table_lock);
}

static HANDLE take_child_handle(DWORD pid)
{
    ensure_table_init();
    HANDLE found = NULL;
    EnterCriticalSection(&child_table_lock);
    for (int i = 0; i < child_count; i++) {
        if (child_table[i].pid == pid) {
            found = child_table[i].handle;
            child_table[i] = child_table[--child_count];
            break;
        }
    }
    LeaveCriticalSection(&child_table_lock);
    return found;
}

static void return_child_handle(DWORD pid, HANDLE handle)
{
    /* For WNOHANG that returned 0: put the handle back so future
     * polls can find it. */
    register_child(pid, handle);
}

/* Public: register a CreateProcess-derived child. Same table as the
 * clone-based fork; lets win_waitpid wait on the ssh.exe spawned by
 * win_spawn_remote_shell. */
void win_register_external_child(DWORD pid, HANDLE h)
{
    register_child(pid, h);
}

/* ---- public API ---- */

pid_t win_fork(void)
{
    PFN_RtlCloneUserProcess clone_fn = load_rtl_clone();
    if (!clone_fn) {
        errno = ENOSYS;
        return (pid_t)-1;
    }

    WIN_RTL_USER_PROCESS_INFORMATION info;
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);

    NTSTATUS_t status = clone_fn(
        RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES,
        NULL, NULL, NULL, &info);

    if (status == STATUS_PROCESS_CLONED) {
        /* CHILD. The clone returns here with a fresh stack frame
         * (well, the same stack contents as the parent). All caller
         * locals are valid; we return 0 to signal "I am the child". */
        return 0;
    }
    if (status == STATUS_SUCCESS) {
        /* PARENT. */
        DWORD child_pid = GetProcessId(info.Process);
        if (child_pid == 0) {
            CloseHandle(info.Process);
            if (info.Thread) CloseHandle(info.Thread);
            errno = EAGAIN;
            return (pid_t)-1;
        }
        register_child(child_pid, info.Process);
        if (info.Thread) CloseHandle(info.Thread);
        return (pid_t)child_pid;
    }

    /* Some NTSTATUS error. STATUS_NO_MEMORY etc. */
    errno = EAGAIN;
    return (pid_t)-1;
}

pid_t win_waitpid(pid_t pid, int *statusp, int options)
{
    if (pid <= 0) {
        /* POSIX waitpid supports -1 / 0 / -pgid; we don't yet.
         * Walk the table and wait for any. Cheap implementation. */
        errno = EINVAL;
        return (pid_t)-1;
    }

    HANDLE h = take_child_handle((DWORD)pid);
    if (!h) {
        /* Could be a process not created by win_fork (e.g., gnulib's
         * create_pipe_bidi for ssh). Try opening by pid. */
        h = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE,
                        FALSE, (DWORD)pid);
        if (!h) {
            errno = ECHILD;
            return (pid_t)-1;
        }
    }

    DWORD timeout = (options & WNOHANG) ? 0 : INFINITE;
    DWORD wr = WaitForSingleObject(h, timeout);

    if (wr == WAIT_TIMEOUT) {
        return_child_handle((DWORD)pid, h);
        return 0;
    }
    if (wr == WAIT_OBJECT_0) {
        DWORD code = 0;
        /* The HANDLE in our table can be a process handle (from
         * win_spawn_remote_shell / win_fork) OR a thread handle (from
         * win_thread_fork). GetExitCodeProcess fails silently on a
         * thread handle and vice versa, leaving code untouched. Try
         * thread first; fall back to process. */
        if (!GetExitCodeThread(h, &code))
            GetExitCodeProcess(h, &code);
        if (statusp) {
            /* POSIX W_EXITCODE layout: (exit << 8) | (signal & 0x7f).
             * We don't have signals; just encode the exit code. */
            *statusp = (int)((code & 0xFF) << 8);
        }
        CloseHandle(h);
        return pid;
    }

    CloseHandle(h);
    errno = ECHILD;
    return (pid_t)-1;
}

#endif /* WIN32_NATIVE */
