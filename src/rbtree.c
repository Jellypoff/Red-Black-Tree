#include "rbtree.h"
#include "fault_alloc.h"
#include "pool.h"
#include "rbtree_internal.h"

#include <stdlib.h>
#include <string.h>

typedef enum { RB_RED, RB_BLACK } rb_color_t;

typedef struct rbnode {
	char *key;
	void *value;
	rb_color_t color;
	struct rbnode *left;
	struct rbnode *right;
	struct rbnode *parent;
} rbnode_t;

struct rbtree {
	rbnode_t *root;
	rbnode_t nil;
	size_t size;
	rb_value_free_fn value_free;
	rb_pool_t *pool; /* node storage for rb_create_pooled; NULL = rb_malloc */
};

/* The only place nodes are allocated: from the pool if the tree has one,
 * else through the seam. Key copies always go through the seam (dup_key). */
static rbnode_t *node_alloc(rbtree_t *t)
{
	return t->pool != NULL ? pool_alloc(t->pool) : rb_malloc(sizeof(rbnode_t));
}

/* Returns a node to wherever node_alloc took it from. */
static void node_release(rbtree_t *t, rbnode_t *node)
{
	if (t->pool != NULL) {
		pool_free(t->pool, node);
	} else {
		rb_free(node);
	}
}

static char *dup_key(const char *key)
{
	size_t len = strlen(key) + 1;
	char *copy = rb_malloc(len);
	if (copy == NULL) {
		return NULL;
	}
	memcpy(copy, key, len);
	return copy;
}

rbtree_t *rb_create(rb_value_free_fn value_free)
{
	rbtree_t *t = rb_malloc(sizeof *t);
	if (t == NULL) {
		return NULL;
	}

	t->nil.key = NULL;
	t->nil.value = NULL;
	t->nil.color = RB_BLACK;
	t->nil.left = &t->nil;
	t->nil.right = &t->nil;
	t->nil.parent = &t->nil;

	t->root = &t->nil;
	t->size = 0;
	t->value_free = value_free;
	t->pool = NULL;
	return t;
}

/* Two allocations, tree then pool (its first slab is grown lazily). If the
 * pool fails, the only thing to undo is the tree block itself. */
rbtree_t *rb_create_pooled(rb_value_free_fn value_free)
{
	rbtree_t *t = rb_create(value_free);
	if (t == NULL) {
		return NULL;
	}
	t->pool = pool_create(sizeof(rbnode_t));
	if (t->pool == NULL) {
		rb_free(t);
		return NULL;
	}
	return t;
}

int rb_tree_pool_stats(const rbtree_t *t, size_t *slabs, size_t *live,
		       size_t *free_objs, size_t *objs_per_slab)
{
	pool_geom_t g;
	if (t->pool == NULL || pool_geometry(sizeof(rbnode_t), &g) != 0) {
		return -1;
	}
	pool_stats(t->pool, slabs, live, free_objs);
	*objs_per_slab = g.objs_per_slab;
	return 0;
}

static void rb_rotate_left(rbtree_t *t, rbnode_t *x)
{
	rbnode_t *y = x->right;

	x->right = y->left;
	if (y->left != &t->nil) {
		y->left->parent = x;
	}

	y->parent = x->parent;
	if (x->parent == &t->nil) {
		t->root = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}

	y->left = x;
	x->parent = y;
}

static void rb_rotate_right(rbtree_t *t, rbnode_t *y)
{
	rbnode_t *x = y->left;

	y->left = x->right;
	if (x->right != &t->nil) {
		x->right->parent = y;
	}

	x->parent = y->parent;
	if (y->parent == &t->nil) {
		t->root = x;
	} else if (y == y->parent->right) {
		y->parent->right = x;
	} else {
		y->parent->left = x;
	}

	x->right = y;
	y->parent = x;
}

