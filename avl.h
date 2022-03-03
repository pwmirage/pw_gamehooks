/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#ifndef PW_AVL_H
#define PW_AVL_H

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

struct pw_avl_node {
	struct pw_avl_node *left;
	struct pw_avl_node *right;
	struct pw_avl_node *next; /**< same key */
	uint64_t key;
	int height;
	char data[0];
};

struct pw_avl {
	size_t el_size;
	size_t el_count;
	struct pw_avl_node *root;
};

typedef void (*pw_avl_foreach_cb)(void *el, void *ctx1, void *ctx2);

struct pw_avl *pw_avl_init(size_t el_size);
void pw_avl_deinit(struct pw_avl *avl);
void *pw_avl_alloc(struct pw_avl *avl);
void pw_avl_free(struct pw_avl *avl, void *data);
void pw_avl_insert(struct pw_avl *avl, uint64_t key, void *data);
void *pw_avl_get(struct pw_avl *avl, uint64_t key);
void *pw_avl_get_next(struct pw_avl *avl, void *data);
void pw_avl_remove(struct pw_avl *avl, void *data);
void pw_avl_foreach(struct pw_avl *avl, pw_avl_foreach_cb cb, void *ctx, void *ctx2);
void pw_avl_print(struct pw_avl *avl);

#endif /* PW_AVL_H */
