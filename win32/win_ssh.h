/* win32/win_ssh.h
 *
 * Default ssh-client resolution for Windows. Used wherever upstream
 * code reads $RSYNC_RSH (options.c).
 */
#ifndef WIN_SSH_H
#define WIN_SSH_H

/* Returns the default remote-shell command to spawn. Lookup order:
 *   1. $RSYNC_RSH
 *   2. %SystemRoot%\System32\OpenSSH\ssh.exe (Win10 1809+ built-in)
 *   3. "ssh.exe" (rely on PATH resolution by CreateProcess)
 *
 * Returns a pointer to an internal static buffer; do not free.
 */
const char *win_default_rsh(void);

#endif /* WIN_SSH_H */
