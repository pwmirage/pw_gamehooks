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

struct game_config_category;

struct game_config {
	FILE *fp;
	bool init;
	struct game_config_category *last_cat;
	int max_category_order;
	struct pw_avl *categories;
} g_config;

struct game_config_opt {
	struct game_config_category *category;
	char key[128];
	bool valid;
	int order;
	char *strval;
	int intval;
};

struct game_config_category {
	char name[32];
	int max_opts;
	int order;
	struct pw_avl *opts;
	struct game_config_opt **opts_arr;
};

static uint32_t
djb2(const char *str) {
	uint32_t hash = 5381;
	unsigned char c;

	while ((c = (unsigned char)*str++)) {
	    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}

struct game_config_category *
_config_get_category(const char *name)
{
	struct game_config_category *cat;
	unsigned hash = djb2(name);

	cat = pw_avl_get(g_config.categories, hash);
	while (cat && strcmp(cat->name, name) != 0) {
		cat = pw_avl_get_next(g_config.categories, cat);
	}
	return cat;
}

static struct game_config_category *
_config_set_category(const char *name)
{
	struct game_config_category *cat;
	unsigned hash = djb2(name);
	bool do_insert = false;

	cat = pw_avl_get(g_config.categories, hash);
	while (cat && strcmp(cat->name, name) != 0) {
		cat = pw_avl_get_next(g_config.categories, cat);
	}

	if (!cat) {
		if (g_config.init && g_config.last_cat) {
			game_config_set_invalid(g_config.last_cat->name, "");
		}

		cat = pw_avl_alloc(g_config.categories);
		if (!cat) {
			return NULL;
		}

		snprintf(cat->name, sizeof(cat->name), "%s", name);
		cat->opts = pw_avl_init(sizeof(struct game_config_opt));
		if (!cat->opts) {
			pw_avl_free(g_config.categories, cat);
			return NULL;
		}
		cat->order = g_config.max_category_order++;
		g_config.last_cat = cat;
		do_insert = true;
	}

	if (do_insert) {
		pw_avl_insert(g_config.categories, hash, cat);
	}

	return cat;
}

static struct game_config_opt *
_config_get(const char *category, const char *key)
{
	struct game_config_category *cat;
	struct game_config_opt *opt;

	cat = _config_get_category(category);
	if (!cat) {
		return NULL;
	}

	unsigned hash = djb2(key);
	opt = pw_avl_get(cat->opts, hash);
	while (opt && strcmp(opt->key, key) != 0) {
		opt = pw_avl_get_next(cat->opts, opt);
	}
	return opt;
}

const char *
game_config_get_str(const char *category,
		const char *key, const char *defval)
{
	struct game_config_opt *opt;

	opt = _config_get(category, key);
	if (!opt) {
		return defval;
	}
	return opt->strval;
}

int
game_config_get_int(const char *category, const char *key, int defval)
{
	struct game_config_opt *opt;

	opt = _config_get(category, key);
	if (!opt) {
		return defval;
	}
	return opt->intval;
}

static struct game_config_opt *
_config_set(struct game_config_category *cat, const char *key)
{
	struct game_config_opt *opt;
	unsigned hash;
	bool do_insert = false;

	hash = djb2(key);
	opt = pw_avl_get(cat->opts, hash);
	while (opt && strcmp(opt->key, key) != 0) {
		opt = pw_avl_get_next(cat->opts, opt);
	}

	if (!opt) {
		opt = pw_avl_alloc(cat->opts);
		if (!opt) {
			return NULL;
		}

		opt->category = cat;
		opt->order = cat->max_opts++;
		snprintf(opt->key, sizeof(opt->key), "%s", key);
		do_insert = true;
	}

	if (do_insert) {
		pw_avl_insert(cat->opts, hash, opt);
	}

	return opt;
}

int
game_config_set_str(const char *category, const char *key, const char *value)
{
	struct game_config_category *cat;
	struct game_config_opt *opt;

	cat = _config_set_category(category);
	if (!cat) {
		return -ENOMEM;
	}

	opt = _config_set(cat, key);
	if (!opt) {
		return -ENOMEM;
	}

	opt->strval = strdup(value);
	if (!opt->strval) {
		pw_avl_remove(cat->opts, opt);
		pw_avl_free(cat->opts, opt);
		return -ENOMEM;
	}

	opt->valid = true;
	return 0;
}

int
game_config_set_int(const char *category, const char *key, int value)
{
	struct game_config_category *cat;
	struct game_config_opt *opt;

	cat = _config_set_category(category);
	if (!cat) {
		return -ENOMEM;
	}

	opt = _config_set(cat, key);
	if (!opt) {
		return -ENOMEM;
	}

	opt->intval = value;
	opt->valid = true;
	return 0;
}

int
game_config_set_invalid(const char *category, const char *val)
{
	struct game_config_category *cat;
	struct game_config_opt *opt;
	unsigned hash = djb2("#");

	cat = _config_set_category(category);
	if (!cat) {
		return -ENOMEM;
	}

	opt = pw_avl_alloc(cat->opts);
	if (!opt) {
		return -ENOMEM;
	}

	opt->category = cat;
	opt->key[0] = '#';

	opt->strval = strdup(val);
	if (!opt->strval) {
		pw_avl_free(cat->opts, opt);
		return -ENOMEM;
	}

	opt->order = cat->max_opts++;
	pw_avl_insert(cat->opts, hash, opt);
	opt->valid = false;
	return 0;
}

void
strip_newlines(char *str)
{
	while (*str) {
		if (*str == '\r' || *str == '\n') {
			*str = 0;
			break;
		}
		str++;
	}
}

int
game_config_parse(const char *filepath)
{
	char line[1024];
	char category[1042] = "Global";
	char key[1024];
	char *tmp, *val = NULL;
	bool do_parse = false;
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

	g_config.categories = pw_avl_init(sizeof(struct game_config_opt));
	if (!g_config.categories) {
		fclose(g_config.fp);
		return -ENOMEM;
	}

	while (fgets(line, sizeof(line), g_config.fp) != NULL) {
		int p = 0;
		char c = 0;

		if (line[0] == '#' || line[0] == 0 || line[0] == '\n' || line[0] == '\r') {
			if (do_parse) {
				strip_newlines(line);
				game_config_set_invalid(category, line);
			}
			continue;
		}

		do_parse = true;

		rc = sscanf(line, " [ %[^]] %c ", key, &c);
		if (rc == 2 && c == ']') {
			snprintf(category, sizeof(category), "%s", key);
			_config_set_category(category);
			continue;
		}

		rc = sscanf(line, " %[^=] = %n ", key, &p);
		if (rc < 1 || p == 0) {
			if (do_parse) {
				strip_newlines(line);
				game_config_set_invalid(category, line);
			}
			continue;
		}

		/* trim key */
		tmp = key + strlen(key) - 1;
		while (*tmp == ' ') {
			*tmp = 0;
			tmp--;
		}

		val = line + p;
		if (*val == '"') {
			/* strip preceeding and following quotes */
			val += 1;
			tmp = val;
			while (*tmp) {
				if (*tmp == '\\' && *(tmp + 1) == '"') {
					tmp += 2;
					continue;
				} else if (*tmp == '"' || *tmp == '\r' || *tmp == '\n') {
					*tmp = 0;
					break;
				}
				tmp++;
			}
			rc = game_config_set_str(category, key, val);
		} else {
			int intval = atoi(val);

			rc = game_config_set_int(category, key, intval);
		}
		if (rc) {
			/* TODO cleanup */
			return rc;
		}
	}

	g_config.init = true;
	return 0;
}

void
sort_cb(void *el, void *ctx1, void *ctx2)
{
	struct game_config_category *cat = ctx1;
	struct pw_avl_node *node = el;
	struct game_config_opt *opt = (void *)node->data;

//	assert(opt->order < cat->max_opts);
	cat->opts_arr[opt->order] = opt;
}

void
category_sort_cb(void *el, void *ctx1, void *ctx2)
{
	struct game_config_category **cats = ctx1;
	struct pw_avl_node *node = el;
	struct game_config_category *cat = (void *)node->data;
	int *rc = ctx2;

//	assert(cat->order < g_config.max_category_order);
	cats[cat->order] = cat;

	cat->opts_arr = calloc(cat->max_opts, sizeof(*cat->opts_arr));
	if (!cat->opts_arr) {
		*rc = -ENOMEM;
		return;
	}
}

void
save_category(struct game_config_category *cat)
{
	int i;

	fprintf(g_config.fp, "[%s]\n", cat->name);

	pw_avl_foreach(cat->opts, sort_cb, cat, NULL);

	for (i = 0; i < cat->max_opts; i++) {
		struct game_config_opt *opt = cat->opts_arr[i];

		if (!opt) {
			continue;
		} else if (!opt->valid) {
			fprintf(g_config.fp, "%s\n", opt->strval);
		} else if (opt->strval) {
			fprintf(g_config.fp, "%s = \"%s\"\n", opt->key, opt->strval);
		} else {
			fprintf(g_config.fp, "%s = %d\n", opt->key, opt->intval);
		}
	}
}

void
game_config_save(bool close)
{
	struct game_config_category **cats;
	int i, rc = 0;

	if (!g_config.fp) {
		return;
	}

	cats = calloc(g_config.max_category_order, sizeof(*cats));
	if (!cats) {
		/* we just can't update the config, too bad */
		return;
	}

	pw_avl_foreach(g_config.categories, category_sort_cb, cats, &rc);
	if (rc) {
		/* ^ */
		return;
	}

	fseek(g_config.fp, 0, SEEK_SET);
	fprintf(g_config.fp, "# PW Game settings v1.1\n");
	fprintf(g_config.fp, "\n");

	for (i = 0; i < g_config.max_category_order; i++) {
		if (!cats[i]) {
			continue;
		}
		save_category(cats[i]);
	}

	fflush(g_config.fp);
	ftruncate(fileno(g_config.fp), ftell(g_config.fp));
	if (close) {
		fclose(g_config.fp);
	}
}

