/*
 * Routines used to setup various kinds of inter-process pipes.
 *
 * Copyright (C) 1996-2000 Andrew Tridgell
 * Copyright (C) 1996 Paul Mackerras
 * Copyright (C) 2001, 2002 Martin Pool <mbp@samba.org>
 * Copyright (C) 2004-2020 Wayne Davison
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, visit the http://fsf.org website.
 */

#include "rsync.h"

extern ROLE_TLS int am_sender;
extern ROLE_TLS int am_server;
extern int blocking_io;
extern int filesfrom_fd;
extern int munge_symlinks;
extern char *logfile_name;
extern int remote_option_cnt;
extern const char **remote_options;
extern struct chmod_mode_struct *chmod_modes;

/**
 * Create a child connected to us via its stdin/stdout.
 *
 * This is derived from CVS code
 *
 * Note that in the child STDIN is set to blocking and STDOUT
 * is set to non-blocking. This is necessary as rsh relies on stdin being blocking
 *  and ssh relies on stdout being non-blocking
 *
 * If blocking_io is set then use blocking io on both fds. That can be
 * used to cope with badly broken rsh implementations like the one on
 * Solaris.
 **/
pid_t piped_child(char **command, int *f_in, int *f_out)
{
#ifdef WIN32_NATIVE
	pid_t pid;

	if (DEBUG_GTE(CMD, 1))
		print_child_argv("opening connection using:", command);

	pid = win_spawn_remote_shell(command, f_in, f_out);
	if (pid == (pid_t)-1) {
		rsyserr(FERROR, errno, "Failed to spawn %s", command[0]);
		exit_cleanup(RERR_IPC);
	}
	return pid;
#else
	pid_t pid;
	int to_child_pipe[2];
	int from_child_pipe[2];

	if (DEBUG_GTE(CMD, 1))
		print_child_argv("opening connection using:", command);

	if (fd_pair(to_child_pipe) < 0 || fd_pair(from_child_pipe) < 0) {
		rsyserr(FERROR, errno, "pipe");
		exit_cleanup(RERR_IPC);
	}

	pid = do_fork();
	if (pid == -1) {
		rsyserr(FERROR, errno, "fork");
		exit_cleanup(RERR_IPC);
	}

	if (pid == 0) {
		if (dup2(to_child_pipe[0], STDIN_FILENO) < 0
		 || close(to_child_pipe[1]) < 0
		 || close(from_child_pipe[0]) < 0
		 || dup2(from_child_pipe[1], STDOUT_FILENO) < 0) {
			rsyserr(FERROR, errno, "Failed to dup/close");
			exit_cleanup(RERR_IPC);
		}
		if (to_child_pipe[0] != STDIN_FILENO)
			close(to_child_pipe[0]);
		if (from_child_pipe[1] != STDOUT_FILENO)
			close(from_child_pipe[1]);
		set_blocking(STDIN_FILENO);
		if (blocking_io > 0)
			set_blocking(STDOUT_FILENO);
		execvp(command[0], command);
		rsyserr(FERROR, errno, "Failed to exec %s", command[0]);
		exit_cleanup(RERR_IPC);
	}

	if (close(from_child_pipe[1]) < 0 || close(to_child_pipe[0]) < 0) {
		rsyserr(FERROR, errno, "Failed to close");
		exit_cleanup(RERR_IPC);
	}

	*f_in = from_child_pipe[0];
	*f_out = to_child_pipe[1];

	return pid;
#endif /* WIN32_NATIVE */
}

/* This function forks a child which calls child_main().  First,
 * however, it has to establish communication paths to and from the
 * newborn child.  It creates two socket pairs -- one for writing to
 * the child (from the parent) and one for reading from the child
 * (writing to the parent).  Since that's four socket ends, each
 * process has to close the two ends it doesn't need.  The remaining
 * two socket ends are retained for reading and writing.  In the
 * child, the STDIN and STDOUT file descriptors refer to these
 * sockets.  In the parent, the function arguments f_in and f_out are
 * set to refer to these sockets. */
