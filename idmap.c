/* SPDX-License-Identifier: MIT
 * Copyright(c) 2020 Darek Stojaczyk for pwmirage.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>

#include "idmap.h"
#include "common.h"
#include "avl.h"
#include "pw_api.h"

#define IDMAP_VERSION 3

struct pw_idmap_file_hdr {
	uint32_t version;
};

struct pw_idmap_file_entry {
	uint64_t lid;
	uint32_t id;
	uint8_t type;
};

struct pw_idmap {
	char *name;
	long registered_types_cnt;
	long max_id;
	int can_set;
	struct pw_avl *lid_mappings;
	struct pw_avl *id_mappings;
	struct pw_avl *by_lid;
	struct pw_avl *by_id;
	bool ignore_dups;
};

struct pw_idmap_async_fn_el {
	pw_idmap_async_fn fn;
	void *ctx;
	struct pw_idmap_async_fn_el *next;
};

struct pw_idmap_async_fn_head {
	struct pw_idmap_async_fn_el *head;
	struct pw_idmap_async_fn_el **tail;
};

struct pw_idmap *
pw_idmap_init(const char *name, const char *filename, int can_set)
{
	struct pw_idmap *map;
	FILE *fp;
	int i;

	map = calloc(1, sizeof(*map));
	if (!map) {
		return NULL;
	}

	map->name = strdup(name);
	if (!map->name) {
		free(map);
		return NULL;
	}

	map->can_set = can_set;

	map->by_lid = pw_avl_init(sizeof(struct pw_idmap_el));
	if (!map->by_lid) {
		free(map->name);
		free(map);
		return NULL;
	}

	map->by_id = pw_avl_init(sizeof(struct pw_idmap_el *));
	if (!map->by_id) {
		free(map->name);
		free(map);
		return NULL;
	}

	map->lid_mappings = pw_avl_init(sizeof(struct pw_idmap_file_entry));
	if (!map->lid_mappings) {
		free(map->name);
		free(map);
		return NULL;
	}

	map->id_mappings = pw_avl_init(sizeof(struct pw_idmap_file_entry *));
	if (!map->id_mappings) {
		free(map->name);
		free(map);
		return NULL;
	}

	if (!filename) {
		return map;
	}

	fp = fopen(filename, "rb");
	if (fp == NULL) {
		/* we'll create it on pw_idmap_save(), no problem */
		return map;
	}

	struct pw_idmap_file_hdr hdr;
	fread(&hdr, 1, sizeof(hdr), fp);

	if (hdr.version != IDMAP_VERSION) {
		/* pretend it's not even there */
		fclose(fp);
		return map;
	}

	size_t fpos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	size_t fsize = ftell(fp);
	fseek(fp, fpos, SEEK_SET);
	int entry_cnt = (fsize - fpos) / sizeof(struct pw_idmap_file_entry);

	for (i = 0; i < entry_cnt; i++) {
		struct pw_idmap_file_entry *entry;
		struct pw_idmap_file_entry **id_entry;

		entry = pw_avl_alloc(map->lid_mappings);
		assert(entry);
		id_entry = pw_avl_alloc(map->id_mappings);
		assert(id_entry);

		*id_entry = entry;

		fread(entry, 1, sizeof(*entry), fp);

		pw_log("%s: lid=0x%llx, id=%u\n", map->name, entry->lid, entry->id);
		pw_avl_insert(map->lid_mappings, entry->lid, entry);
		pw_avl_insert(map->id_mappings, entry->id, id_entry);

		if (entry->id > map->max_id) {
			map->max_id = entry->id;
		}
	}

	fclose(fp);
	return map;
}

void
pw_idmap_ignore_dups(struct pw_idmap *map)
{
	map->ignore_dups = true;
}

long
pw_idmap_register_type(struct pw_idmap *map)
{
	return ++map->registered_types_cnt;
}

struct pw_idmap_el *
_idmap_get(struct pw_idmap *map, long long lid, long type, bool async)
{
	struct pw_idmap_el *el;
	struct pw_idmap_el **el_p;

	el = pw_avl_get(map->by_lid, lid);
	while (el && ((!async && el->is_async_fn) || (type && el->type && el->type != type))) {
		el = pw_avl_get_next(map->by_lid, el);
	}

	if (el || lid >= 0x80000000) {
		return el;
	}

	el_p = pw_avl_get(map->by_id, lid);
	el = el_p ? *el_p : NULL;

	while (el && (type && el->type && el->type != type)) {
		el_p = pw_avl_get_next(map->by_id, el_p);
		el = el_p ? *el_p : NULL;
	}
	return el;
}

unsigned
pw_idmap_get_mapping(struct pw_idmap *map, long long lid, long type)
{
	struct pw_idmap_file_entry *entry;

	entry = pw_avl_get(map->lid_mappings, lid);
	if (entry) {
		return entry->id;
	}

	return 0;
}

struct pw_idmap_el *
pw_idmap_get(struct pw_idmap *map, long long lid, long type)
{
	return _idmap_get(map, lid, type, false);
}

/* retrieve the item even if it's not set yet. The callback will be fired
 * once pw_idmap_set() hits */
