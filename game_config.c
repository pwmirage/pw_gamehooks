/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021-2022 Darek Stojaczyk for pwmirage.com
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "game_config.h"

struct cached_field {
	char key[128];
	char val[128];
	struct cached_field *next;
};

struct config_line {
	char str[128];
	struct config_line *next;
};

struct game_config {
	char filename[64];
	char profile[64];
	struct cached_field *cached;
	struct config_line *file_lines;
} g_config = {
	.profile = "Default",
};

int
game_config_set_file(const char *filepath)
{
	snprintf(g_config.filename, sizeof(g_config.filename), "%s", filepath);
}

/** strip preceeding and following quotes and whitespaces */
static char *
cleanup_str(char *str, const char *chars_to_remove)
{
	int len = strlen(str);
	char *end = str + len - 1;

	while (*str) {
		if (*str == '\\' && *(str + 1) == '"') {
			str += 2;
			continue;
		} else if (strchr(chars_to_remove, *str)) {
			*str = 0;
			str++;
			continue;
		}
		break;
	}

	while (end > str) {
		if (*(end - 1) == '\\' && *end == '"') {
			end -= 2;
			continue;
		} else if (strchr(chars_to_remove, *end)) {
			*end = 0;
			end--;
			continue;
		}
		break;
	}

	return str;
}

int
game_config_set_profile(const char *profile)
{
	snprintf(g_config.profile, sizeof(g_config.profile), "%s", profile);
}

int
game_config_parse(game_config_fn fn, void *fn_ctx)
{
	FILE *fp;
	char line[1024];
	char tmp[64], tmp2[64];
	char *l;
	int rc;
	bool do_skip = false;

	assert(g_config.filename[0] != 0);

	fp = fopen(g_config.filename, "r");
	if (!fp) {
		return -errno;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		l = cleanup_str(line, " \t\r\n");

		if (l[0] == '#' || l[0] == 0) {
			continue;
		}

		rc = sscanf(l, "if [ %63s == %63s ]; then", tmp, tmp2);
		if (rc == 2) {
			char *var1 = cleanup_str(tmp, " \t\"'");
			char *var2 = cleanup_str(tmp2, " \t\"'");

			if (strcmp(var1, "$PROFILE") == 0 && strcmp(var2, g_config.profile) == 0) {
				/* go ahead */
			} else {
				do_skip = true;
			}

			continue;
		} else if (strcmp(l, "fi") == 0) {
			do_skip = false;
			continue;
		}

		if (!do_skip) {
			fn(l, fn_ctx);
		}
	}

	fclose(fp);
	return 0;
}

static int
write_new(void)
{
	FILE *fp;
	int i, rc = 0;

	assert(g_config.filename[0] != 0);
	fp = fopen(g_config.filename, "w");
	if (!fp) {
		return -errno;
	}

	#define GC_W(S) fputs(S "\r\n", fp)
	GC_W("# PW Mirage Client Config");
	GC_W("# This is a list of configuration commands. The same commands can be typed");
	GC_W("# into the built-in console inside the game (Shift + ~). Putting them here");
	GC_W("# simply makes them execute at launch.");
	GC_W("#");
	GC_W("# There's multiple variances (profiles) of game configuration in this file.");
	GC_W("# The game starts with any profile selected from the launcher or provided");
	GC_W("# via game.exe parameters (if launched directly):");
	GC_W("# \"C:\\PWMirage\\element\\game.exe\" --profile Secondary");
	GC_W("# or");
	GC_W("# \"C:\\PWMirage\\element\\game.exe\" -p Secondary");
	GC_W("#");
	GC_W("# The syntax is a very basic subset of Shell. See pwmirage.com/launcher");
	GC_W("# for full documentation.");
	GC_W("#");
	GC_W("# Changing ingame settings will modify this file. The conflicting commands");
	GC_W("# may be updated, or new overriding rules might be added to the end of file.");
	GC_W("");

	fclose(fp);
	return 0;
}

static int
read_file_for_updating(void)
{
	FILE *fp;
	struct config_line **next_p;
	struct config_line *line;
	char *c, *l;
	int len;
	char *buf;
	bool do_skip = false;

	fp = fopen(g_config.filename, "r");
	if (!fp) {
		return -errno;
	}

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);

	fseek(fp, 0, SEEK_SET);
	buf = malloc(len + 1);
	if (!buf) {
		fclose(fp);
		return -errno;
	}

	fread(buf, 1, len, fp);
	buf[len] = 0;

	l = c = buf;
	next_p = &g_config.file_lines;
	while (*c) {
		if (*c == '\r') {
			*c = 0;
		} else if (*c == '\n') {
			*c = 0;

			line = calloc(1, sizeof(*line));
			assert(line);
			l = cleanup_str(l, " \t");
			snprintf(line->str, sizeof(line->str), "%s", l);

			*next_p = line;
			next_p = &line->next;

			l = c + 1;
		}

		c++;
	}

	/* add remainder if any, OR if the file is empty so there's at least 1 rule */
	if (l != c || g_config.file_lines == NULL) {
		line = calloc(1, sizeof(*line));
		assert(line);
		l = cleanup_str(l, " \t");
		snprintf(line->str, sizeof(line->str), "%s", l);
		*next_p = line;
		next_p = &line->next;
	}

	fclose(fp);
	free(buf);

	return 0;
}

