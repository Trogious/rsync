/* win32/win_paths.h
 *
 * Path normalization and classification helpers for Windows.
 * Used by upstream code paths around argument parsing (options.c) and
 * filesystem syscalls (syscall.c).
 */
#ifndef WIN_PATHS_H
#define WIN_PATHS_H

/* True (1) if path begins with a Windows drive letter spec ("X:"), where
 * X is [A-Za-z]. Detects "C:", "C:/", "C:\", etc. */
int win_is_drive_path(const char *path);

/* True (1) if path is a UNC path beginning with "\\server\share" or with
 * the "\\?\" / "\\.\" long/device path prefixes. */
int win_is_unc_path(const char *path);

/* In-place: replace backslashes with forward slashes for rsync's
 * internal canonical form. Returns its argument. */
char *win_normalize_path(char *path);

/* If path would be unsafe at >MAX_PATH (260), return a freshly-allocated
 * "\\?\<abs-path>" form. Otherwise returns NULL.
 * Caller frees the returned buffer. */
char *win_long_path_prefix(const char *path);

#endif /* WIN_PATHS_H */
