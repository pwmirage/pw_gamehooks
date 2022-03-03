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

#ifdef DLLEXPORT
#define APICALL __declspec(dllexport)
#else
#define APICALL __declspec(dllimport)
#endif

/**
 * Mini runtime shell compatible with csh_config. Manages all memory internally.
 * Any passed buffers are copied.
 *
 * Defines the following commands usable with csh_cmd():
 *
 * set <varname> <value>
 * show <varname>
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

/** Load csh_config from given file, execute commands inside. */
APICALL int csh_init(const char *file);

/**
 * Dump all variables into a file. If it doesn't exist it's created.
 * If it exists it's updated without changing its syntax, comments, etc.
 */
APICALL int csh_save(const char *file);

/**
 * Execute given command inside the shell.
 *
 * \return response message. May be prefixed by color, e.g.
 * ^ff0000Invalid command.
 */
APICALL const char *csh_cmd(const char *cmd);

/** Printf-like variant of csh_cmd(); */
APICALL const char *csh_cmdf(const char *cmd, ...);

/**
 * Set internal variable (that was registered earlier with csh_register_var_*()).
 * This will update the variable, call its callbacks immediatly, and then return
 *
 * \return 0 on success, negative errno otherwise, e.g. -ENOENT if variable wasn't
 * registered
 */
APICALL int csh_set(const char *key, const char *val);
/** Variant of csh_set() */
APICALL int csh_set_i(const char *key, int val);
/** Variant of csh_set() */
APICALL int csh_set_f(const char *key, double val);
/** Variant of csh_set() */
APICALL int csh_set_b(const char *key, bool val);
/** Variant of csh_set() */
APICALL int csh_set_b_toggle(const char *key);

/** Get variable's value. NULL if unset or variable doesn't exist. */
APICALL const char *csh_get(const char *key);
/** Variant of csh_show() */
APICALL int csh_get_i(const char *key);
/** Variant of csh_show() */
APICALL double csh_get_f(const char *key);
/** Variant of csh_show() */
APICALL bool csh_get_b(const char *key);
/** Get pointer to the backing variable */
APICALL void *csh_get_ptr(const char *key);

/**
 * Register custom handler for commands starting with `prefix`.
 *
 * \return 0 on success, negative errno otherwise, e.g. -EALREADY if `prefix`
 * is already registered.
 */
APICALL int csh_register_cmd(const char *prefix, csh_cmd_handler_fn fn, void *ctx);

/**
 * Register new variable. All data set by the user will be copied into the
 * provided buffer at `buf`, max `buflen` characters (including null terminator).
 * Defval is immediately copied into the buffer.
 */
APICALL void csh_register_var_s(const char *name, char *buf, int buflen, const char *defval);

/**
 * Register new variable. All data set by the user will be put into a newly allocated
 * buffer, that will be set in *var. Defval will be strduped, unless it's NULL.
 */
APICALL void csh_register_var_dyn_s(const char *name, char **var, const char *defval);

/**
 * Register new variable. All data set by the user will be converted into an integer
 * and put into *var.
 */
APICALL void csh_register_var_i(const char *name, int *var, int defval);
/** \see csh_register_var_i() */
APICALL void csh_register_var_f(const char *name, double *var, double defval);
/** \see csh_register_var_i() */
APICALL void csh_register_var_b(const char *name, bool *var, bool defval);

/**
 * Register a function to be called after the given variable is ever modified.
 */
APICALL void csh_register_var_callback(const char *name, csh_set_cb_fn fn);

/** To be called before any CSH_REGISTER_*() */
APICALL void csh_static_preinit(void);

/** To be called after csh_static_preinit() and all CSH_REGISTER_*() */
APICALL void csh_static_postinit(void);

/* utility macros */
#define _CSH_JOIN2(a, b) a ## _ ## b
#define CSH_JOIN2(a, b) _CSH_JOIN2(a, b)
#define CSH_UNIQUENAME(str) CSH_JOIN2(str, __LINE__)
#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
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

/** Statically register a string variable (non-dynamic) */
#define CSH_REGISTER_VAR_S(...) \
    _CSH_REGISTER_VAR_S_CHOOSER(__VA_ARGS__)(__VA_ARGS__)


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

/** Statically register a string variable (dynamic) */
#define CSH_REGISTER_VAR_DYN_S(...) CSH_REGISTER_VAR(dyn_s, __VA_ARGS__);
/** Statically register an int variable */
#define CSH_REGISTER_VAR_I(...) CSH_REGISTER_VAR(i, __VA_ARGS__);
/** Statically register a bool variable */
#define CSH_REGISTER_VAR_B(...) CSH_REGISTER_VAR(b, __VA_ARGS__);
/** Statically register a double floating point variable (double) */
#define CSH_REGISTER_VAR_F(...) CSH_REGISTER_VAR(f, __VA_ARGS__);


/** Statically register a variable modification callback */
#define _CSH_REGISTER_VAR_CALLBACK_INLINE(name_p) \
static void CSH_UNIQUENAME(init_csh_register_var_cb_fn)(void); \
static void __attribute__((constructor (106))) \
CSH_UNIQUENAME(init_csh_register_var_fn)(void) \
{ \
    csh_register_var_callback(name_p, CSH_UNIQUENAME(init_csh_register_var_cb_fn)); \
} \
static void CSH_UNIQUENAME(init_csh_register_var_cb_fn)

#define _CSH_REGISTER_VAR_CALLBACK(name_p, cb_fn_p) \
static void __attribute__((constructor (106))) \
CSH_UNIQUENAME(init_csh_register_var_fn)(void) \
{ \
    csh_register_var_callback(name_p, cb_fn_p); \
}

#define _CSH_REGISTER_VAR_CALLBACK_CHOOSER(...) \
    GET_3RD_ARG(__VA_ARGS__, _CSH_REGISTER_VAR_CALLBACK, _CSH_REGISTER_VAR_CALLBACK_INLINE, )


#define CSH_REGISTER_VAR_CALLBACK(...) \
    _CSH_REGISTER_VAR_CALLBACK_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* CSH_H */
