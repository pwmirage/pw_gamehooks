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

#include "csh.h"
#include "avl.h"
#include "pw_api.h"

static char g_filename[128];

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

	assert(g_filename[0] != 0);
	fp = fopen(g_filename, "w");
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

static const char *
cmd_profile_fn(const char *val, void *ctx)
{
	if (*val == 0) {
		return g_profile;
	}

	snprintf(g_profile, sizeof(g_profile), "%s", val);
	return NULL;
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

	return NULL;
}

int
csh_set(const char *key, const char *val)
{
	struct csh_var *var;
	uint32_t hash = djb2(key);

	var = pw_avl_get(g_var_avl, hash);
	while (var && strcmp(var->key, key) != 0) {
		var = pw_avl_get_next(g_var_avl, var);
	}

	if (!var) {
		return -ENOENT;
	}

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

	var->cb_fn();

	return 0;
}

int
csh_set_i(const char *key, int64_t val)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%"PRId64, val);
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
csh_init(const char *file)
{
	snprintf(g_filename, sizeof(g_filename), "%s", file);
	/* TODO read config file, save its name */
	return 0;
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
	struct csh_cmd *cmd;
	char *c;
	uint32_t hash;
	char buf[128];
	int prefixlen;

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
			snprintf(buf, sizeof(buf), "%s", usercmd + prefixlen);
			return cmd->fn(cleanup_str(buf, " \t\r\n"), cmd->ctx);
		}
		cmd = cmd->next;
	}

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

	cmd = g_cmds;
	while (cmd) {
		if (cmd->prefix_hash == hash && strcmp(cmd->prefix, prefix) == 0) {
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

	return 0;
}

static void
csh_register_var(const char *key, struct csh_var tmpvar)
{
	struct csh_var *var;
	uint32_t hash = djb2(key);

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
			*var->i = var->def_val.b;
			break;
		case CSH_T_DOUBLE:
			*var->d = var->def_val.d;
			break;
	}

	pw_avl_insert(g_var_avl, hash, var);
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

	var = pw_avl_get(g_var_avl, hash);
	while (var && strcmp(var->key, key) != 0) {
		var = pw_avl_get_next(g_var_avl, var);
	}

	assert(var);
	assert(!var->cb_fn);

	var->cb_fn = fn;
}

static void __attribute__((constructor (104)))
static_init(void)
{
	g_var_avl = pw_avl_init(sizeof(struct csh_var));
	assert(g_var_avl != NULL);

	csh_register_cmd("profile", cmd_profile_fn, NULL);
	csh_register_cmd("set", cmd_set_var_fn, NULL);

}
