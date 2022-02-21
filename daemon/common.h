/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_DAEMON_COMMON
#define PW_DAEMON_COMMON

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <assert.h>
#include <windows.h>
#include <winbase.h>
#include <winsock2.h>
#include <tlhelp32.h>
#include <errno.h>

#define MASTER_PORT 61171
#define LOCAL_IPC_PORT 61170

#ifndef ETOOMANYREFS
#define	ETOOMANYREFS	109	/* Too many references: cannot splice */
#endif

typedef int (*listen_sockfd_fn)(int sockfd, void *ctx);
struct listen_sockfd_entry {
	int fd;
	listen_sockfd_fn fn;
	void *ctx;
};

struct ipc_init_msg {
	listen_sockfd_fn fn;
	void *ctx;
};

extern char g_hookdir[1024];

int listen_sockfd(int sockfd, listen_sockfd_fn msg_fn, void *ctx);
void silence_sockfd(int sockfd);

void conn_echo(int connfd, const char *fmt, ...);
int conn_exec_unsafe(int connfd, const char *cmd);

int init_sock(int port);
int start_ipc(listen_sockfd_fn fn, void *ctx);

int game_hook(int connfd);
int start_debug(int connfd, int port);

#endif