static void
free_file_for_updating(void)
{
	struct config_line *tmp, *line = g_config.file_lines;

	while (line) {
		tmp = line->next;
		free(line);
		line = tmp;
	}

	g_config.file_lines = NULL;
}

static void
set_cmd(const char *key, const char *val)
{
	struct config_line *prev = NULL, *line;
	int keylen = strlen(key);
	char tmp[64], tmp2[64];
	int rc;
	bool do_skip = false;

	assert(g_config.file_lines);

	line = g_config.file_lines;
	while (line) {
		rc = sscanf(line->str, "if [ %63s == %63s ]; then", tmp, tmp2);
		if (rc == 2) {
			/* don't override variables in ifs, just append overriding commands to the end */
			do_skip = true;
		} else if (strcmp(line->str, "fi") == 0) {
			do_skip = false;
		} else if (!do_skip && strncmp(line->str, key, keylen) == 0) {
			snprintf(line->str, sizeof(line->str), "%s %s", key, val);
			return;
		}

		prev = line;
		line = line->next;
	}

	prev->next = calloc(1, sizeof(*prev->next));
	assert(prev->next);

	snprintf(prev->next->str, sizeof(prev->next->str), "%s %s", key, val);
	prev->next->next = line;
}

static int
config_flush(void)
{
	struct cached_field *cache, *tmpcache;
	struct config_line *line;
	FILE *fp;
	char tmp[64], tmp2[64];
	bool do_indent = false;
	int rc;

	rc = read_file_for_updating();
	if (rc == -ENOENT) {
		rc = write_new();
		if (rc) {
			return rc;
		}

		rc = read_file_for_updating();
	}

	if (rc) {
		return rc;
	}

	cache = g_config.cached;
	while (cache) {
		tmpcache = cache->next;
		set_cmd(cache->key, cache->val);
		free(cache);
		cache = tmpcache;
	}
	g_config.cached = NULL;

	fp = fopen(g_config.filename, "w");
	if (!fp) {
		return -errno;
	}

	line = g_config.file_lines;
	while (line) {
		char c;

		if (strcmp(line->str, "fi") == 0) {
			do_indent = false;
		}

		fprintf(fp, "%s%s\r\n", do_indent ? "\t" : "", line->str);

		rc = sscanf(line->str, "if [ %63s == %63s ]; then", tmp, tmp2);
		if (rc == 2) {
			do_indent = true;
		}

		line = line->next;
	}


	fclose(fp);
	free_file_for_updating();
	return 0;
}

int
game_config_save_s(const char *key, const char *val, bool flush)
{
	FILE *fp;
	struct cached_field **last_p = &g_config.cached;
	struct cached_field *cache;

	assert(g_config.filename[0] != 0);

	if (key == NULL) {
		if (flush) {
			return config_flush();
		}
		return 0;
	}

	cache = *last_p;
	while (cache) {
		if (strcmp(cache->key, key) == 0) {
			snprintf(cache->val, sizeof(cache->val), "%s", val);
			break;
		}
		last_p = &cache->next;
		cache = *last_p;
	}

	if (!cache) {
		cache = calloc(1, sizeof(*cache));
		assert(cache != NULL);

		snprintf(cache->key, sizeof(cache->key), "%s", key);
		snprintf(cache->val, sizeof(cache->val), "%s", val);

		*last_p = cache;
	}

	if (!flush) {
		return 0;
	}

	return config_flush();
}

int
game_config_save_i(const char *key, int64_t val, bool flush)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%"PRIu64, val);

	return game_config_save_s(key, buf, flush);
}

int
game_config_save_f(const char *key, float val, bool flush)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.6f", val);

	return game_config_save_s(key, buf, flush);
}

#ifdef GAME_CONFIG_TEST
static void
print_fn(const char *cmd, void *ctx)
{
	fprintf(stderr, "%s\n", cmd);
}

int
main(void)
{
    game_config_set_file("test1.cfg");

    game_config_save_s("set d3d8", "0", true);
    game_config_save_s("set x", "768", false);
    game_config_save_s("set show_mp_bar", "1", false);
    game_config_save_s("set x2", "1111", true);

	game_config_parse("Secondary", print_fn, NULL);
}
#endif