int
pw_idmap_get_async(struct pw_idmap *map, long long lid, long type, pw_idmap_async_fn fn, void *fn_ctx)
{
	struct pw_idmap_el *el;
	struct pw_idmap_async_fn_el *async_el;
	struct pw_idmap_async_fn_head *async_head;

	el = pw_avl_get(map->by_lid, lid);
	while (el && (type && el->type && el->type != type)) {
		el = pw_avl_get_next(map->by_lid, el);
	}

	if (el && !el->is_async_fn) {
		fn(el, fn_ctx);
		return 0;
	}

	pw_log("setting async mapping at lid=0x%llx\n", lid);

	async_el = calloc(1, sizeof(*async_el));
	if (!async_el) {
		return -1;
	}
	async_el->fn = fn;
	async_el->ctx = fn_ctx;

	if (!el) {
		el = pw_idmap_set(map, lid, type, NULL);
		if (!el) {
			free(async_el);
			return -1;
		}
		el->is_async_fn = 1;
	}

	async_head = el->data;
	if (!async_head) {
		async_head = calloc(1, sizeof(*async_head));
		if (!async_head) {
			free(async_el);
			return -1;
		}

		async_head->tail = &async_head->head;
		el->data = async_head;
	}

	/* put it to the end to ensure the call order. everything queued first will
	 * be called first. */
	*async_head->tail = async_el;
	async_head->tail = &async_el->next;

	return 0;
}

static void
call_async_arr(struct pw_idmap_async_fn_head *async, struct pw_idmap_el *node)
{
	struct pw_idmap_async_fn_el *async_el, *tmp;

	async_el = async->head;
	while (async_el) {
		async_el->fn(node, async_el->ctx);
		tmp = async_el;
		async_el = async_el->next;
		free(tmp);
	}
}

struct pw_idmap_el *
pw_idmap_set(struct pw_idmap *map, long long lid, long type, void *data)
{
	struct pw_idmap_el *el, *tmp;

	el = _idmap_get(map, lid, type, true);
	if (el && !el->is_async_fn) {
		if (map->ignore_dups) {
			return NULL;
		}

		pw_log("Conflict at lid=0x%x, type=%d (el->type=%d, el->id=%d)\n", lid, type, el->type, el->id);
		assert(false);
		return NULL;
	}

	if (el) {
		struct pw_idmap_async_fn_head *async_head = el->data;

		el->is_async_fn = 0;
		el->data = data;

		call_async_arr(async_head, el);
		free(async_head);
		return el;
	}

	el = pw_avl_alloc(map->by_lid);
	if (!el) {
		pw_log("pw_avl_alloc() failed\n");
		return NULL;
	}

	if (lid < 0x80000000) {
		struct pw_idmap_file_entry **entry_p;
		struct pw_idmap_file_entry *entry;

		entry_p = pw_avl_get(map->id_mappings, lid);
		entry = entry_p ? *entry_p : NULL;

		while (entry && (type && entry->type && entry->type != type)) {
			entry_p = pw_avl_get_next(map->by_id, entry_p);
			entry = entry_p ? *entry_p : NULL;
		}

		if (entry) {
			el->id = entry->id;
			lid = entry->lid;
		}
	}

	el->lid = lid;
	el->type = type;
	el->data = data;
	pw_avl_insert(map->by_lid, lid, el);

	if (lid < 0x80000000) {
		el->id = lid;
	} else {
		struct pw_idmap_file_entry *entry;

		entry = pw_avl_get(map->lid_mappings, lid);
		if (entry) {
			el->id = entry->id;
		} else {

			if (!map->can_set) {
				pw_log("Unexpected lid=0x%x\n", lid);
				assert(false);
				return NULL;
			}

			el->id = map->max_id + 1;

			entry = pw_avl_alloc(map->lid_mappings);
			assert(entry);
			entry->lid = el->lid;
			entry->id = el->id;
			entry->type = el->type;

			pw_avl_insert(map->lid_mappings, lid, entry);
		}
	}

	if (el->id > map->max_id) {
		assert(el->id <= 0x80000000);
		map->max_id = el->id;
	}

	tmp = pw_avl_alloc(map->by_id);
	if (!tmp) {
		pw_log("pw_avl_alloc() failed\n");
		return NULL;
	}

	*(struct pw_idmap_el **)tmp = el;
	pw_avl_insert(map->by_id, el->id, tmp);

	return el;
}

static void
idmap_save_cb(void *_node, void *ctx1, void *ctx2)
{
	struct pw_avl_node *node = _node;
	struct pw_idmap_file_entry *entry = (void *)node->data;
	FILE *fp = ctx1;
	struct pw_idmap *map = ctx2;

	pw_log("%s: lid=0x%llx, id=%u\n", map->name, entry->lid, entry->id);
	fwrite(entry, 1, sizeof(*entry), fp);
}

int
pw_idmap_save(struct pw_idmap *map, const char *filename)
{
	FILE *fp;

	assert(map->can_set);

	fp = fopen(filename, "wb");
	if (fp == NULL) {
		pw_log("Cant open %s\n", filename);
		return -errno;
	}

	struct pw_idmap_file_hdr hdr;
	hdr.version = IDMAP_VERSION;
	fwrite(&hdr, 1, sizeof(hdr), fp);

	pw_avl_foreach(map->lid_mappings, idmap_save_cb, fp, map);
	fclose(fp);
	return 0;
}
