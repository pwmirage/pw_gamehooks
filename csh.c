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
#include <pthread.h>

#include "csh.h"
#include "csh_config.h"
#include "avl.h"
#include "pw_api.h"

static uint32_t
djb2(const char *str)
{
	uint32_t hash = 5381;
	unsigned char c;

	while ((c = (unsigned char)*str++)) {
	    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}

static int
write_new(void)
{
	FILE *fp;
	int i, rc = 0;

	assert(g_csh_cfg.filename[0] != 0);
	fp = fopen(g_csh_cfg.filename, "w");
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
    GC_W("set d3d8 0");
    GC_W("set render_nofocus 1");
    GC_W("set show_mp_bar 0");
    GC_W("set show_hp_bar 0");
    GC_W("");
    GC_W("if [ \"$PROFILE\" == \"Secondary\" ]; then");
    GC_W("\tset x 10");
    GC_W("\tset y 10");
    GC_W("\tset width 1297");
    GC_W("\tset height 731");
    GC_W("\tset fullscreen 0");
    GC_W("fi");
    GC_W("");
    GC_W("if [ \"$PROFILE\" == \"Tertiary\" ]; then");
    GC_W("\tset x 960");
    GC_W("\tset y 0");
    GC_W("\tset width 960");
    GC_W("\tset height 1040");
    GC_W("\tset fullscreen 1");
    GC_W("fi");
    #undef GC_W

	fclose(fp);
	return 0;
}

enum csh_var_type {
	CSH_T_NONE,
	CSH_T_STRING,
	CSH_T_DYN_STRING,
	CSH_T_INT,
	CSH_T_BOOL,
	CSH_T_DOUBLE,
};

struct csh_var {
	char key[64];
	enum csh_var_type type;
	union {
		char **dyn_s;
		struct {
			char *buf;
			int len;
		} s;
		int *i;
		bool *b;
		double *d;
	};
	csh_set_cb_fn cb_fn;
	union {
		const char *s;
		int i;
		bool b;
		double d;
	} def_val;
	bool initialized;
};

struct csh_cmd {
	char prefix[64];
	uint32_t prefix_hash;
	csh_cmd_handler_fn fn;
	void *ctx;
	struct csh_cmd *next;
};

static struct pw_avl *g_var_avl;
static struct csh_cmd *g_cmds;
static char g_profile[64];
static pthread_mutex_t g_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static bool g_static_init_done;

static inline void
lock(void)
{
	if (g_static_init_done) {
		pthread_mutex_lock(&g_mutex);
	}
}

static inline void
unlock(void)
{
	if (g_static_init_done) {
		pthread_mutex_unlock(&g_mutex);
	}
}

static const char *
cmd_profile_fn(const char *val, void *ctx)
{
	if (*val == 0) {
		return g_profile;
	}

	snprintf(g_profile, sizeof(g_profile), "%s", val);
	return "";
}

static struct csh_var *
get_var(const char *key)
{
	struct csh_var *var;
	uint32_t hash = djb2(key);

	var = pw_avl_get(g_var_avl, hash);
	while (var && strcmp(var->key, key) != 0) {
		var = pw_avl_get_next(g_var_avl, var);
	}

	if (var && !var->initialized) {
		return NULL;
	}

	return var;
}

static const char *
cmd_show_var_fn(const char *key, void *ctx)
{
	const char *ret;
	int rc;

	ret = csh_get(key);
	if (ret == NULL) {
		return "^ff0000Unknown variable";
	}

	return ret;
}

static const char *
cmd_set_var_fn(const char *cmd, void *ctx)
{
	char key[64];
	char val[64];
	int rc;

	rc = sscanf(cmd, "%s %s", key, val);
	if (rc != 2) {
		return "^ff0000Usage: set <varname> <value>";
	}

	rc = csh_set(key, val);
	if (rc == -ENOENT) {
		return "^ff0000Unknown variable.";
	}

	return "";
}

static void
set_var_val(struct csh_var *var, const char *val)
{
	switch (var->type) {
		case CSH_T_NONE:
			assert(false);
			break;
		case CSH_T_STRING:
			snprintf(var->s.buf, var->s.len, "%s", val);
			break;
		case CSH_T_DYN_STRING:
			free(*var->dyn_s);
			*var->dyn_s = strdup(val);
			assert(*var->dyn_s);
			break;
		case CSH_T_INT:
			*var->i = strtoll(val, NULL, 0);
			break;
		case CSH_T_BOOL:
			if (strcmp(val, "false") == 0) {
				*var->b = false;
			} else if (strcmp(val, "true") == 0) {
				*var->b = true;
			} else {
				*var->b = strtoll(val, NULL, 0);
			}
			break;
		case CSH_T_DOUBLE:
			*var->d = strtod(val, NULL);
			break;
	}
}

int
csh_set(const char *key, const char *val)
{
	struct csh_var *var;

	lock();
	var = get_var(key);
	if (!var) {
		unlock();
		return -ENOENT;
	}

	set_var_val(var, val);

	if (var->cb_fn) {
		var->cb_fn();
	}

	unlock();
	return 0;
}

int
csh_set_i(const char *key, int val)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%d", val);
	return csh_set(key, buf);
}

