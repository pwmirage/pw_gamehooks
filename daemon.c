/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <windows.h>
#include <winbase.h>
#include <winsock2.h>
#include <errno.h>

static int g_connfd;
static DWORD g_game_procid;
static HANDLE g_game_handle;
static HMODULE g_game_dll;
static HANDLE g_debug_stdout_rd = NULL;
static HANDLE g_debug_stdout_wr = NULL;
static HANDLE g_debug_stdin_rd = NULL;
static HANDLE g_debug_stdin_wr = NULL;
static bool g_is_debugging;

static char g_hookdir[1024];
static unsigned g_hookcount;

#ifndef PW_GAMEDIR
#error PW_GAMEDIR not defined
#endif

#define MASTER_PORT 61171
#define GDB_PORT 61172

static int init_sock(int port);

static void
echo(const char *fmt, ...)
{
	char buf[2048];
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	send(g_connfd, buf, rc, 0);
}

static int
exec_unsafe(const char *cmd)
{
	FILE *fp;
	char path[1048];

	/* Open the command for reading. */
	snprintf(path, sizeof(path), "%s 2>&1", cmd);
	fp = popen(path, "r");
	if (fp == NULL) {
		perror("");
		exit(1);
	}

	/* Read the output a line at a time - output it. */
	while (fgets(path, sizeof(path), fp) != NULL) {
		send(g_connfd, path, strlen(path), 0);
	}

	/* close */
	return pclose(fp);
}

static HMODULE
inject_dll(DWORD pid, char *path_to_dll)
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

	echo("$ inject gamehook.dll\n");

	CloseHandle(thr);
	return injected_dll;

err_free:
	VirtualFreeEx(g_game_handle, ext_path_to_dll, 0, MEM_RELEASE);
err:
	echo("Failed to open PW process. Was it closed prematurely?\n");
	return NULL;
}

static int
detach_dll(DWORD pid, HMODULE dll)
{
	HANDLE thr;
	char buf[64];
	LPVOID free_lib_winapi_addr;
	HMODULE injected_dll = NULL;
	DWORD thr_state;
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

	CloseHandle(thr);

	echo("$ detach gamehook.dll\n");
	Sleep(150);
	return 0;
}

static int
inject(bool do_echo)
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
		echo("$ game.exe %s\n", dll_path2);
	}
	CopyFile(dll_path, dll_path2, FALSE);
	g_game_dll = inject_dll(g_game_procid, dll_path2);

	return g_game_dll != NULL ? 0 : -1;
}

static int
start_game(void)
{
	STARTUPINFO pw_proc_startup_info = {0};
	PROCESS_INFORMATION pw_proc_info = {0};
	BOOL result;
	int rc;

	SetCurrentDirectory(PW_GAMEDIR);
	pw_proc_startup_info.cb = sizeof(STARTUPINFO);
	result = CreateProcess(NULL, "game.exe --profile=1",
			NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL,
			&pw_proc_startup_info, &pw_proc_info);
	if(!result) {
		echo("Could not start the PW process");
		return -1;
	}

	g_game_procid = pw_proc_info.dwProcessId;
	g_game_handle = OpenProcess(PROCESS_ALL_ACCESS, 0, g_game_procid);
	if (g_game_handle == NULL) {
		echo("Could not start the PW process");
		return -1;
	}

	SetCurrentDirectory(g_hookdir);
	rc = inject(false);
	ResumeThread(pw_proc_info.hThread);

	fprintf(stderr, "Started game.exe with pid %d\n", g_game_procid);
	return rc;
}

static DWORD __stdcall
read_debug_stdout_fn(void *arg)
{
	DWORD read; 
	char buf[4096]; 
	bool ok = true;

	while (ok) { 
		ok = ReadFile(g_debug_stdout_rd, buf, sizeof(buf), &read, NULL);
		if (!ok || read == 0) {
			break;
		}

		//fprintf(stderr, "got %d bytes from gdb: %.*s\n", read, read, buf);

		send(g_connfd, buf, read, 0);
	}

	fprintf(stderr, "gdb closed; terminating conn\n");

	return 0;
}

