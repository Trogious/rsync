/* win32/win_reexec.c
 *
 * Implements "fork-of-self-with-different-entry-point" on Windows.
 *
 * Strategy:
 *   1. Create two anonymous pipes for parent <-> child stdio.
 *   2. Mark only the child-side pipe ends as inheritable.
 *   3. Serialize role + a few critical globals + argv into a temp
 *      state file, naming the child-side handles by value.
 *   4. CreateProcess our own exe with --_win_child=<state-path>.
 *   5. Parent returns child PID + parent-end fds. Child runs main(),
 *      where win_child_init detects the marker and dispatches.
 *
 * Why a state file rather than env vars or extra CLI args?
 *   - Env vars get inherited unfiltered (sensitive!) and have size limits.
 *   - CLI args are visible to anyone running tasklist/Process Explorer.
 *   - State files give us a private channel readable only by the child.
 *
 * The state file is unlinked from the child as soon as it's loaded.
 *
 * Currently dispatches WIN_ROLE_LOCAL_CHILD. Roles RECEIVER and
 * GENERATOR are NOT yet supported here — see PORTING.md for the
 * design choice (state serialization vs threads vs fork-clone API).
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_reexec.h"
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>

extern int am_server;

/* On-disk state format. All multi-byte fields are host-endian (parent
 * and child are the same exe / same arch / same endianness, so this is
 * safe). Layout:
 *   - 4 bytes magic "RWN1"
 *   - int32  role
 *   - int32  parent_am_server
 *   - uint64 in_handle   (child end of parent->child pipe)
 *   - uint64 out_handle  (child end of child->parent pipe)
 *   - int32  argc
 *   - argc * { uint32 len; len bytes }    -- arg bytes (NOT NUL-terminated)
 */
#define WIN_REEXEC_MAGIC "RWN1"

static int write_all(HANDLE h, const void *buf, DWORD len)
{
    const char *p = (const char *)buf;
    DWORD written;
    while (len > 0) {
        if (!WriteFile(h, p, len, &written, NULL) || written == 0)
            return -1;
        p += written;
        len -= written;
    }
    return 0;
}

static int write_state_file(const char *path,
                            win_role_t role,
                            HANDLE in_handle,
                            HANDLE out_handle,
                            int argc, char **argv)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    int32_t role32     = (int32_t)role;
    int32_t srv        = (int32_t)am_server;
    uint64_t in_h64    = (uint64_t)(uintptr_t)in_handle;
    uint64_t out_h64   = (uint64_t)(uintptr_t)out_handle;
    int32_t argc32     = (int32_t)argc;

    if (write_all(h, WIN_REEXEC_MAGIC, 4) < 0)              goto err;
    if (write_all(h, &role32,  sizeof(role32))  < 0)        goto err;
    if (write_all(h, &srv,     sizeof(srv))     < 0)        goto err;
    if (write_all(h, &in_h64,  sizeof(in_h64))  < 0)        goto err;
    if (write_all(h, &out_h64, sizeof(out_h64)) < 0)        goto err;
    if (write_all(h, &argc32,  sizeof(argc32))  < 0)        goto err;

    for (int i = 0; i < argc; i++) {
        uint32_t len = (uint32_t)strlen(argv[i]);
        if (write_all(h, &len, sizeof(len)) < 0)            goto err;
        if (len > 0 && write_all(h, argv[i], len) < 0)      goto err;
    }
    CloseHandle(h);
    return 0;

err:
    CloseHandle(h);
    DeleteFileA(path);
    return -1;
}

pid_t win_reexec_self_as(win_role_t role,
                         int argc, char **argv,
                         int *f_in, int *f_out)
{
    /* 1. Create pipes. Inheritable by default (sa.bInheritHandle=TRUE);
     *    we'll suppress inheritance of the parent-side ends below. */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE to_child_r = NULL, to_child_w = NULL;
    HANDLE from_child_r = NULL, from_child_w = NULL;

    if (!CreatePipe(&to_child_r, &to_child_w, &sa, 0)) {
        errno = EAGAIN;
        return (pid_t)-1;
    }
    if (!CreatePipe(&from_child_r, &from_child_w, &sa, 0)) {
        CloseHandle(to_child_r);
        CloseHandle(to_child_w);
        errno = EAGAIN;
        return (pid_t)-1;
    }
    SetHandleInformation(to_child_w,   HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(from_child_r, HANDLE_FLAG_INHERIT, 0);

    /* 2. Pick a temp state file path. */
    char temp_dir[MAX_PATH];
    char state_path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp_dir) ||
        !GetTempFileNameA(temp_dir, "rsy", 0, state_path)) {
        CloseHandle(to_child_r);   CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        errno = EIO;
        return (pid_t)-1;
    }

    /* 3. Write state. */
    if (write_state_file(state_path, role,
                         to_child_r, from_child_w, argc, argv) < 0) {
        CloseHandle(to_child_r);   CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        DeleteFileA(state_path);
        return (pid_t)-1;
    }

    /* 4. Build "<self.exe>" --_win_child=<state_path> */
    char self_path[MAX_PATH];
    if (GetModuleFileNameA(NULL, self_path, MAX_PATH) == 0) {
        CloseHandle(to_child_r);   CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        DeleteFileA(state_path);
        errno = EIO;
        return (pid_t)-1;
    }

    /* Quote self_path to handle spaces; state_path is a temp-name with
     * no spaces by construction. */
    char cmdline[MAX_PATH * 3];
    int n = snprintf(cmdline, sizeof(cmdline),
                     "\"%s\" --_win_child=%s", self_path, state_path);
    if (n < 0 || (size_t)n >= sizeof(cmdline)) {
        CloseHandle(to_child_r);   CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        DeleteFileA(state_path);
        errno = E2BIG;
        return (pid_t)-1;
    }

    /* 5. Spawn. */
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, cmdline, NULL, NULL,
                        /* bInheritHandles */ TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(to_child_r);   CloseHandle(to_child_w);
        CloseHandle(from_child_r); CloseHandle(from_child_w);
        DeleteFileA(state_path);
        errno = EAGAIN;
        return (pid_t)-1;
    }

    /* Close handles the child now owns; keep parent-side ends. */
    CloseHandle(to_child_r);
    CloseHandle(from_child_w);
    CloseHandle(pi.hThread);

    /* Wrap parent-end handles into POSIX fds. */
    int fd_in_p  = _open_osfhandle((intptr_t)from_child_r, _O_BINARY);
    int fd_out_p = _open_osfhandle((intptr_t)to_child_w,   _O_BINARY);
    if (fd_in_p < 0 || fd_out_p < 0) {
        if (fd_in_p  >= 0) _close(fd_in_p);
        else CloseHandle(from_child_r);
        if (fd_out_p >= 0) _close(fd_out_p);
        else CloseHandle(to_child_w);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        DeleteFileA(state_path);
        errno = EBADF;
        return (pid_t)-1;
    }

    /* Note: hProcess is leaked here intentionally — child PID is what
     * upstream code waits on via waitpid(). The handle will be reaped
     * when this process exits. */
    CloseHandle(pi.hProcess);

    *f_in  = fd_in_p;
    *f_out = fd_out_p;
    return (pid_t)pi.dwProcessId;
}

#endif /* WIN32_NATIVE */