int
csh_set_f(const char *key, double val)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%.6f", val);
	return csh_set(key, buf);
}

int
csh_set_b(const char *key, bool val)
{
	return csh_set_i(key, val);
}

int
csh_set_b_toggle(const char *key)
{
	struct csh_var *var;

	lock();
	var = get_var(key);
	if (!var) {
		unlock();
		return -ENOENT;
	}

	set_var_val(var, *var->b ? "0" : "1");
	if (var->cb_fn) {
		var->cb_fn();
	}

	unlock();

	return 0;
}

const char *
csh_get(const char *key)
{
	static char tmpbuf[64];
	struct csh_var *var;
	const char *ret;

	lock();
	var = get_var(key);
	if (!var) {
		unlock();
		return NULL;
	}

	switch (var->type) {
		case CSH_T_STRING:
			ret = var->s.buf;
			break;
		case CSH_T_DYN_STRING:
			/* a copy is needed to avoid data races - use after free */
			snprintf(tmpbuf, sizeof(tmpbuf), "%s", *var->dyn_s ? *var->dyn_s : "");
			ret = tmpbuf;
			break;
		case CSH_T_INT:
			snprintf(tmpbuf, sizeof(tmpbuf), "%d", *var->i);
			ret = tmpbuf;
			break;
		case CSH_T_DOUBLE:
			snprintf(tmpbuf, sizeof(tmpbuf), "%.6f", *var->d);
			ret = tmpbuf;
			break;
		case CSH_T_BOOL:
			ret = *var->b ? "1" : "0";
			break;
		default:
			assert(false);
			break;
	}

	unlock();
	return ret;
}

int
csh_get_i(const char *key)
{
	struct csh_var *var;
	int ret;

	lock();
	var = get_var(key);
	if (!var) {
		unlock();
		return 0;
	}

	switch (var->type) {
		case CSH_T_STRING:
			ret = var->s.buf ? strtoll(var->s.buf, NULL, 0) : 0;
			break;
		case CSH_T_DYN_STRING:
			ret = *var->dyn_s ? strtoll(*var->dyn_s, NULL, 0) : 0;
			break;
		case CSH_T_INT:
			ret = *var->i;
			break;
		case CSH_T_BOOL:
			ret = *var->b;
			break;
		case CSH_T_DOUBLE:
			ret = *var->d;
			break;
		case CSH_T_NONE:
			assert(false);
			break;
	}

	unlock();
	return ret;
}

bool
csh_get_b(const char *key)
{
	return csh_get_i(key);
}

double
csh_get_f(const char *key)
{
	struct csh_var *var;
	double ret;

	lock();
	var = get_var(key);
	if (!var) {
		unlock();
		return 0;
	}

	switch (var->type) {
		case CSH_T_STRING:
			ret = var->s.buf ? strtod(var->s.buf, NULL) : 0;
			break;
		case CSH_T_DYN_STRING:
			ret = *var->dyn_s ? strtod(*var->dyn_s, NULL) : 0;
			break;
		case CSH_T_INT:
			ret = *var->i;
			break;
		case CSH_T_BOOL:
			ret = *var->b;
			break;
		case CSH_T_DOUBLE:
			ret = *var->d;
			break;
		case CSH_T_NONE:
			assert(false);
			break;
	}

	unlock();
	return ret;
}

