/* SPDX-License-Identifier: MIT
 * Copyright(c) 2021 Darek Stojaczyk for pwmirage.com
 */

#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include "avl.h"

struct pw_avl *
pw_avl_init(size_t el_size)
{
	struct pw_avl *avl;

	avl = calloc(1, sizeof(*avl));
	if (!avl) {
		return NULL;
	}

	avl->el_size = el_size;
	return avl;
}

static void
deinit_foreach_cb(struct pw_avl_node *node)
{
	if (node->next) {
		deinit_foreach_cb(node->next);
	}

	if (node->left) {
		deinit_foreach_cb(node->left);
	}

	if (node->right) {
		deinit_foreach_cb(node->right);
	}

	free(node);
}

void
pw_avl_deinit(struct pw_avl *avl)
{
	if (!avl) {
		return;
	}

	/* we need to iterate depth-first to avoid use-after-free,
	 * so pw_avl_foreach() won't work */
	deinit_foreach_cb(avl->root);

	free(avl);
}

void *
pw_avl_alloc(struct pw_avl *avl)
{
	struct pw_avl_node *node;

	node = calloc(1, sizeof(*node) + avl->el_size);
	if (!node) {
		return NULL;
	}

	node->height = 1;
	return (void *)node->data;
}

void
pw_avl_free(struct pw_avl *avl, void *data)
{
	struct pw_avl_node *node = (void *)(data - offsetof(struct pw_avl_node, data));

	free(node);
}

static int
height(struct pw_avl_node *node)
{
	return node ? node->height : 0;
}

static int
max(int a, int b)
{
	return a >= b ? a : b;
}

static void
calc_height(struct pw_avl_node *node)
{
	node->height = 1 + max(height(node->left), height(node->right));
}

static struct pw_avl_node *
rotate_right(struct pw_avl_node *root)
{
	struct pw_avl_node *new_root = root->left;
	struct pw_avl_node *moved = new_root->right;

	new_root->right = root;
	root->left = moved;
	calc_height(root);
	calc_height(new_root);

	return new_root;
}

struct pw_avl_node *
rotate_left(struct pw_avl_node *root)
{
	struct pw_avl_node *new_root = root->right;
	struct pw_avl_node *moved = new_root->left;

	new_root->left = root;
	root->right = moved;
	calc_height(root);
	calc_height(new_root);

	return new_root;
}

static struct pw_avl_node *
insert(struct pw_avl_node *parent, struct pw_avl_node *node)
{
	if (!parent) {
		return node;
	}

	if (node->key < parent->key) {
		parent->left  = insert(parent->left, node);
	} else if (node->key > parent->key) {
		parent->right = insert(parent->right, node);
	} else {
		node->next = parent->next;
		parent->next = node;
		/* no changes to the tree, just return */
		return parent;
	}

	calc_height(parent);

	int balance = height(parent->left) - height(parent->right);

	if (balance > 1) {
		if (node->key > parent->left->key) {
			parent->left = rotate_left(parent->left);
		}
		return rotate_right(parent);
	}

	if (balance < -1) {
		if (node->key < parent->right->key) {
			parent->right = rotate_right(parent->right);
		}
		return rotate_left(parent);
	}

	return parent;

}

void
pw_avl_insert(struct pw_avl *avl, uint64_t key, void *data)
{
	struct pw_avl_node *node = (void *)(data - offsetof(struct pw_avl_node, data));

	node->key = key;
	avl->root = insert(avl->root, node);
	avl->el_count++;
}

