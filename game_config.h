/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_GAME_CONFIG_H
#define PW_GAME_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

int game_config_parse(const char *filepath);
const char *game_config_get_str(const char *category, const char *key, const char *defval);
int game_config_get_int(const char *category, const char *key, int defval);
int game_config_set_str(const char *category, const char *key, const char *value);
int game_config_set_int(const char *category, const char *key, int value);
int game_config_set_invalid(const char *category, const char *val);
void game_config_save(bool close);

#ifdef __cplusplus
}
#endif

#endif /* PW_GAME_CONFIG_H */