void *
csh_get_ptr(const char *key)
{
	struct csh_var *var;
	void *ret;

	lock();
	var = get_var(key);
	if (!var) {
		unlock();
		return NULL;
	}

	switch (var->type) {
		case CSH_T_STRING:
			ret = var->s.buf;
			break;
		case CSH_T_DYN_STRING:
			ret = var->dyn_s;
			break;
		case CSH_T_INT:
			ret = var->i;
			break;
		case CSH_T_BOOL:
			ret = var->b;
			break;
		case CSH_T_DOUBLE:
			ret = var->d;
			break;
		case CSH_T_NONE:
			break;
		default:
			assert(false);
			break;
	}

	unlock();
	return ret;
}

static void
cfg_parse_fn(const char *cmd, void *ctx)
{
	csh_cmd(cmd);
}

int
csh_init(const char *file)
{
	snprintf(g_csh_cfg.filename, sizeof(g_csh_cfg.filename), "%s", file);

	csh_cfg_parse(cfg_parse_fn, NULL);
	return 0;
}

static void
save_var_foreach_cb(void *el, void *ctx1, void *ctx2)
{
	struct pw_avl_node *node = el;
	struct csh_var *var = (void *)node->data;

	switch (var->type) {
	case CSH_T_STRING:
		csh_cfg_save_s(var->key, var->s.buf, false);
		break;
	case CSH_T_DYN_STRING:
		csh_cfg_save_s(var->key, *var->dyn_s ? *var->dyn_s : "", false);
		break;
	case CSH_T_INT:
		csh_cfg_save_i(var->key, *var->i, false);
		break;
	case CSH_T_BOOL:
		csh_cfg_save_i(var->key, *var->b, false);
		break;
	case CSH_T_DOUBLE:
		csh_cfg_save_f(var->key, *var->d, false);
		break;
	default:
		assert(false);
		break;
	}
}

