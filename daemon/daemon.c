/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2022 Darek Stojaczyk for pwmirage.com
 */

#include "common.h"

#define MAX_SOCKETS 16
static struct listen_sockfd_entry g_conn_sockfds[MAX_SOCKETS];
static int g_ipc_socket = -1;

void
conn_echo(int connfd, const char *fmt, ...)
{
	char buf[2048];
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	send(connfd, buf, rc, 0);
}

int
conn_exec_unsafe(int connfd, const char *cmd)
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
		send(connfd, path, strlen(path), 0);
	}

	/* close */
	return pclose(fp);
}

static int
handle_conn_sockfd(int connfd, void *ctx)
{
	char buf[1024];
	char tmpbuf[1024];
	char *argv[64] = {};
	char *c;
	int argc;
	int rc;
	DWORD exit_code = -1;
	bool need_new_word = false;

	rc = recv(connfd, buf, sizeof(buf) - 1, 0);
	if (rc <= 0) {
		fprintf(stderr, "master conn closed\n");
		return -1;
	}

	if (rc > 0 && buf[rc - 1] == '\n') {
		buf[rc - 1] = 0;
	}
	buf[rc] = 0;

	printf("cmd: %s\n", buf);
	strncpy(tmpbuf, buf, sizeof(tmpbuf));

	c = tmpbuf;
	argc = 0;
	argv[argc++] = c;

	while (*c) {
		if (*c == ' ' || *c == '\t') {
			*c = 0;
			need_new_word = true;
			c++;
			continue;
		}

		if (need_new_word) {
			argv[argc++] = c;
			need_new_word = false;
		}


		c++;
	}

	/* TODO API for registering handlers? */

	if (strcmp(argv[0], "hook") == 0) {
		rc = game_hook(connfd);
	} else if (strcmp(argv[0], "startdebug") == 0) {
		if (!argv[1]) {
			fprintf(stderr, "master conn closed\n");
			return -1;
		}
		rc = start_debug(connfd, atoi(argv[1]));
	} else {
		rc = conn_exec_unsafe(connfd, buf);
	}

	/* FIXME? for now we close the connection after every message */
	return 1;
}

int
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
listen_sockfd(int sockfd, listen_sockfd_fn msg_fn, void *ctx)
{
	struct listen_sockfd_entry *entry;

	assert(sockfd != -1);

	for (int i = 0; i < MAX_SOCKETS; i++) {
		entry = &g_conn_sockfds[i];
		if (entry->fd != -1) {
			continue;
		}

		entry->fd = sockfd;
		entry->fn = msg_fn;
		entry->ctx = ctx;
		return 0;
	}

	return -ETOOMANYREFS;
}

void
silence_sockfd(int sockfd)
{
	struct listen_sockfd_entry *entry;

	for (int i = 0; i < MAX_SOCKETS; i++) {
		entry = &g_conn_sockfds[i];
		if (entry->fd != -1) {
			continue;
		}

		entry->fd = -1;
		break;
	}
}

static int
handle_master_sockfd(int sockfd, void *ctx)
{
	struct sockaddr_in cli = {};
	int len = sizeof(cli);

	int connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
	if (connfd < 0) {
		perror("acccept failed");
		return 0;
	}

	fprintf(stderr, "new master conn\n");

	listen_sockfd(connfd, handle_conn_sockfd, NULL);

	return 0;
}

int
start_ipc(listen_sockfd_fn fn, void *ctx)
{
	struct sockaddr_in servaddr = {};
	struct ipc_init_msg init_msg;
	int connfd, rc;

	init_msg.fn = fn;
	init_msg.ctx = ctx;

	connfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(connfd == -1) {
		perror("ipc: socket");
		return 1;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	servaddr.sin_port = htons(LOCAL_IPC_PORT);

	rc = connect(connfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if (rc == SOCKET_ERROR) {
		perror("ipc: connect");
		closesocket(connfd);
		return 1;
	}

	send(connfd, (void *)&init_msg, sizeof(init_msg), 0);

	return connfd;
}

static int
handle_ipc_sockfd(int sockfd, void *ctx)
{
	struct sockaddr_in cli = {};
	int len = sizeof(cli);
	struct ipc_init_msg init_msg;
	char buf[64];
	int rc;

	int connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
	if (connfd < 0) {
		perror("ipc: acccept failed");
		return 0;
	}

	rc = recv(connfd, (void *)&init_msg, sizeof(init_msg), 0);
	if (rc <= 0) {
		fprintf(stderr, "ipc: recv failed: %d\n", errno);
		return 0;
	}

	if (rc != sizeof(init_msg)) {
		fprintf(stderr, "ipc: invalid init msg size (got %d, expected %d)\n",
				rc, sizeof(init_msg));
		return 0;
	}

	assert(init_msg.fn);
	listen_sockfd(connfd, init_msg.fn, init_msg.ctx);
	return 0;
}

static int
init_ipc(void)
{
	struct sockaddr_in servaddr = {};
	int sockfd, ipc_serverfd;
	int rc;

	/* create a server and listen on it */
	ipc_serverfd = init_sock(LOCAL_IPC_PORT);
	if(ipc_serverfd < 0) {
		return -ipc_serverfd;
	}
	listen_sockfd(ipc_serverfd, handle_ipc_sockfd, NULL);

	g_ipc_socket = sockfd;
	return 0;
}

int
main()
{
	int master_sockfd, len;
	struct sockaddr_in servaddr = {}, cli = {};
    fd_set readfds;
	WORD versionWanted = MAKEWORD(1, 1);
	WSADATA wsaData;
	int rc;

	WSAStartup(versionWanted, &wsaData);

	GetCurrentDirectory(sizeof(g_hookdir), g_hookdir);
	fprintf(stderr, "Hook dir: \"%s\"\n", g_hookdir);

    for (int i = 0; i < MAX_SOCKETS; i++) {
		struct listen_sockfd_entry *entry = &g_conn_sockfds[i];
		entry->fd = -1;
	}

	master_sockfd = init_sock(MASTER_PORT);
	if (master_sockfd < 0) {
		return -master_sockfd;
	}
	listen_sockfd(master_sockfd, handle_master_sockfd, NULL);
	fprintf(stderr, "Server started at %d\n", MASTER_PORT);

	init_ipc();

	while (1) {
        FD_ZERO(&readfds);

        for (int i = 0; i < MAX_SOCKETS; i++) {
			struct listen_sockfd_entry *entry = &g_conn_sockfds[i];
            if (entry->fd > 0) {
                FD_SET(entry->fd, &readfds);
            }
        }

        rc = select(0, &readfds, NULL, NULL, NULL);
        if (rc == SOCKET_ERROR) {
            perror("select");
			return rc;
        }

        for (int i = 0; i < MAX_SOCKETS; i++) {
			struct listen_sockfd_entry *entry = &g_conn_sockfds[i];

            if (!FD_ISSET(entry->fd , &readfds)) {
				continue;
			}

			rc = entry->fn(entry->fd, entry->ctx);
			if (rc) {
				fprintf(stderr, "closing %d\n", entry->fd);
				closesocket(entry->fd);
				entry->fd = -1;
				if (i == 0 || i == 1) {
					/* master socket went down */
					return -1;
				}
			}
		}
	}

	return 0;
}
