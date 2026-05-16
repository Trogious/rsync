/* win32/stub_daemon.c
 *
 * Linker stubs for daemon-side symbols that the Windows build doesn't
 * implement. Since 7f278cca the rsync:// CLIENT side compiles (the
 * relevant chunks of clientserver.c + authenticate.c are now built on
 * Windows), so this file is the smaller residual:
 *   - --daemon entry points (start_daemon / daemon_main / become_daemon
 *     / start_accept_loop) -- still rejected at parse time, but the
 *     linker still wants the symbols since they're addressed elsewhere.
 *   - rsyncd.conf accessor functions lp_*() from loadparm.c -- the
 *     daemon-mode reads of these are unreachable when --daemon is
 *     refused, but a couple of code paths consult them defensively
 *     (module_id == -1 short-circuit) and need a real function to
 *     return-sentinel from.
 *   - A few daemon-side globals (namecvt_pid, daemon_chmod_modes)
 *     read in dead branches of rsync.c / generator.c.
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

/* Signatures here must exactly match proto.h. The daemon-config
 * accessors stay stubbed -- there is no rsyncd.conf parser linked in
 * (loadparm.c is still excised), and the client-mode reads of these
 * are all guarded by module_id != -1 which never fires. */
BOOL  lp_ignore_nonreadable(int m) { (void)m; return 0; }
char *lp_dont_compress(int m)      { (void)m; return ""; }
char *lp_name(int m)               { (void)m; return ""; }
BOOL  lp_munge_symlinks(int m)     { (void)m; return 0; }
BOOL  lp_numeric_ids(int m)        { (void)m; return 0; }
BOOL  lp_use_chroot(int m)         { (void)m; return 0; }
char *lp_pid_file(void)            { return NULL; }
char *lp_log_file(int m)           { (void)m; return NULL; }
char *lp_refuse_options(int m)     { (void)m; return NULL; }
char *lp_charset(int m)            { (void)m; return ""; }
char *lp_syslog_tag(int m)         { (void)m; return NULL; }
int   lp_syslog_facility(int m)    { (void)m; return 0; }
BOOL  lp_reverse_lookup(int m)     { (void)m; return 0; }
BOOL  lp_write_only(int m)         { (void)m; return 0; }
/* exchange_protocols calls this only when am_client=0 (server side
 * generating the @RSYNCD greeting). We're client-only, so this is
 * never actually invoked at runtime; the stub exists only to
 * satisfy the link. */
char *lp_motd_file(void)           { return NULL; }

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

/* daemon_chmod_modes is a *pointer* (struct chmod_mode_struct *) on the
 * daemon-side code path — NOT a function. rsync.c::set_file_attrs reads
 * it as `if (daemon_chmod_modes && !S_ISLNK(...)) new_mode =
 * tweak_mode(new_mode, daemon_chmod_modes);`. An earlier version of
 * this stub defined it as a function, which made the linker take the
 * function address — a non-NULL pointer — and pass it as a struct ptr
 * to tweak_mode. tweak_mode then read garbage from the function's
 * machine code as the chmod_mode_struct fields and crashed. */
struct chmod_mode_struct *daemon_chmod_modes = NULL;

int  namecvt_call(const char *cmd, const char **name_p, id_t *id_p)
{
    (void)cmd; (void)name_p; (void)id_p;
    return 0;
}

#endif /* WIN32_NATIVE */
