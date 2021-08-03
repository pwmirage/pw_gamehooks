/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>

#include "avl.h"
#include "game_config.h"
#include "common.h"

struct game_config {
	FILE *fp;
	struct pw_avl *opts;
} g_config;

static uint32_t
djb2(const char *str) {
	uint32_t hash = 5381;
	unsigned char c;

	while ((c = (unsigned char)*str++)) {
	    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}

int
game_config_parse(const char *filepath)
{
	struct game_config_opt *opt;
	char line[1024];
	char key[1024];
	char val[1024];
	int rc;

	g_config.fp = fopen(filepath, "r+");
	if (!g_config.fp) {
		if (errno == ENOENT) {
			g_config.fp = fopen(filepath, "w+");
		}

		if (!g_config.fp) {
			return -errno;
		}
	}

	g_config.opts = pw_avl_init(sizeof (struct game_config_opt));
	if (!g_config.opts) {
		fclose(g_config.fp);
		return -ENOMEM;
	}

	while (fgets(line, sizeof(line), g_config.fp) != NULL) {
		if (line[0] == '#' || line[0] == 0 || line[0] == '\n' || line[0] == '\r') {
			continue;
		}

		rc = sscanf(line, " %s = %s ", key, val);
		if (rc != 2) {
			continue;
		}

		opt = game_config_set(key, val);
		if (!opt) {
			/* TODO cleanup */
			return -ENOMEM;
		}
	}

	return 0;
}

const char *
game_config_get(const char *key, const char *defval)
{
	struct game_config_opt *opt;
	unsigned hash = djb2(key);

	opt = pw_avl_get(g_config.opts, hash);
	while (opt && strcmp(opt->key, key) != 0) {
		opt = pw_avl_get_next(g_config.opts, opt);
	}
	if (!opt) {
		return defval;
	}
	
	return opt->val;
}

struct game_config_opt *
game_config_set(const char *key, const char *value)
{
	struct game_config_opt *opt;
	unsigned hash = djb2(key);
	bool do_insert = false;

	opt = pw_avl_get(g_config.opts, hash);
	while (opt && strcmp(opt->key, key) != 0) {
		opt = pw_avl_get_next(g_config.opts, opt);
	}

	if (!opt) {
		opt = pw_avl_alloc(g_config.opts);
		if (!opt) {
			return NULL;
		}

		snprintf(opt->key, sizeof(opt->key), "%s", key);
		do_insert = true;
	}

	opt->val = strdup(value);
	if (!opt->val) {
		free(opt);
		return NULL;
	}

	if (do_insert) {
		pw_avl_insert(g_config.opts, hash, opt);
	}

	return opt;
}

void
save_cb(void *el, void *ctx1, void *ctx2)
{
	FILE *fp = ctx1;
	struct pw_avl_node *node = el;
	struct game_config_opt *opt = (void *)node->data;

	fprintf(fp, "%s = %s\n", opt->key, opt->val);
}

void
game_config_save(bool close)
{
	fseek(g_config.fp, 0, SEEK_SET);
	fprintf(g_config.fp, "# PW Mirage Game settings\n");
	fprintf(g_config.fp, "# key = value\n");
	fprintf(g_config.fp, "# All following and trailing spaces are trimmed\n");
	fprintf(g_config.fp, "\n");

	pw_avl_foreach(g_config.opts, save_cb, g_config.fp, NULL);

	fflush(g_config.fp);
	ftruncate(fileno(g_config.fp), ftell(g_config.fp));

	if (close) {
		fclose(g_config.fp);
	}
}

void
dump_cb(void *el, void *ctx1, void *ctx2)
{
	char **buf = ctx1;
	size_t *buf_remaining = ctx2;

	struct pw_avl_node *node = el;
	struct game_config_opt *opt = (void *)node->data;

	size_t bytes = snprintf(*buf, *buf_remaining, "    %s = %s\r\n", opt->key, opt->val);
	*buf_remaining -= bytes;
	*buf += bytes;
}

size_t
game_config_dump(char *buf, size_t bufsize)
{
	char *buf_org = buf;
	pw_avl_foreach(g_config.opts, dump_cb, &buf, &bufsize);
	return buf - buf_org;
}
