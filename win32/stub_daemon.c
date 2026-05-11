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

void start_accept_loop(int port, int (*fn)(int, int))
{
    (void)port; (void)fn;
    rprintf(FERROR,
        "rsync daemon mode is not supported in this Windows build\n");
    exit_cleanup(RERR_UNSUPPORTED);
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

/* Daemon-side globals & functions referenced from client-side code paths.
 * On Linux these come from loadparm.c / clientserver.c / authenticate.c
 * which we wrapped in #ifndef WIN32_NATIVE for the Windows build. The
 * client code paths that touch them are dead in our client-only build
 * (module_id stays -1 == "not running as daemon"); the stubs exist only
 * to satisfy the linker. */
int   module_id        = -1;
int   namecvt_pid      = 0;
int   read_only        = 0;
char *daemon_auth_choices = NULL;
char *module_dir       = NULL;
unsigned int module_dirlen = 0;
char *auth_user        = NULL;
char *full_module_path = NULL;
const char undetermined_hostname[] = "UNDETERMINED";

/* Signatures here must exactly match proto.h. */
BOOL  lp_ignore_nonreadable(int m) { (void)m; return 0; }
char *lp_dont_compress(int m)      { (void)m; return ""; }
char *lp_name(int m)               { (void)m; return ""; }
BOOL  lp_munge_symlinks(int m)     { (void)m; return 0; }
BOOL  lp_numeric_ids(int m)        { (void)m; return 0; }
BOOL  lp_use_chroot(int m)         { (void)m; return 0; }
char *lp_pid_file(void)            { return NULL; }
char *lp_log_file(int m)           { (void)m; return NULL; }
char *lp_refuse_options(int m)     { (void)m; return NULL; }
char *lp_syslog_tag(int m)         { (void)m; return NULL; }
int   lp_syslog_facility(int m)    { (void)m; return 0; }
BOOL  lp_reverse_lookup(int m)     { (void)m; return 0; }
BOOL  lp_write_only(int m)         { (void)m; return 0; }

int  start_inband_exchange(int f_in, int f_out, const char *user, int argc, char *argv[])
{
    (void)f_in; (void)f_out; (void)user; (void)argc; (void)argv;
    rprintf(FERROR, "rsync daemon connections are not supported in this Windows build\n");
    return RERR_UNSUPPORTED;
}

void set_env_num(const char *var, long num)
{
    (void)var; (void)num;
}

void reset_daemon_vars(void)
{
}

BOOL set_dparams(int syntax_check_only)
{
    (void)syntax_check_only;
    return 1;
}

int  daemon_chmod_modes(struct file_struct *file, mode_t *modep,
                        int omit_dir_changes)
{
    (void)file; (void)modep; (void)omit_dir_changes;
    return 0;
}

int  namecvt_call(const char *cmd, const char **name_p, id_t *id_p)
{
    (void)cmd; (void)name_p; (void)id_p;
    return 0;
}

/* base64_encode lives in authenticate.c (excised). socket.c references
 * it via establish_proxy_connection which we never enter in client mode
 * because that code path requires HTTP CONNECT support. */
void base64_encode(const char *buf, int len, char *out, int pad)
{
    (void)buf; (void)len; (void)pad;
    if (out) out[0] = '\0';
}

#endif /* WIN32_NATIVE */