static struct pw_avl_node *
remove_node(struct pw_avl_node *parent, struct pw_avl_node *node)
{
	if (!parent) {
		return node;
	}

	if (node->key < parent->key) {
		parent->left  = remove_node(parent->left, node);
	} else if (node->key > parent->key) {
		parent->right = remove_node(parent->right, node);
	} else {
		if (parent->next) {
			if (parent == node) {
				/* make the next entry act as this one, then replace it */
				parent->next->left = parent->left;
				parent->next->right = parent->right;
				parent->next->height = parent->height;
				return parent->next;
			}

			struct pw_avl_node *tmp = parent;
			while (tmp->next) {
				if (tmp->next == node) {
					/* just remove it from the chain */
					tmp->next = tmp->next->next;
					return parent;
				}
				tmp = tmp->next;
			}

			/* node not in the chain -> nothing removed */
			return parent;
		}

		if (parent != node) {
			/* that's not the node we're looking for, despite having
			 * the same key */
			return parent;
		}

		if (!parent->left || !parent->right) {
			/* zero or just one branch present */
			struct pw_avl_node *tmp = parent->left ? parent->left : parent->right;

			if (!tmp) {
				/* perfect case, nothing to do */
				return NULL;
			}

			/* move that one branch up in the tree */
			parent = tmp;
		} else {
			/* both branches present */
			struct pw_avl_node *tmp = parent->right;

			/* parent->right branch is just bigger numbers, so
			 * move that branch up in the tree */
			if (!tmp->left) {
				tmp->left = parent->left;
				parent = tmp;
			} else {
				/* get the smallest number of those bigger than parent
				 * and replace it with parent */
				while (tmp->left) {
					tmp = tmp->left;
				}

				parent->right = remove_node(parent->right, tmp);
				tmp->left = parent->left;
				tmp->right = parent->right;
				tmp->next = parent->next;
				parent = tmp;
			}
		}
	}

	calc_height(parent);

	int balance = height(parent->left) - height(parent->right);
	int balance_left = parent->left ? height(parent->left->left) - height(parent->left->right) : 0;
	int balance_right = parent->right ? height(parent->right->left) - height(parent->right->right) : 0;

	if (balance > 1) {
		if (balance_left < 0) {
			parent->left = rotate_left(parent->left);
		}
		return rotate_right(parent);
	}

	if (balance < -1) {
		if (balance_right > 0) {
			parent->right = rotate_right(parent->right);
		}
		return rotate_left(parent);
	}

	return parent;
}

void
pw_avl_remove(struct pw_avl *avl, void *data)
{
	struct pw_avl_node *node = (void *)(data - offsetof(struct pw_avl_node, data));

	avl->root = remove_node(avl->root, node);
	assert(avl->el_count > 0);
	avl->el_count--;

	/* deinitialize in case it's inserted again */
	node->left = NULL;
	node->right = NULL;
	node->next = NULL;
	node->height = 1;
}

void *
pw_avl_get(struct pw_avl *avl, uint64_t key)
{
	struct pw_avl_node *node = avl->root;

	while (node && node->key != key) {
		if (key > node->key) {
			node = node->right;
		} else {
			node = node->left;
		}
	}

	return node ? (void *)node->data : NULL;
}

void *
pw_avl_get_next(struct pw_avl *avl, void *data)
{
	struct pw_avl_node *node = (void *)(data - offsetof(struct pw_avl_node, data));

	return node->next ? (void *)node->next->data : NULL;
}

static void
foreach_cb(struct pw_avl_node *node, pw_avl_foreach_cb cb, void *ctx1, void *ctx2)
{
	cb(node, ctx1, ctx2);

	if (node->next) {
		foreach_cb(node->next, cb, ctx1, ctx2);
	}

	if (node->left) {
		foreach_cb(node->left, cb, ctx1, ctx2);
	}

	if (node->right) {
		foreach_cb(node->right, cb, ctx1, ctx2);
	}
}

void
pw_avl_foreach(struct pw_avl *avl, pw_avl_foreach_cb cb, void *ctx1, void *ctx2)
{
	if (!avl->root) {
		return;
	}

	foreach_cb(avl->root, cb, ctx1, ctx2);
}

static void
print_node(struct pw_avl_node *node)
{
	fprintf(stderr, "%"PRIu64"\n", node->key);
	if (node->next) {
		fprintf(stderr, "%"PRIu64" next:\n", node->key);
		print_node(node->next);
	}
	if (node->left) {
		fprintf(stderr, "%"PRIu64" left:\n", node->key);
		print_node(node->left);
	}

	if (node->right) {
		fprintf(stderr, "%"PRIu64" right:\n", node->key);
		print_node(node->right);
	}
}

void
pw_avl_print(struct pw_avl *avl)
{
	fprintf(stderr, "pw_avl_print():\n");

	if (avl->root) {
		print_node(avl->root);
	}
}

#ifdef PW_AVL_TEST
int
main(void)
{
	struct pw_avl *avl = pw_avl_init(0);
	int arr[] = { 1, 2, 3, 4, 10, 12, 7, 6, 40, 20, 2, 2, 4 };

	for (int i = 0; i < sizeof(arr) / sizeof(arr[0]); i++) {
		int num = arr[i];

		void *data = pw_avl_alloc(avl);
		pw_avl_insert(avl, num, data);
	}

	pw_avl_print(avl);
	/*       4
	 *      /  \___________,
	 *     2               10
	 *    / \             /  \
	 *   1   3           7   20
	 *                  /   /  \
	 *                 6   12  40
	 */
	return 0;
}
#endif
