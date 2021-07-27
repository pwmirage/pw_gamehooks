/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include "avl.h"
#include "pw_item_desc.h"

static struct pw_item_desc_state {
	char *filename;
	struct pw_avl *avl;
} g_state;

struct pw_avl *g_pw_item_desc_avl;

static const char *g_empty_str = "";

#define ITEM_DESC_MAGIC 0x7ab30e1f
#define ITEM_DESC_VERSION 1

struct pw_item_desc_hdr {
	uint32_t magic;
	uint32_t ver;
	uint32_t count;
};

struct pw_item_desc_file_entry {
	uint32_t id;
	uint32_t len;
	char desc[0];
};

int
pw_item_desc_load(const char *filepath)
{
	struct pw_item_desc_hdr hdr;
	struct pw_item_desc_entry *entry;
	int i, rc = 0;

	g_state.filename = strdup(filepath);
	if (!g_state.filename) {
		return -ENOMEM;
	}

	g_state.avl = g_pw_item_desc_avl = pw_avl_init(sizeof(struct pw_item_desc_entry));
	if (!g_state.avl) {
		free(g_state.filename);
		return -ENOMEM;
	}

	FILE *fp = fopen(filepath, "rb");
	if (!fp) {
		/* no problem, we'll create this file later */
		return 0;
	}

	fread(&hdr, sizeof(hdr), 1, fp);

	if (hdr.magic != ITEM_DESC_MAGIC) {
		/* fail because we don't want to override this file later on */
		rc = -EIO;
		goto out;
	}

	if (hdr.ver != ITEM_DESC_VERSION) {
		rc = 0;
		goto out;
	}

	for (i = 0; i < hdr.count; i++) {
		struct pw_item_desc_file_entry file_entry;

		entry = pw_avl_alloc(g_state.avl);
		if (!entry) {
			rc = -ENOMEM;
			goto out;
		}

		fread(&file_entry, sizeof(file_entry), 1, fp);
		entry->id = file_entry.id;
		entry->len = file_entry.len;
		entry->desc = malloc(entry->len + 1);
		if (!entry->desc) {
			rc = -ENOMEM;
			goto out;
		}

		/* expect it to be null-terminated */
		fread(entry->desc, entry->len + 1, 1, fp);
		pw_avl_insert(g_state.avl, entry->id, entry);
	}

	rc = 0;
out:
	fclose(fp);
	return rc;
}

struct pw_item_desc_entry *
pw_item_desc_get(int id)
{
	struct pw_item_desc_entry *entry;

	entry = pw_avl_get(g_state.avl, id);
	while (entry && entry->id != id) {
		entry = pw_avl_get_next(g_state.avl, entry);
	}

	return entry;
}

int
pw_item_desc_set(int id, const char *desc)
{
	struct pw_item_desc_entry *entry;

	entry = pw_item_desc_get(id);
	if (!entry) {
		entry = pw_avl_alloc(g_state.avl);
		if (!entry) {
			return -ENOMEM;
		}

		entry->id = id;

		pw_avl_insert(g_state.avl, id, entry);
	} else {
		if (entry->desc != g_empty_str) {
			free(entry->desc);
		}
	}

	if (!desc) {
		entry->desc = (char *)g_empty_str;
		entry->len = 0;
	} else {
		int i;

		entry->len = strlen(desc);
		entry->desc = strdup(desc);
		if (!entry->desc) {
			entry->desc = (char *)g_empty_str;
			return -ENOMEM;
		}

		i = entry->len - 1;
		while (i > 0 && (entry->desc[i] == '\n' || entry->desc[i] == '\r')) {
			entry->desc[i] = 0;
			i--;
		}
	}

	return 0;
}

static void
save_entry_cb(void *el, void *ctx1, void *ctx2)
{
	FILE *fp = ctx1;
	struct pw_avl_node *node = el;
	struct pw_item_desc_entry *entry = (void *)node->data;
	struct pw_item_desc_file_entry file_entry;
	uint32_t *count = ctx2;

	file_entry.id = entry->id;
	file_entry.len = entry->len;
	fwrite(&file_entry, sizeof(file_entry), 1, fp);
	fwrite(entry->desc, entry->len + 1, 1, fp);
	(*count)++;
}

int
pw_item_desc_save(void)
{
	struct pw_item_desc_hdr hdr = {};
	FILE *fp = fopen(g_state.filename, "wb");

	if (!fp) {
		return -errno;
	}

	fwrite(&hdr, sizeof(hdr), 1, fp);
	pw_avl_foreach(g_state.avl, save_entry_cb, fp, &hdr.count);

	hdr.magic = ITEM_DESC_MAGIC;
	hdr.ver = ITEM_DESC_VERSION;
	fseek(fp, 0, SEEK_SET);
	fwrite(&hdr, sizeof(hdr), 1, fp);

	fclose(fp);
	return 0;
}
