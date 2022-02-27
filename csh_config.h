/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021-2022 Darek Stojaczyk
 */

#ifndef CSH_CONFIG_H
#define CSH_CONFIG_H

#include <stdint.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*csh_cfg_fn)(const char *cmd, void *ctx);

struct csh_cfg_internal;
extern struct csh_cfg {
	char filename[64];
	char profile[64];
	struct csh_cfg_internal *intrnl;
} g_csh_cfg;

/**
 * Try to open and read a config file at path that was set earlier.
 * fn is called for every meaningful line (non-empty, non-comment)
 * in the file.
 *
 * \return 0 on success, errno otherwise
 */
int csh_cfg_parse(csh_cfg_fn fn, void *fn_ctx);

/**
 * Try to open the config file, locate a setting with the same key,
 * then try to update it or append new entry at the end. If flush param
 * is false, the new value is just kept in memory, awaiting for further
 * calls of this function.
 */
int csh_cfg_save_s(const char *key, const char *val, bool flush);
int csh_cfg_save_i(const char *key, int64_t val, bool flush);
int csh_cfg_save_f(const char *key, float val, bool flush);

#ifdef __cplusplus
}
#endif

#endif /* CSH_CONFIG_H */
