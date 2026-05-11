/* win32/win_ssh.c
 *
 * Resolves the default remote-shell command on Windows. Lookup order:
 *   1. $RSYNC_RSH (verbatim, like other platforms)
 *   2. %SystemRoot%\System32\OpenSSH\ssh.exe (Win10 1809+, Win11,
 *      Server 2019+; the Microsoft-supplied OpenSSH client)
 *   3. "ssh.exe" — fall back to PATH resolution by CreateProcess.
 */
#include "rsync.h"

#ifdef WIN32_NATIVE
#include "win32/win_ssh.h"

const char *win_default_rsh(void)
{
    static char buf[MAX_PATH];

    DWORD n = GetEnvironmentVariableA("RSYNC_RSH", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        return buf;

    char winroot[MAX_PATH];
    n = GetEnvironmentVariableA("SystemRoot", winroot, sizeof(winroot));
    if (n > 0 && n < sizeof(winroot)) {
        if (snprintf(buf, sizeof(buf), "%s\\System32\\OpenSSH\\ssh.exe",
                     winroot) > 0
            && GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES)
            return buf;
    }

    return "ssh.exe";
}

#endif /* WIN32_NATIVE */