static int
start_debug(void)
{
	STARTUPINFO startup = {0};
	PROCESS_INFORMATION proc = {0};
	SECURITY_ATTRIBUTES saAttr = {0};
	DWORD tid;
	BOOL ok;
	char buf[4096];
	int rc;
	DWORD written;
	HANDLE prochandle;
	HANDLE thr;

    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

	if (g_is_debugging)	{
		CloseHandle(g_debug_stdin_rd);
		CloseHandle(g_debug_stdin_wr);
		CloseHandle(g_debug_stdout_rd);
		CloseHandle(g_debug_stdout_wr);
		g_is_debugging = false;
	}

	ok = CreatePipe(&g_debug_stdin_rd, &g_debug_stdin_wr, &saAttr, 0);
	ok = ok && SetHandleInformation(g_debug_stdin_wr, HANDLE_FLAG_INHERIT, 0);
	ok = ok && CreatePipe(&g_debug_stdout_rd, &g_debug_stdout_wr, &saAttr, 0);
	ok = ok && SetHandleInformation(g_debug_stdout_rd, HANDLE_FLAG_INHERIT, 0);
	if (!ok) {
		return GetLastError();
	}

	SetCurrentDirectory(PW_GAMEDIR);
	startup.cb = sizeof(startup);
	startup.hStdError = g_debug_stdout_wr;
    startup.hStdOutput = g_debug_stdout_wr;
	startup.hStdInput = g_debug_stdin_rd;
	startup.dwFlags |= STARTF_USESTDHANDLES;

	snprintf(buf, sizeof(buf), "gdb.exe --interpreter=mi2 game.exe --pid=%d",
			g_game_procid);
	ok = CreateProcess(NULL, buf,
			NULL, NULL, TRUE, 0, NULL, NULL,
			&startup, &proc);

	SetCurrentDirectory(g_hookdir);
	if(!ok) {
		echo("Could not start gdb.exe\n");
		return -1;
	}

	fprintf(stderr, "Started gdb.exe with pid %d\n", proc.dwProcessId);

	thr = CreateThread(NULL, 0, read_debug_stdout_fn, NULL, 0, &tid);
	while (1) {
		char *found;

		rc = recv(g_connfd, buf, sizeof(buf), 0);
		if (rc <= 0) {
			break;
		}

		//fprintf(stderr, "read %d bytes: %.*s\n", rc, rc, buf);

		found = strstr(buf, "-exec-interrupt");
		if (found != NULL) {
			DebugBreakProcess(g_game_handle);
		}

		found = strstr(buf, "-file-exec-and-symbols");
		if (found != NULL) {
			snprintf(found, sizeof(buf) - (found - buf), "-file-exec-and-symbols game.exe\n");
			rc = strlen(buf);
		}

		found = strstr(buf, "-environment-cd");
		if (found != NULL) {
			snprintf(found, sizeof(buf) - (found - buf), "-environment-cd .\n");
			rc = strlen(buf);
		}

		found = strstr(buf, "-target-select remote");
		if (found != NULL) {
			DebugBreakProcess(g_game_handle);
			snprintf(found, sizeof(buf) - (found - buf), "-exec-continue\n");
			rc = strlen(buf);
		}

		found = strstr(buf, "-exec-run");
		if (found != NULL) {
			snprintf(found, sizeof(buf) - (found - buf), "-exec-continue\n");
			rc = strlen(buf);
		}

		ok = WriteFile(g_debug_stdin_wr, buf, rc, &written, NULL);
      	if (!ok) {
			  break;
		}

		//fprintf(stderr, "wrote %d bytes\n", written);
	}

	fprintf(stderr, "conn closed; terminating gdb\n");

	prochandle = OpenProcess(PROCESS_TERMINATE, false, proc.dwProcessId);
	TerminateProcess(prochandle, 1);
	CloseHandle(prochandle);

	TerminateThread(thr, 0);
	CloseHandle(thr);

	CloseHandle(g_debug_stdin_rd);
	CloseHandle(g_debug_stdin_wr);
	/* this will also terminate the thread */
	CloseHandle(g_debug_stdout_rd);
	CloseHandle(g_debug_stdout_wr);
	g_is_debugging = false;

	return 0;
}

static int
handle_conn(void)
{
	char buf[1024];
	int rc;
	DWORD exit_code = -1;

	rc = recv(g_connfd, buf, sizeof(buf), 0);
	if (rc <= 0) {
		return -1;
	}

	if (rc > 0 && buf[rc - 1] == '\n') {
		buf[rc - 1] = 0;
	}
	buf[rc] = 0;
	printf("cmd: %s\n", buf);

	if (strchr(buf, ';') != NULL) {
		echo("Error: found invalid character - \";\"");
		return -1;
	} else if (strncmp(buf, "gcc ", 4) == 0) {
		return exec_unsafe(buf);
	} else if (strncmp(buf, "hook", 4) == 0) {
		GetExitCodeProcess(g_game_handle, &exit_code);
		if (exit_code != STILL_ACTIVE) {
			rc = start_game();
		} else {
			rc = detach_dll(g_game_procid, g_game_dll);
			rc = rc || inject(true);
		}
	} else if (strncmp(buf, "startdebug", 4) == 0) {
		GetExitCodeProcess(g_game_handle, &exit_code);
		if (exit_code != STILL_ACTIVE) {
			rc = start_game();
			if (rc != 0) {
				return rc;
			}
			Sleep(4000);
		}
		rc = start_debug();
	}
	return rc;
}

static int
init_sock(int port)
{
	int sockfd, len;
	struct sockaddr_in servaddr = {};

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd == -1) {
		perror("socket failed");
		return -errno;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);

	if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
		perror("bind failed");
		return -errno;
	}

	if ((listen(sockfd, 5)) != 0) {
		perror("listen failed");
		return -errno;
	}

	return sockfd;
}

int
main()
{
	int sockfd, len;
	struct sockaddr_in servaddr = {}, cli = {};

	WORD versionWanted = MAKEWORD(1, 1);
	WSADATA wsaData;
	WSAStartup(versionWanted, &wsaData);

	GetCurrentDirectory(sizeof(g_hookdir), g_hookdir);

	sockfd = init_sock(MASTER_PORT);
	if (sockfd < 0) {
		return -sockfd;
	}

	while (1) {
		len = sizeof(cli);
		g_connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
		if (g_connfd < 0) {
			perror("acccept failed");
			return errno;
		}

		handle_conn();
		closesocket(g_connfd);
	}

	closesocket(sockfd);
	return 0;
}
