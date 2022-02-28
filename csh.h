/* SPDX-License-Identifier: MIT
 * Copyright(c) 2022 Darek Stojaczyk
 */

#ifndef CSH_H
#define CSH_H

#include <stdint.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EALREADY
#define	EALREADY	114
#endif

/**
 * Mini runtime shell compatible with csh_config.
 * Defines the following commands usable with csh_cmd():
 *
 * set <varname> <value>
 * profile <profilename>
 *
 * TODO:
 * reload_cfg
 * unset
 * dump_cfg
 *
 * More can be specified with csh_register_var_*()
 */

typedef const char *(*csh_cmd_handler_fn)(const char *val, void *ctx);
typedef void (*csh_set_cb_fn)(void);

int csh_init(const char *file);
const char *csh_cmd(const char *cmd);
const char *csh_cmdf(const char *cmd, ...);
int csh_set(const char *key, const char *val);
int csh_set_i(const char *key, int64_t val);
int csh_set_f(const char *key, double val);
int csh_register_cmd(const char *prefix, csh_cmd_handler_fn fn, void *ctx);

void csh_register_var_s(const char *name, char *buf, int buflen, const char *defval);
void csh_register_var_dyn_s(const char *name, char **var, const char *defval);
void csh_register_var_i(const char *name, int *var, int64_t defval);
void csh_register_var_f(const char *name, double *var, double defval);
void csh_register_var_b(const char *name, bool *var, bool defval);

void csh_register_var_callback(const char *name, csh_set_cb_fn fn);

/* utility macros */
#define _CSH_JOIN2(a, b) a ## _ ## b
#define CSH_JOIN2(a, b) _CSH_JOIN2(a, b)
#define CSH_UNIQUENAME(str) CSH_JOIN2(str, __LINE__)
#define GET_4TH_ARG(arg1, arg2, arg3, arg4, ...) arg4
#define GET_5TH_ARG(arg1, arg2, arg3, arg4, arg5, ...) arg5

/* registering T_STRING with optional default value */
#define _CSH_REGISTER_VAR_S(name_p, buf_p, buflen_p, defval_p) \
static void __attribute__((constructor (105))) \
CSH_UNIQUENAME(init_csh_register_fn)(void) \
{ \
    csh_register_var_s((name_p), (buf_p), (buflen_p), (defval_p)); \
}

#define _CSH_REGISTER_VAR_S_NODEF(name_p, buf_p, buflen_p) \
    _CSH_REGISTER_VAR_S(name_p, buf_p, buflen_p, "")

#define _CSH_REGISTER_VAR_S_CHOOSER(...) \
    GET_4TH_ARG(__VA_ARGS__, _CSH_REGISTER_VAR_S, _CSH_REGISTER_VAR_S_NODEF, )

#define CSH_REGISTER_VAR_S(...) \
    _CSH_REGISTER_VAR_S_CHOOSER(__VA_ARGS__)(__VA_ARGS)

/* registering every other T_* with optional default value */
#define _CSH_REGISTER_VAR(type_suffix_p, name_p, var_p, defval_p) \
static void __attribute__((constructor (105))) \
CSH_UNIQUENAME(init_csh_register_fn)(void) \
{ \
    csh_register_var_ ## type_suffix_p(name_p, var_p, defval_p); \
}

#define _CSH_REGISTER_VAR_NODEF(type_suffix_p, name_p, var_p) \
    _CSH_REGISTER_VAR(type_suffix_p, name_p, var_p, 0)

#define _CSH_REGISTER_VAR_CHOOSER(...) \
    GET_5TH_ARG(__VA_ARGS__, _CSH_REGISTER_VAR, _CSH_REGISTER_VAR_NODEF, )

#define CSH_REGISTER_VAR(type, ...) \
    _CSH_REGISTER_VAR_CHOOSER(type, __VA_ARGS__)(type, __VA_ARGS__)

#define CSH_REGISTER_VAR_I(...) CSH_REGISTER_VAR(i, __VA_ARGS__);
#define CSH_REGISTER_VAR_B(...) CSH_REGISTER_VAR(b, __VA_ARGS__);
#define CSH_REGISTER_VAR_F(...) CSH_REGISTER_VAR(f, __VA_ARGS__);

/* registering var modification callbacks */
#define CSH_REGISTER_VAR_CALLBACK(name_p) \
static void CSH_UNIQUENAME(init_csh_register_var_cb_fn)(void); \
static void __attribute__((constructor (106))) \
CSH_UNIQUENAME(init_csh_register_var_fn)(void) \
{ \
    csh_register_var_callback(name_p, CSH_UNIQUENAME(init_csh_register_var_cb_fn)); \
} \
static void CSH_UNIQUENAME(init_csh_register_var_cb_fn)

#ifdef __cplusplus
}
#endif

#endif /* CSH_H */
