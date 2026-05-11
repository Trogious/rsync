/* win32/win_child_init.h
 *
 * Child-side counterpart to win_reexec. Inspected at the top of main()
 * on Windows; if we were spawned with --_win_child=<state>, loads the
 * serialized state and dispatches to the right entry point.
 */
#ifndef WIN_CHILD_INIT_H
#define WIN_CHILD_INIT_H

/* Returns >= 0 if this process IS a re-exec'd child (the caller should
 * exit immediately with the returned value as the exit code).
 *
 * Returns -1 if this is the normal main() invocation (caller proceeds
 * with regular argument parsing).
 */
int win_child_init(int argc, char **argv);

#endif /* WIN_CHILD_INIT_H */