static void rb_insert_fixup(rbtree_t *t, rbnode_t *z)
{
	/* invariant: z is red, and z is the only red-red violation in the tree */
	while (z->parent->color == RB_RED) {
		if (z->parent == z->parent->parent->left) {
			rbnode_t *y = z->parent->parent->right;
			if (y->color == RB_RED) {
				z->parent->color = RB_BLACK;
				y->color = RB_BLACK;
				z->parent->parent->color = RB_RED;
				z = z->parent->parent;
			} else {
				if (z == z->parent->right) {
					z = z->parent;
					rb_rotate_left(t, z);
				}
				z->parent->color = RB_BLACK;
				z->parent->parent->color = RB_RED;
				rb_rotate_right(t, z->parent->parent);
			}
		} else {
			rbnode_t *y = z->parent->parent->left;
			if (y->color == RB_RED) {
				z->parent->color = RB_BLACK;
				y->color = RB_BLACK;
				z->parent->parent->color = RB_RED;
				z = z->parent->parent;
			} else {
				if (z == z->parent->left) {
					z = z->parent;
					rb_rotate_right(t, z);
				}
				z->parent->color = RB_BLACK;
				z->parent->parent->color = RB_RED;
				rb_rotate_left(t, z->parent->parent);
			}
		}
	}
	t->root->color = RB_BLACK;
}

int rb_insert(rbtree_t *t, const char *key, void *value)
{
	rbnode_t *parent = &t->nil;
	rbnode_t *cur = t->root;
	int cmp = 0;
	rbnode_t *node;

	/* invariant: cur is the still-unsearched subtree; parent trails one step behind cur */
	while (cur != &t->nil) {
		cmp = strcmp(key, cur->key);
		if (cmp == 0) {
			if (t->value_free != NULL && cur->value != NULL) {
				t->value_free(cur->value);
			}
			cur->value = value;
			return 0;
		}
		parent = cur;
		cur = (cmp < 0) ? cur->left : cur->right;
	}

	node = node_alloc(t);
	if (node == NULL) {
		goto fail;
	}

	node->key = dup_key(key);
	if (node->key == NULL) {
		goto fail_node;
	}

	node->value = value;
	node->color = RB_RED;
	node->left = &t->nil;
	node->right = &t->nil;
	node->parent = parent;

	if (parent == &t->nil) {
		t->root = node;
	} else if (cmp < 0) {
		parent->left = node;
	} else {
		parent->right = node;
	}

	rb_insert_fixup(t, node);
	t->size++;
	return 0;

fail_node:
	node_release(t, node);
fail:
	return -1;
}

void *rb_find(const rbtree_t *t, const char *key)
{
	const rbnode_t *cur = t->root;

	/* invariant: cur is the still-unsearched subtree that may contain key */
	while (cur != &t->nil) {
		int cmp = strcmp(key, cur->key);
		if (cmp == 0) {
			return cur->value;
		}
		cur = (cmp < 0) ? cur->left : cur->right;
	}
	return NULL;
}

static void rb_transplant(rbtree_t *t, rbnode_t *u, rbnode_t *v)
{
	if (u->parent == &t->nil) {
		t->root = v;
	} else if (u == u->parent->left) {
		u->parent->left = v;
	} else {
		u->parent->right = v;
	}
	v->parent = u->parent;
}

static void rb_delete_fixup(rbtree_t *t, rbnode_t *x)
{
	/* invariant: x carries an extra unit of black-height that its subtree's
	 * shape doesn't reflect; each iteration either discharges it (rotation)
	 * or pushes it one level up toward the root */
	while (x != t->root && x->color == RB_BLACK) {
		if (x == x->parent->left) {
			rbnode_t *w = x->parent->right;
			if (w->color == RB_RED) {
				w->color = RB_BLACK;
				x->parent->color = RB_RED;
				rb_rotate_left(t, x->parent);
				w = x->parent->right;
			}
			if (w->left->color == RB_BLACK && w->right->color == RB_BLACK) {
				w->color = RB_RED;
				x = x->parent;
			} else {
				if (w->right->color == RB_BLACK) {
					w->left->color = RB_BLACK;
					w->color = RB_RED;
					rb_rotate_right(t, w);
					w = x->parent->right;
				}
				w->color = x->parent->color;
				x->parent->color = RB_BLACK;
				w->right->color = RB_BLACK;
				rb_rotate_left(t, x->parent);
				x = t->root;
			}
		} else {
			rbnode_t *w = x->parent->left;
			if (w->color == RB_RED) {
				w->color = RB_BLACK;
				x->parent->color = RB_RED;
				rb_rotate_right(t, x->parent);
				w = x->parent->left;
			}
			if (w->right->color == RB_BLACK && w->left->color == RB_BLACK) {
				w->color = RB_RED;
				x = x->parent;
			} else {
				if (w->left->color == RB_BLACK) {
					w->right->color = RB_BLACK;
					w->color = RB_RED;
					rb_rotate_left(t, w);
					w = x->parent->left;
				}
				w->color = x->parent->color;
				x->parent->color = RB_BLACK;
				w->left->color = RB_BLACK;
				rb_rotate_right(t, x->parent);
				x = t->root;
			}
		}
	}
	x->color = RB_BLACK;
}

