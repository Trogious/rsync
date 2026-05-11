/* win32/stub_daemon.c
 *
 * Error stubs for daemon-only entry points that we excise on Windows.
 * The corresponding upstream source files (clientserver.c, socket.c,
 * loadparm.c, access.c, authenticate.c) are wrapped in
 * `#ifndef WIN32_NATIVE` so they compile to empty translation units on
 * Windows; the symbols below satisfy any remaining references.
 *
 * Replaced/expanded in Phase 6.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE

int start_daemon(int f_in, int f_out)
{
    (void)f_in; (void)f_out;
    rprintf(FERROR,
        "rsync daemon mode is not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

int daemon_main(void)
{
    rprintf(FERROR,
        "rsync daemon mode is not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

int start_accept_loop(int port, int (*fn)(int, int))
{
    (void)port; (void)fn;
    rprintf(FERROR,
        "rsync daemon mode is not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

int start_socket_client(char *host,
                        int remote_argc, char *remote_argv[],
                        int argc, char *argv[])
{
    (void)host; (void)remote_argc; (void)remote_argv;
    (void)argc; (void)argv;
    rprintf(FERROR,
        "rsync:// daemon connections are not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

#endif /* WIN32_NATIVE */
