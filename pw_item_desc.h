/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_ITEM_DESC_H
#define PW_ITEM_DESC_H

struct pw_avl;
extern struct pw_avl *g_pw_item_desc_avl;

struct pw_item_desc_entry {
	uint32_t id;
	uint32_t len;
	char *desc;
	wchar_t *aux;
};

int pw_item_desc_load(const char *filepath);
struct pw_item_desc_entry *pw_item_desc_get(int id);
int pw_item_desc_set(int id, const char *desc);
int pw_item_desc_save(void);

#endif /* PW_ITEM_DESC_H */
