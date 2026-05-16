/* win32/win_getpass.c
 *
 * Windows getpass() shim. MSVC's CRT removed _getpass years ago, so
 * authenticate.c::auth_client (the rsync:// daemon auth flow) needs a
 * console-API-based replacement. Disables console echo for the
 * duration of the read, then restores the prior mode. The returned
 * pointer is to a static buffer (matches POSIX getpass semantics);
 * the caller must not rely on it surviving a second call.
 *
 * On a redirected stdin (pipe, file) ReadConsoleA fails, so we fall
 * back to fgets() with echo unchanged -- if the user is piping a
 * password in, suppression isn't useful anyway.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE

#include <conio.h>

#define WIN_GETPASS_MAX 256

char *win_getpass(const char *prompt)
{
    static char buf[WIN_GETPASS_MAX];
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD saved_mode = 0;
    DWORD read = 0;
    BOOL is_console = GetConsoleMode(hin, &saved_mode);

    if (prompt) {
        fputs(prompt, stderr);
        fflush(stderr);
    }

    if (is_console) {
        SetConsoleMode(hin, saved_mode & ~(DWORD)ENABLE_ECHO_INPUT);
        if (!ReadConsoleA(hin, buf, sizeof(buf) - 1, &read, NULL))
            read = 0;
        SetConsoleMode(hin, saved_mode);
        /* Move to a new line so the user's next prompt doesn't tail
         * onto the (now-invisible) password they just typed. */
        fputc('\n', stderr);
    } else {
        /* stdin isn't a TTY -- pipe, redirect, or non-console host.
         * Fall back to a plain fgets(); ENABLE_ECHO_INPUT doesn't
         * apply to anonymous-pipe reads anyway. */
        if (!fgets(buf, sizeof(buf), stdin))
            return NULL;
        read = (DWORD)strlen(buf);
    }

    if (read == 0)
        return NULL;

    /* Strip trailing CRLF / LF / CR. ReadConsoleA returns the bytes
     * with the line terminator(s) included. */
    while (read > 0 && (buf[read-1] == '\r' || buf[read-1] == '\n'))
        read--;
    buf[read] = '\0';

    return buf;
}

#endif /* WIN32_NATIVE */
