/* win32/win_spawn.c
 *
 * Replaces pipe.c::piped_child() on Windows. Spawns the remote-shell
 * process (ssh.exe) with bidirectional anonymous pipes.
 *
 * Earlier revisions of this file delegated to gnulib's create_pipe_bidi,
 * but pulling in gnulib's build system (gl/Makefile.am, 4447 lines of
 * automake; *.in.h sed substitution driven by m4 macros) into rsync's
 * hand-written Makefile.in is intractable for the ~60 LOC we actually
 * need. We do the CreateProcess + CreatePipe dance directly.
 *
 * Compatibility note: Windows has no execvp on the parent side, so we
 * build a single command-line string via MSVC quoting rules. rsync's
 * upstream caller (pipe.c::piped_child) hands us an argv it has already
 * built with the standard fork+exec contract — every arg is one token.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_spawn.h"
#include <errno.h>
#include <fcntl.h>
/* MSVC <io.h> forward declarations live in win32/win_compat.h (pulled in
 * via rsync.h); avoid <io.h> directly so we don't grab rsync's local one. */

/* MSVC-compatible argv -> command-line quoting.
 * Rules: backslashes only need doubling if they precede a literal "
 * inside a quoted argument. Wrap any arg containing whitespace or "
 * in quotes. See "Parsing C++ Command-Line Arguments" in the MSVC docs.
 * Returns a malloc'd string, or NULL on OOM. */
static char *build_cmdline(char *const *argv)
{
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    for (int i = 0; argv[i]; i++) {
        const char *a = argv[i];
        int needs_quote = (*a == '\0') ||
                          strpbrk(a, " \t\n\v\"") != NULL;
        size_t alen = strlen(a);
        size_t want = len + alen * 2 + 4 + (i ? 1 : 0);
        if (want >= cap) {
            while (cap <= want) cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        if (i) buf[len++] = ' ';
        if (needs_quote) buf[len++] = '"';

        int backslashes = 0;
        for (const char *p = a; *p; p++) {
            if (*p == '\\') {
                backslashes++;
                buf[len++] = '\\';
            } else if (*p == '"') {
                for (int b = 0; b < backslashes + 1; b++) buf[len++] = '\\';
                buf[len++] = '"';
                backslashes = 0;
            } else {
                backslashes = 0;
                buf[len++] = *p;
            }
        }
        if (needs_quote) {
            for (int b = 0; b < backslashes; b++) buf[len++] = '\\';
            buf[len++] = '"';
        }
        buf[len] = '\0';
    }
    return buf;
}

pid_t win_spawn_remote_shell(char **argv, int *f_in, int *f_out)
{
    HANDLE parent_in_r = NULL, parent_in_w = NULL;     /* child stdout -> parent_in_r */
    HANDLE parent_out_r = NULL, parent_out_w = NULL;   /* parent_out_w -> child stdin */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    char *cmdline = NULL;
    BOOL ok;

    /* CreatePipe size = 0 means "use the system default" (4 KB on most
     * Windows builds), which is too small for rsync's protocol — the
     * sender easily fills 4 KB of file list before the receiver drains
     * a byte. Hint a 1 MB buffer so blocking writes are rare. */
    if (!CreatePipe(&parent_in_r, &parent_in_w, &sa, 1 << 20)) {
        errno = EIO;
        return (pid_t)-1;
    }
    if (!SetHandleInformation(parent_in_r, HANDLE_FLAG_INHERIT, 0)) {
        errno = EIO;
        goto fail;
    }
    if (!CreatePipe(&parent_out_r, &parent_out_w, &sa, 1 << 20)) {
        errno = EIO;
        goto fail;
    }
    if (!SetHandleInformation(parent_out_w, HANDLE_FLAG_INHERIT, 0)) {
        errno = EIO;
        goto fail;
    }

    cmdline = build_cmdline(argv);
    if (!cmdline) { errno = ENOMEM; goto fail; }

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = parent_out_r;          /* child reads from here */
    si.hStdOutput = parent_in_w;           /* child writes to here */
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    /* lpApplicationName=NULL lets the runtime search PATH using cmdline[0]. */
    ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                        NULL, NULL, &si, &pi);
    if (!ok) {
        DWORD e = GetLastError();
        errno = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
                ? ENOENT : EIO;
        goto fail;
    }

    /* Close the ends the child owns. */
    CloseHandle(parent_out_r); parent_out_r = NULL;
    CloseHandle(parent_in_w);  parent_in_w  = NULL;
    CloseHandle(pi.hThread);

    int fd_in  = _open_osfhandle((intptr_t)parent_in_r,  _O_BINARY);
    int fd_out = _open_osfhandle((intptr_t)parent_out_w, _O_BINARY);
    if (fd_in < 0 || fd_out < 0) {
        if (fd_in  < 0) CloseHandle(parent_in_r);
        if (fd_out < 0) CloseHandle(parent_out_w);
        CloseHandle(pi.hProcess);
        free(cmdline);
        errno = EMFILE;
        return (pid_t)-1;
    }

    *f_in  = fd_in;
    *f_out = fd_out;

    /* Hand the process HANDLE to the fork table so win_waitpid can find it.
     * Prototype comes from win32/win_fork.h via the win_compat.h chain. */
    win_register_external_child(pi.dwProcessId, pi.hProcess);

    free(cmdline);
    return (pid_t)pi.dwProcessId;

fail:
    if (parent_in_r)  CloseHandle(parent_in_r);
    if (parent_in_w)  CloseHandle(parent_in_w);
    if (parent_out_r) CloseHandle(parent_out_r);
    if (parent_out_w) CloseHandle(parent_out_w);
    free(cmdline);
    return (pid_t)-1;
}

#endif /* WIN32_NATIVE */