int rb_delete(rbtree_t *t, const char *key)
{
	rbnode_t *z = t->root;

	/* invariant: z is the still-unsearched subtree that may contain key */
	while (z != &t->nil) {
		int cmp = strcmp(key, z->key);
		if (cmp == 0) {
			break;
		}
		z = (cmp < 0) ? z->left : z->right;
	}
	if (z == &t->nil) {
		return -1;
	}

	rbnode_t *y = z;
	rb_color_t y_original_color = y->color;
	rbnode_t *x;

	if (z->left == &t->nil) {
		x = z->right;
		rb_transplant(t, z, z->right);
	} else if (z->right == &t->nil) {
		x = z->left;
		rb_transplant(t, z, z->left);
	} else {
		y = z->right;
		/* invariant: y is the still-unsearched left spine of z->right */
		while (y->left != &t->nil) {
			y = y->left;
		}
		y_original_color = y->color;
		x = y->right;
		if (y->parent == z) {
			x->parent = y;
		} else {
			rb_transplant(t, y, y->right);
			y->right = z->right;
			y->right->parent = y;
		}
		rb_transplant(t, z, y);
		y->left = z->left;
		y->left->parent = y;
		y->color = z->color;
	}

	if (y_original_color == RB_BLACK) {
		rb_delete_fixup(t, x);
	}

	if (t->value_free != NULL && z->value != NULL) {
		t->value_free(z->value);
	}
	rb_free(z->key);
	node_release(t, z);
	t->size--;
	return 0;
}

size_t rb_size(const rbtree_t *t)
{
	return t->size;
}

/* invariant: subtree at node is a valid RB-tree with all keys in (lo, hi),
 * else return 0; on success *black_height is the subtree's black-height */
static int rb_validate_rec(const rbtree_t *t, const rbnode_t *node,
			    const char *lo, const char *hi, int *black_height)
{
	if (node == &t->nil) {
		*black_height = 0;
		return 1;
	}
	if ((lo != NULL && strcmp(node->key, lo) <= 0) ||
	    (hi != NULL && strcmp(node->key, hi) >= 0)) {
		return 0;
	}
	if (node->color == RB_RED &&
	    (node->left->color == RB_RED || node->right->color == RB_RED)) {
		return 0;
	}
	if ((node->left != &t->nil && node->left->parent != node) ||
	    (node->right != &t->nil && node->right->parent != node)) {
		return 0;
	}

	int bh_left, bh_right;
	if (!rb_validate_rec(t, node->left, lo, node->key, &bh_left)) {
		return 0;
	}
	if (!rb_validate_rec(t, node->right, node->key, hi, &bh_right)) {
		return 0;
	}
	if (bh_left != bh_right) {
		return 0;
	}

	*black_height = bh_left + (node->color == RB_BLACK ? 1 : 0);
	return 1;
}

int rb_validate(const rbtree_t *t)
{
	if (t->root != &t->nil && t->root->color != RB_BLACK) {
		return 1;
	}
	int black_height;
	return rb_validate_rec(t, t->root, NULL, NULL, &black_height) ? 0 : 1;
}

static void rb_foreach_rec(const rbtree_t *t, const rbnode_t *node,
			    void (*fn)(const char *key, void *value, void *ctx),
			    void *ctx)
{
	if (node == &t->nil) {
		return;
	}
	rb_foreach_rec(t, node->left, fn, ctx);
	fn(node->key, node->value, ctx);
	rb_foreach_rec(t, node->right, fn, ctx);
}

void rb_foreach(const rbtree_t *t, void (*fn)(const char *key, void *value, void *ctx),
		 void *ctx)
{
	rb_foreach_rec(t, t->root, fn, ctx);
}

static void free_subtree(rbtree_t *t, rbnode_t *node)
{
	if (node == &t->nil) {
		return;
	}
	free_subtree(t, node->left);
	free_subtree(t, node->right);
	if (t->value_free != NULL && node->value != NULL) {
		t->value_free(node->value);
	}
	rb_free(node->key);
	node_release(t, node);
}

void rb_destroy(rbtree_t *t)
{
	if (t == NULL) {
		return;
	}
	free_subtree(t, t->root);
	pool_destroy(t->pool); /* NULL-safe: plain trees have no pool */
	rb_free(t);
}
