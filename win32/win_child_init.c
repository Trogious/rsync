/* win32/win_child_init.c
 *
 * Counterpart to win_reexec.c. Called as the first thing in main() on
 * Windows. If we were spawned with --_win_child=<state-path>, loads the
 * serialized state, hooks up inherited pipe handles to stdin/stdout,
 * sets the required globals, and dispatches to the role's entry point.
 *
 * Currently supports WIN_ROLE_LOCAL_CHILD. RECEIVER and GENERATOR roles
 * print an unsupported-feature error and exit — see PORTING.md.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_child_init.h"
#include "win32/win_reexec.h"
#include <errno.h>
#include <fcntl.h>
#include <io.h>

#define WIN_CHILD_FLAG     "--_win_child="
#define WIN_REEXEC_MAGIC   "RWN1"

extern int am_server;
extern int am_sender;

static int read_all(HANDLE h, void *buf, DWORD len)
{
    char *p = (char *)buf;
    DWORD got;
    while (len > 0) {
        if (!ReadFile(h, p, len, &got, NULL) || got == 0)
            return -1;
        p += got;
        len -= got;
    }
    return 0;
}

/* Returns 0 on success, fills *out_role, *out_am_server, *in_handle,
 * *out_handle, *out_argc, *out_argv (malloc'd, NULL-terminated). */
static int load_state(const char *path,
                      win_role_t *out_role,
                      int *out_am_server,
                      HANDLE *in_handle, HANDLE *out_handle,
                      int *out_argc, char ***out_argv)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;

    char magic[4];
    int32_t role32, srv, argc32;
    uint64_t in_h64, out_h64;

    if (read_all(h, magic, 4) < 0 || memcmp(magic, WIN_REEXEC_MAGIC, 4) != 0)
        goto err;
    if (read_all(h, &role32,  sizeof(role32))  < 0) goto err;
    if (read_all(h, &srv,     sizeof(srv))     < 0) goto err;
    if (read_all(h, &in_h64,  sizeof(in_h64))  < 0) goto err;
    if (read_all(h, &out_h64, sizeof(out_h64)) < 0) goto err;
    if (read_all(h, &argc32,  sizeof(argc32))  < 0) goto err;

    if (argc32 <= 0 || argc32 > 4096) goto err;  /* sanity */

    char **argv = (char **)calloc((size_t)argc32 + 1, sizeof(char *));
    if (!argv) goto err;

    for (int i = 0; i < argc32; i++) {
        uint32_t len;
        if (read_all(h, &len, sizeof(len)) < 0) goto err_argv;
        if (len > (1u << 20)) goto err_argv;  /* 1MB per arg max */
        char *arg = (char *)malloc((size_t)len + 1);
        if (!arg) goto err_argv;
        if (len > 0 && read_all(h, arg, len) < 0) {
            free(arg);
            goto err_argv;
        }
        arg[len] = '\0';
        argv[i] = arg;
        continue;
    err_argv:
        for (int j = 0; j < i; j++) free(argv[j]);
        free(argv);
        goto err;
    }

    CloseHandle(h);

    *out_role       = (win_role_t)role32;
    *out_am_server  = (int)srv;
    *in_handle      = (HANDLE)(uintptr_t)in_h64;
    *out_handle     = (HANDLE)(uintptr_t)out_h64;
    *out_argc       = (int)argc32;
    *out_argv       = argv;
    return 0;

err:
    CloseHandle(h);
    return -1;
}

int win_child_init(int argc, char **argv)
{
    /* Scan for our marker. */
    const char *state_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], WIN_CHILD_FLAG, strlen(WIN_CHILD_FLAG)) == 0) {
            state_path = argv[i] + strlen(WIN_CHILD_FLAG);
            break;
        }
    }
    if (!state_path)
        return -1;  /* normal main() invocation */

    win_role_t role;
    int parent_am_server;
    HANDLE in_h, out_h;
    int child_argc;
    char **child_argv;

    if (load_state(state_path,
                   &role, &parent_am_server,
                   &in_h, &out_h,
                   &child_argc, &child_argv) < 0) {
        /* Don't try to log via rprintf — io/options aren't set up yet. */
        fprintf(stderr, "rsync: failed to load Windows re-exec state from %s\n",
                state_path);
        return RERR_IPC;
    }

    /* Clean up the state file — it has handle values that don't outlive
     * this process anyway. */
    DeleteFileA(state_path);

    /* Hook inherited pipe handles to stdin/stdout. */
    int fd_in  = _open_osfhandle((intptr_t)in_h,  _O_BINARY);
    int fd_out = _open_osfhandle((intptr_t)out_h, _O_BINARY);
    if (fd_in < 0 || fd_out < 0) {
        fprintf(stderr, "rsync: failed to open inherited pipe fds (Windows)\n");
        return RERR_IPC;
    }
    if (_dup2(fd_in, 0) < 0 || _dup2(fd_out, 1) < 0) {
        fprintf(stderr, "rsync: failed to dup pipe fds onto stdin/stdout\n");
        return RERR_IPC;
    }
    _close(fd_in);
    _close(fd_out);

    /* Dispatch. */
    switch (role) {
    case WIN_ROLE_LOCAL_CHILD:
        am_sender = 0;
        am_server = 1;
        return child_main(child_argc, child_argv);

    case WIN_ROLE_RECEIVER:
    case WIN_ROLE_GENERATOR:
        /* TODO(win-port): the receiver/generator split needs in-memory
         * state (first_flist, option globals) that we don't yet
         * serialize. See PORTING.md "do_recv state preservation" for
         * design options. */
        fprintf(stderr,
            "rsync: receiver/generator fork is not yet implemented on Windows\n"
            "       (this is the do_recv split — required for downloads).\n");
        return RERR_UNSUPPORTED;

    default:
        fprintf(stderr, "rsync: unknown Windows child role %d\n", (int)role);
        return RERR_IPC;
    }
}

#endif /* WIN32_NATIVE */