#ifdef WIN32_NATIVE
/* Windows variant: spawn rsync.exe as a subprocess (via CreateProcessA)
 * instead of forking. Two reasons rule out the thread + TLS treatment
 * that worked for do_recv:
 *   1. fork-via-RtlCloneUserProcess deadlocks here, just like do_recv.
 *   2. local_child's "child" branch runs setup_protocol(), which mutates
 *      a large amount of shared global state (compat_flags, file_extra_cnt,
 *      negotiated checksum/compression, ...). Running it concurrently in
 *      two threads of the same process races and crashes.
 * A separate process gives us the same process-level isolation POSIX gets
 * from fork(), at the cost of one extra exec + arg-passing round-trip. */

/* Spawn rsync.exe as a subprocess to serve as the local "child" — this
 * sidesteps the thread + shared-state issues that would arise from running
 * setup_protocol() concurrently in two threads sharing globals. The child
 * inherits the parent's filesystem view and parses --server itself. */
static pid_t spawn_local_rsync_child(char **argv, int *f_in, int *f_out)
{
	HANDLE parent_in_r = NULL, parent_in_w = NULL;
	HANDLE parent_out_r = NULL, parent_out_w = NULL;
	SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
	char self_path[MAX_PATH];
	char **new_argv = NULL;
	char *cmdline = NULL;
	int new_argc = 0;
	pid_t pid = (pid_t)-1;

	if (GetModuleFileNameA(NULL, self_path, MAX_PATH) == 0 ||
	    GetModuleFileNameA(NULL, self_path, MAX_PATH) == MAX_PATH) {
		errno = ENOENT;
		return (pid_t)-1;
	}

	/* Build a new argv whose argv[0] points at our own exe path. The
	 * caller's argv[0] is typically "rsync" without a path. */
	{
		int i, count = 0;
		while (argv[count])
			count++;
		new_argv = (char **)malloc(sizeof(char *) * (count + 1));
		if (!new_argv) { errno = ENOMEM; return (pid_t)-1; }
		new_argv[new_argc++] = self_path;
		for (i = 1; i < count; i++)
			new_argv[new_argc++] = argv[i];
		new_argv[new_argc] = NULL;
	}

	if (!CreatePipe(&parent_in_r, &parent_in_w, &sa, 1 << 20)) {
		errno = EIO;
		goto fail;
	}
	SetHandleInformation(parent_in_r, HANDLE_FLAG_INHERIT, 0);

	if (!CreatePipe(&parent_out_r, &parent_out_w, &sa, 1 << 20)) {
		errno = EIO;
		goto fail;
	}
	SetHandleInformation(parent_out_w, HANDLE_FLAG_INHERIT, 0);

	{
		/* Build the CreateProcessA cmdline, applying the Windows
		 * argv-quoting rules (CommandLineToArgvW semantics):
		 *   * wrap each arg in double quotes
		 *   * a run of N backslashes followed by a " is interpreted
		 *     as N/2 literal backslashes + an escaped/closing quote,
		 *     so we have to double the backslash count before any "
		 *     inside the arg AND before the closing ".
		 * Without this, a trailing-backslash path like
		 *   C:\Users\Trog\Temp\foo\
		 * wrapped naively as "C:\Users\Trog\Temp\foo\" reads the
		 * final \" as an escaped quote and the parser bleeds into the
		 * next argument. */
		size_t total = 0;
		int i;
		for (i = 0; i < new_argc; i++)
			total += strlen(new_argv[i]) * 2 + 4; /* worst-case: every char doubled + quotes + space */
		cmdline = (char *)malloc(total + 1);
		if (!cmdline) { errno = ENOMEM; goto fail; }
		char *p = cmdline;
		for (i = 0; i < new_argc; i++) {
			if (i > 0) *p++ = ' ';
			*p++ = '"';
			int backslashes = 0;
			for (const char *c = new_argv[i]; *c; c++) {
				if (*c == '\\') {
					backslashes++;
					continue;
				}
				if (*c == '"') {
					/* double the run of backslashes, then escape " */
					while (backslashes-- > 0) { *p++ = '\\'; *p++ = '\\'; }
					backslashes = 0;
					*p++ = '\\';
					*p++ = '"';
					continue;
				}
				/* flush any pending backslashes verbatim */
				while (backslashes-- > 0) *p++ = '\\';
				backslashes = 0;
				*p++ = *c;
			}
			/* trailing backslashes need doubling before the closing " */
			while (backslashes-- > 0) { *p++ = '\\'; *p++ = '\\'; }
			*p++ = '"';
		}
		*p = '\0';
	}

	{
		STARTUPINFOA si = { sizeof(si) };
		PROCESS_INFORMATION pi = { 0 };
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput  = parent_out_r;
		si.hStdOutput = parent_in_w;
		si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

		if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
		                    NULL, NULL, &si, &pi)) {
			DWORD e = GetLastError();
			errno = (e == ERROR_FILE_NOT_FOUND) ? ENOENT : EIO;
			goto fail;
		}

		CloseHandle(parent_out_r); parent_out_r = NULL;
		CloseHandle(parent_in_w);  parent_in_w  = NULL;
		CloseHandle(pi.hThread);

		*f_in  = _open_osfhandle((intptr_t)parent_in_r,  _O_BINARY);
		*f_out = _open_osfhandle((intptr_t)parent_out_w, _O_BINARY);
		if (*f_in < 0 || *f_out < 0) {
			if (*f_in  < 0) CloseHandle(parent_in_r);
			if (*f_out < 0) CloseHandle(parent_out_w);
			CloseHandle(pi.hProcess);
			errno = EMFILE;
			goto fail;
		}

		win_register_external_child(pi.dwProcessId, pi.hProcess);
		pid = (pid_t)pi.dwProcessId;
	}

	free(new_argv);
	free(cmdline);
	return pid;