int
csh_save(const char *file)
{
	lock();
	snprintf(g_csh_cfg.filename, sizeof(g_csh_cfg.filename), "%s", file);

	pw_avl_foreach(g_var_avl, save_var_foreach_cb, NULL, NULL);

	csh_cfg_save_s(NULL, NULL, true);
	unlock();
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

const char *
csh_cmd(const char *usercmd)
{
	struct csh_cmd *cmd, tmp;
	char *c;
	uint32_t hash;
	char buf[128];
	int prefixlen;

	lock();
	snprintf(buf, sizeof(buf), "%s", usercmd);
	c = buf;
	while (*c) {
		if (*c == ' ') {
			*c = 0;
			prefixlen = (int)(c - buf);
			break;
		}
		c++;
	}

	hash = djb2(buf);

	cmd = g_cmds;
	while (cmd) {
		if (cmd->prefix_hash == hash && strcmp(cmd->prefix, buf) == 0) {
			const char *ret;

			tmp = *cmd;
			snprintf(buf, sizeof(buf), "%s", usercmd + prefixlen);
			ret = tmp.fn(cleanup_str(buf, " \t\r\n"), tmp.ctx);
			unlock();
			return ret;
		}
		cmd = cmd->next;
	}

	unlock();
	return "^ff0000Unknown command.";
}

const char *
csh_cmdf(const char *fmt, ...)
{
	va_list args;
	char buf[256];
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	return csh_cmd(buf);
}

int
csh_register_cmd(const char *prefix, csh_cmd_handler_fn fn, void *ctx)
{
	struct csh_cmd *cmd;
	uint32_t hash = djb2(prefix);
	int rc;

	lock();
	cmd = g_cmds;
	while (cmd) {
		if (cmd->prefix_hash == hash && strcmp(cmd->prefix, prefix) == 0) {
			unlock();
			return -EALREADY;
		}
		cmd = cmd->next;
	}

	cmd = calloc(1, sizeof(*cmd));
	assert(cmd != NULL);

	rc = snprintf(cmd->prefix, sizeof(cmd->prefix), "%s", prefix);
	assert(rc <= sizeof(cmd->prefix) - 1);

	cmd->prefix_hash = hash;
	cmd->fn = fn;
	cmd->ctx = ctx;

	cmd->next = g_cmds;
	g_cmds = cmd;

	unlock();
	return 0;
}

static void
csh_register_var(const char *key, struct csh_var tmpvar)
{
	struct csh_var *var;
	uint32_t hash = djb2(key);

	lock();
	var = pw_avl_get(g_var_avl, hash);
	while (var && strcmp(var->key, key) != 0) {
		var = pw_avl_get_next(g_var_avl, var);
	}

	assert(var == NULL || !var->initialized);
	if (var != NULL) {
		var->initialized = true;
		unlock();
		return;
	}

	var = pw_avl_alloc(g_var_avl);
	assert(var);

	memcpy(var, &tmpvar, sizeof(*var));
	snprintf(var->key, sizeof(var->key), "%s", key);

	switch (var->type) {
		case CSH_T_NONE:
			assert(false);
			break;
		case CSH_T_STRING:
			if (var->def_val.s) {
				snprintf(var->s.buf, var->s.len, "%s", var->def_val.s);
			}
			break;
		case CSH_T_DYN_STRING:
			if (var->def_val.s) {
				*var->dyn_s = strdup(var->def_val.s);
			}
			assert(*var->dyn_s);
			break;
		case CSH_T_INT:
			*var->i = var->def_val.i;
			break;
		case CSH_T_BOOL:
			*var->b = var->def_val.b;
			break;
		case CSH_T_DOUBLE:
			*var->d = var->def_val.d;
			break;
	}

	var->initialized = true;
	pw_avl_insert(g_var_avl, hash, var);
	unlock();
}

void
csh_register_var_s(const char *key, char *buf, int buflen, const char *defval)
{
	csh_register_var(key, (struct csh_var){ .type = CSH_T_STRING,
			.s = { .buf = buf, .len = buflen },
			.def_val = { .s = defval } });
}

void
csh_register_var_dyn_s(const char *key, char **buf, const char *defval)
{
	csh_register_var(key, (struct csh_var){ .type = CSH_T_DYN_STRING,
			.dyn_s = buf, .def_val = { .s = defval } });
}

void
csh_register_var_i(const char *key, int *val, int defval)
{
	csh_register_var(key, (struct csh_var){ .type = CSH_T_INT, .i = val,
			.def_val = { .i = defval } });
}

void
csh_register_var_b(const char *key, bool *val, bool defval)
{
	csh_register_var(key, (struct csh_var){ .type = CSH_T_BOOL, .b = val,
			.def_val = { .b = defval } });
}

void
csh_register_var_f(const char *key, double *val, double defval)
{
	csh_register_var(key, (struct csh_var){ .type = CSH_T_DOUBLE, .d = val,
			.def_val = { .d = defval } });
}

void
csh_register_var_callback(const char *key, csh_set_cb_fn fn)
{
	struct csh_var *var;
	uint32_t hash = djb2(key);

	lock();
	var = pw_avl_get(g_var_avl, hash);
	while (var && strcmp(var->key, key) != 0) {
		var = pw_avl_get_next(g_var_avl, var);
	}

	assert(var);
	assert(!var->cb_fn);

	var->cb_fn = fn;
	unlock();
}

static void
static_preinit_foreach_var_cb(void *el, void *ctx1, void *ctx2)
{
	struct pw_avl_node *node = el;
	struct csh_var *var = (void *)node->data;

	var->initialized = false;
	var->cb_fn = NULL;
}

void
csh_static_preinit(void)
{
	struct csh_cmd *cmd, *tmp;

	g_static_init_done = false;

	cmd = g_cmds;
	while (cmd) {
		tmp = cmd->next;
		free(cmd);
		cmd = tmp;
	}
	g_cmds = NULL;

	if (g_var_avl) {
		pw_avl_foreach(g_var_avl, static_preinit_foreach_var_cb, NULL, NULL);
	} else {
		g_var_avl = pw_avl_init(sizeof(struct csh_var));
		assert(g_var_avl != NULL);
	}

	csh_register_cmd("profile", cmd_profile_fn, NULL);
	csh_register_cmd("set", cmd_set_var_fn, NULL);
	csh_register_cmd("show", cmd_show_var_fn, NULL);
}

void
csh_static_postinit(void)
{
	g_static_init_done = true;
}