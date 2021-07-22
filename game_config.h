/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_GAME_CONFIG_H
#define PW_GAME_CONFIG_H

struct game_config_opt {
	char key[128];
	char *val;
};

int game_config_parse(const char *filepath);
const char *game_config_get(const char *key, const char *defval);
struct game_config_opt *game_config_set(const char *key, const char *value);
void game_config_save(bool close);

#endif /* PW_GAME_CONFIG_H */