fail:
	if (parent_in_r) CloseHandle(parent_in_r);
	if (parent_in_w) CloseHandle(parent_in_w);
	if (parent_out_r) CloseHandle(parent_out_r);
	if (parent_out_w) CloseHandle(parent_out_w);
	free(new_argv);
	free(cmdline);
	return (pid_t)-1;
}

pid_t local_child(int argc, char **argv, int *f_in, int *f_out,
		  int (*child_main)(int, char*[]))
{
	pid_t pid;

	/* The parent process is always the sender for a local rsync. */
	assert(am_sender);

	(void)argc;
	(void)child_main;

	pid = spawn_local_rsync_child(argv, f_in, f_out);
	if (pid == (pid_t)-1) {
		rsyserr(FERROR, errno, "spawn_local_rsync_child");
		exit_cleanup(RERR_IPC);
	}
	return pid;
}
#else
pid_t local_child(int argc, char **argv, int *f_in, int *f_out,
		  int (*child_main)(int, char*[]))
{
	pid_t pid;
	int to_child_pipe[2];
	int from_child_pipe[2];

	/* The parent process is always the sender for a local rsync. */
	assert(am_sender);

	if (fd_pair(to_child_pipe) < 0 || fd_pair(from_child_pipe) < 0) {
		rsyserr(FERROR, errno, "pipe");
		exit_cleanup(RERR_IPC);
	}

	pid = do_fork();
	if (pid == -1) {
		rsyserr(FERROR, errno, "fork");
		exit_cleanup(RERR_IPC);
	}

	if (pid == 0) {
		am_sender = 0;
		am_server = 1;
		filesfrom_fd = -1;
		munge_symlinks = 0; /* Each side needs its own option. */
		chmod_modes = NULL; /* Let the sending side handle this. */

		/* Let the client side handle this. */
		if (logfile_name) {
			logfile_name = NULL;
			logfile_close();
		}

		if (remote_option_cnt) {
			int rc = remote_option_cnt + 1;
			const char **rv = remote_options;
			if (!parse_arguments(&rc, &rv)) {
				option_error();
				exit_cleanup(RERR_SYNTAX);
			}
		}

		if (dup2(to_child_pipe[0], STDIN_FILENO) < 0
		 || close(to_child_pipe[1]) < 0
		 || close(from_child_pipe[0]) < 0
		 || dup2(from_child_pipe[1], STDOUT_FILENO) < 0) {
			rsyserr(FERROR, errno, "Failed to dup/close");
			exit_cleanup(RERR_IPC);
		}
		if (to_child_pipe[0] != STDIN_FILENO)
			close(to_child_pipe[0]);
		if (from_child_pipe[1] != STDOUT_FILENO)
			close(from_child_pipe[1]);
#ifdef ICONV_CONST
		setup_iconv();
#endif
		child_main(argc, argv);
	}

	if (close(from_child_pipe[1]) < 0 || close(to_child_pipe[0]) < 0) {
		rsyserr(FERROR, errno, "Failed to close");
		exit_cleanup(RERR_IPC);
	}

	*f_in = from_child_pipe[0];
	*f_out = to_child_pipe[1];

	return pid;
}
#endif
