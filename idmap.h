/* SPDX-License-Identifier: MIT
 * Copyright(c) 2019-2021 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_IDMAP_H
#define PW_IDMAP_H

#include <stdint.h>
#include <inttypes.h>

struct pw_idmap_el {
	long long lid;
	long id;
	short type;
	uint8_t is_async_fn : 1;
	uint8_t _unused1: 7;
	char _unused2;
	void *data;
	void *next;
};

typedef void (*pw_idmap_async_fn)(struct pw_idmap_el *node, void *ctx);

struct pw_idmap *pw_idmap_init(const char *name, const char *filename, int can_set);
void pw_idmap_ignore_dups(struct pw_idmap *map);
long pw_idmap_register_type(struct pw_idmap *map);
struct pw_idmap_el *pw_idmap_get(struct pw_idmap *map, long long lid, long type);
unsigned pw_idmap_get_mapping(struct pw_idmap *map, long long lid, long type);
int pw_idmap_get_async(struct pw_idmap *map, long long lid, long type, pw_idmap_async_fn fn, void *fn_ctx);
struct pw_idmap_el *pw_idmap_set(struct pw_idmap *map, long long lid, long type, void *data);
int pw_idmap_save(struct pw_idmap *map, const char *filename);

#endif /* PW_IDMAP_H */
