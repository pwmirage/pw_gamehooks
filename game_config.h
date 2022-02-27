/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021-2022 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_GAME_CONFIG_H
#define PW_GAME_CONFIG_H

#include <stdint.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*game_config_fn)(const char *cmd, void *ctx);

/** Set file path to be used for reading and writing. */
int game_config_set_file(const char *filepath);

/** Set $PROFILE for use inside if conditions in the file */
int game_config_set_profile(const char *profile);

/**
 * Try to open and read a config file at path that was set earlier.
 * fn is called for every meaningful line (non-empty, non-comment)
 * in the file.
 */
int game_config_parse(game_config_fn fn, void *fn_ctx);

/**
 * Try to open the config file, locate a setting with the same key,
 * then try to update it or append new entry at the end. If flush param
 * is false, the new value is just kept in memory, awaiting for further
 * calls of this function.
 */
int game_config_save_s(const char *key, const char *val, bool flush);
int game_config_save_i(const char *key, int64_t val, bool flush);
int game_config_save_f(const char *key, float val, bool flush);

#ifdef __cplusplus
}
#endif

#endif /* PW_GAME_CONFIG_H */
