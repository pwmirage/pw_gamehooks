/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include "common.h"

static struct {
	HANDLE gdb_stdout_rd;
	HANDLE gdb_stdout_wr;
	HANDLE gdb_stdin_rd;
	HANDLE gdb_stdin_wr;
	DWORD gdb_pid;
	HANDLE gdb_pipe_thread;
} g_debug;

static DWORD g_game_procid;
static HANDLE g_game_handle;
static HANDLE g_game_mainthread;
static HMODULE g_game_dll;

char g_hookdir[1024];
static char g_gamedir[1024];
static unsigned g_hookcount;

static HMODULE
inject_dll(int connfd, DWORD pid, char *path_to_dll)
{
	HANDLE thr;
	char buf[64];
	LPVOID ext_path_to_dll;
	LPVOID load_lib_winapi_addr;
	HMODULE injected_dll = NULL;
	DWORD thr_state;
	int rc;

	load_lib_winapi_addr = (LPVOID)GetProcAddress(
	GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
	if (load_lib_winapi_addr == NULL) {
		goto err;
	}

	ext_path_to_dll = (LPVOID)VirtualAllocEx(g_game_handle, NULL,
	strlen(path_to_dll), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	if (ext_path_to_dll == NULL) {
		goto err;
	}

	rc = WriteProcessMemory(g_game_handle, (LPVOID)ext_path_to_dll,
			path_to_dll, strlen(path_to_dll), NULL);
	if (rc == 0) {
		goto err_free;
	}


	thr = CreateRemoteThread(g_game_handle, NULL, 0,
			(LPTHREAD_START_ROUTINE)load_lib_winapi_addr,
			(LPVOID)ext_path_to_dll, 0, NULL);
	if (thr == NULL) {
		goto err_free;
	}

	while(GetExitCodeThread(thr, &thr_state)) {
		if(thr_state != STILL_ACTIVE) {
			injected_dll = (HMODULE)thr_state;
			break;
		}
	}
	CloseHandle(thr);

	conn_echo(connfd, "$ inject gamehook.dll\n");

	return injected_dll;

err_free:
	VirtualFreeEx(g_game_handle, ext_path_to_dll, 0, MEM_RELEASE);
err:
	conn_echo(connfd, "Failed to open PW process. Was it closed prematurely?\n");
	return NULL;
}

static int
detach_dll(int connfd, DWORD pid, HMODULE dll)
{
	HANDLE thr;
	char buf[64];
	LPVOID free_lib_winapi_addr;
	DWORD thr_state;
	unsigned start_ts, ts;
	int rc;

	free_lib_winapi_addr = (LPVOID)GetProcAddress(
	GetModuleHandleA("kernel32.dll"), "FreeLibrary");
	if (free_lib_winapi_addr == NULL) {
		return -ENOSYS;
	}

	thr = CreateRemoteThread(g_game_handle, NULL, 0,
			(LPTHREAD_START_ROUTINE)free_lib_winapi_addr,
			(LPVOID)dll, 0, NULL);
	if (thr == NULL) {
		return -EIO;
	}

	conn_echo(connfd, "$ detach gamehook.dll\n");

	start_ts = GetTickCount();
	while(GetExitCodeThread(thr, &thr_state)) {
		if(thr_state != STILL_ACTIVE) {
			break;
		}

		ts = GetTickCount();
		if (ts - start_ts > 500) {
			TerminateThread(thr, 1);
			CloseHandle(thr);
			conn_echo(connfd, "$ Can't detach while the program is stopped in gdb\n");
			return -EACCES;
		}
	}

	CloseHandle(thr);

	Sleep(150);
	return 0;
}

static int
inject(int connfd, bool do_echo)
{
	char dll_path[1024];
	char dll_path2[1024];
	unsigned i;

	for (i = 0; i < g_hookcount; i++) {
		_snprintf(dll_path2, sizeof(dll_path2), "%s\\build\\gamehook_%u.dll", g_hookdir, i);
		DeleteFile(dll_path2);
	}

	_snprintf(dll_path, sizeof(dll_path), "%s\\build\\gamehook.dll", g_hookdir);
	_snprintf(dll_path2, sizeof(dll_path2), "%s\\build\\gamehook_%u.dll", g_hookdir, g_hookcount++);

	if (do_echo) {
		conn_echo(connfd, "$ game.exe %s\n", dll_path2);
	}
	CopyFile(dll_path, dll_path2, FALSE);
	g_game_dll = inject_dll(connfd, g_game_procid, dll_path2);

	return g_game_dll != NULL ? 0 : -1;
}

#define PW_GAMEDIR "C:\\PWMirageTest\\element\\"

static int
start_game(int connfd, bool start_paused)
{
	STARTUPINFO pw_proc_startup_info = {0};
	PROCESS_INFORMATION pw_proc_info = {0};
	BOOL result;
	int rc;

	SetCurrentDirectory(PW_GAMEDIR);
	SetDllDirectory(PW_GAMEDIR);
	pw_proc_startup_info.cb = sizeof(STARTUPINFO);
	result = CreateProcess(NULL, "game.exe --profile=1",
			NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL,
			&pw_proc_startup_info, &pw_proc_info);
	if(!result) {
		conn_echo(connfd, "Could not start the PW process");
		return -1;
	}

	g_game_procid = pw_proc_info.dwProcessId;
	g_game_handle = OpenProcess(PROCESS_ALL_ACCESS, 0, g_game_procid);
	if (g_game_handle == NULL) {
		conn_echo(connfd, "Could not start the PW process");
		return -1;
	}

	SetDllDirectory(NULL);
	SetCurrentDirectory(g_hookdir);
	rc = inject(connfd, false);

	g_game_mainthread = pw_proc_info.hThread;
	if (!start_paused) {
		ResumeThread(g_game_mainthread);
		g_game_mainthread = NULL;
	}

	fprintf(stderr, "Started game.exe with pid %d\n", g_game_procid);
	return rc;
}

static void
unset_close_handle(HANDLE *h)
{
	if (*h) {
		CloseHandle(*h);
		*h = 0;
	}
}

static void
terminate_gdb(void)
{
	HANDLE prochandle;

	if (g_debug.gdb_pipe_thread) {
		/* closing the handle will block if there's a pending ReadFile() on it
		 * (causing a deadlock) so forcefully terminate the thread here */
		TerminateThread(g_debug.gdb_pipe_thread, 1);
		CloseHandle(g_debug.gdb_pipe_thread);
		g_debug.gdb_pipe_thread = NULL;
	}

	unset_close_handle(&g_debug.gdb_stdin_rd);
	unset_close_handle(&g_debug.gdb_stdin_wr);
	unset_close_handle(&g_debug.gdb_stdout_rd);
	unset_close_handle(&g_debug.gdb_stdout_wr);

	/* terminate gdb itself */
	if (g_debug.gdb_pid) {
		prochandle = OpenProcess(PROCESS_TERMINATE, false, g_debug.gdb_pid);
		if (prochandle) {
			TerminateProcess(prochandle, 1);
			CloseHandle(prochandle);
		}
		g_debug.gdb_pid = 0;
	}
}

static int
handle_gdb_input(int sockfd, void *ctx)
{
	char buf[4096];
	char *cmd;
	int cmdsize, rc, off;
	int token;
	BOOL ok;
	DWORD written;

	rc = recv(sockfd, buf, sizeof(buf), 0);
	if (rc <= 0) {
		fprintf(stderr, "gdb conn closed; terminating gdb\n");
		terminate_gdb();
		return 1;
	}

	buf[rc] = 0;
	if (buf[rc - 1] == '\n') {
		buf[rc - 1] = 0;
	}

	fprintf(stderr, "read %d bytes: %s\n", rc, buf);
	rc = sscanf(buf, "%d %n %*s", &token, &off);
	if (rc == 0) {
		cmd = buf;
		cmdsize = sizeof(buf) - 1; /* -1 for newline */
		token = 0;
	} else {
		cmd = buf + off;
		cmdsize = sizeof(buf) - off - 1;
	}

	if (strcmp(cmd, "-exec-interrupt") == 0) {
		DebugBreakProcess(g_game_handle);
	} else if (strstr(cmd, "-file-exec-and-symbols") == cmd) {
		snprintf(cmd, cmdsize, "-file-exec-and-symbols game.exe");
	} else if (strstr(cmd, "-environment-cd") == cmd) {
		snprintf(cmd, cmdsize, "-environment-cd .");
	} else if (strstr(cmd, "-target-select remote") == cmd) {
		snprintf(cmd, cmdsize, "-exec-interrupt");
	} else if (strcmp(cmd, "-exec-run") == 0) {
		snprintf(cmd, cmdsize, "-exec-continue");
	} else if (strcmp(cmd, "kill") == 0) {
		snprintf(cmd, cmdsize, "detach");
	}

	rc = strlen(buf);
	buf[rc++] = '\n';
	ok = WriteFile(g_debug.gdb_stdin_wr, buf, rc, &written, NULL);
	if (!ok) {
		fprintf(stderr, "gdb stdin closed; terminating gdb\n");
		terminate_gdb();
		return 1;
	}

	// fprintf(stderr, "wrote %d bytes\n", written);
	return 0;
}

struct gdb_output_thr_ctx {
	HANDLE stdout_rd;
	int fd;
} g_gdb_output_thr_ctx;


static int
handle_gdb_output(int sockfd, void *ctx)
{
	int dst_fd = (int)(uintptr_t)ctx;
	char buf[4096];
	int rc;

	rc = recv(sockfd, buf, sizeof(buf), 0);
	if (rc <= 0) {
		terminate_gdb();
		fprintf(stderr, "gdb died?\n");
		/* TODO gdb died? */
		return 1;
	}

	/* forward it without any parsing for now */

	send(dst_fd, buf, rc, 0);
	return 0;
}

/**
 * we can't run select() on pipe, so we create a thread blocking on ReadFile()
 * and forward all data to the master socket.
 *
 * This is a separate thread, don't put any logic here!
 */
static DWORD __stdcall
gdb_output_thr_fn(void *_ctx)
{
	struct gdb_output_thr_ctx *ctx = &g_gdb_output_thr_ctx;
	DWORD read;
	char buf[4096];
	bool ok = true;
	int ipc_fd = start_ipc(handle_gdb_output, (void *)(uintptr_t)ctx->fd);

	if (ipc_fd < 0) {
		fprintf(stderr, "start_ipc() failed: %d\n", -ipc_fd);
		return 1;
	}

	while (ok) {
		ok = ReadFile(ctx->stdout_rd, buf, sizeof(buf), &read, NULL);
		if (!ok || read == 0) {
			break;
		}

		send(ipc_fd, buf, read, 0);
	}

	fprintf(stderr, "gdb closed; terminating conn\n");

	return 0;
}

static int
handle_gdb_conn(int sockfd, void *ctx)
{
	STARTUPINFO startup = {0};
	PROCESS_INFORMATION proc = {0};
	SECURITY_ATTRIBUTES saAttr = {0};
	struct sockaddr_in servaddr = {};
	DWORD exit_code, tid;
	BOOL ok;
	char buf[4096];
	int rc;
	DWORD written;
	HANDLE prochandle;
	HANDLE thr;
	struct sockaddr_in cli = {};
	int len = sizeof(cli);

	int connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
	if (connfd < 0) {
		perror("acccept failed");
		return 0;
	}

	fprintf(stderr, "new gdb conn\n");
	listen_sockfd(connfd, handle_gdb_input, NULL);

	saAttr.nLength = sizeof(saAttr);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	ok = CreatePipe(&g_debug.gdb_stdin_rd, &g_debug.gdb_stdin_wr, &saAttr, 0);
	ok = ok && SetHandleInformation(g_debug.gdb_stdin_wr, HANDLE_FLAG_INHERIT, 0);
	ok = ok && CreatePipe(&g_debug.gdb_stdout_rd, &g_debug.gdb_stdout_wr, &saAttr, 0);
	ok = ok && SetHandleInformation(g_debug.gdb_stdout_rd, HANDLE_FLAG_INHERIT, 0);
	if (!ok) {
		rc = GetLastError();
		terminate_gdb();
		return rc;
	}

	GetExitCodeProcess(g_game_handle, &exit_code);
	if (exit_code != STILL_ACTIVE) {
		rc = start_game(connfd, true);
		if (rc != 0) {
			terminate_gdb();
			fprintf(stderr, "master conn closed\n");
			return rc;
		}
	}

	SetCurrentDirectory(PW_GAMEDIR);
	startup.cb = sizeof(startup);
	startup.hStdError = g_debug.gdb_stdout_wr;
    startup.hStdOutput = g_debug.gdb_stdout_wr;
	startup.hStdInput = g_debug.gdb_stdin_rd;
	startup.dwFlags |= STARTF_USESTDHANDLES;

	snprintf(buf, sizeof(buf), "gdb.exe --interpreter=mi2 game.exe --pid=%d",
			g_game_procid);
	ok = CreateProcess(NULL, buf,
			NULL, NULL, TRUE, 0, NULL, NULL,
			&startup, &proc);

	if (g_game_mainthread) {
		ResumeThread(g_game_mainthread);
		g_game_mainthread = NULL;
	}

	SetCurrentDirectory(g_hookdir);
	if(!ok) {
		conn_echo(connfd, "Could not start gdb.exe\n");
		terminate_gdb();
		return -1;
	}

	g_debug.gdb_pid = proc.dwProcessId;
	fprintf(stderr, "Started gdb.exe with pid %d\n", g_debug.gdb_pid);

	g_gdb_output_thr_ctx.stdout_rd = g_debug.gdb_stdout_rd;
	g_gdb_output_thr_ctx.fd = connfd;
	thr = CreateThread(NULL, 0, gdb_output_thr_fn, NULL, 0, &tid);
	if (!thr) {
		terminate_gdb();
		return -1;
	}

	g_debug.gdb_pipe_thread = thr;
	return 0;
}

int
start_debug(int connfd, int port)
{
	int serverfd;

	serverfd = init_sock(port);
	if(serverfd < 0) {
		fprintf(stderr, "Could not bind to %d\n", port);
		return 1;
	}

	listen_sockfd(serverfd, handle_gdb_conn, NULL);
	return 0;
}

int
game_hook(int connfd)
{
	DWORD exit_code;
	int rc;

	GetExitCodeProcess(g_game_handle, &exit_code);
	if (exit_code != STILL_ACTIVE) {
		rc = start_game(connfd, false);
	} else {
		rc = detach_dll(connfd, g_game_procid, g_game_dll);
		rc = rc || inject(connfd, true);
	}

	return rc;
}