#ifndef RBTREE_INTERNAL_H
#define RBTREE_INTERNAL_H
#include "rbtree.h"

#include <stddef.h>

/* Test-only view of a pooled tree's node pool (never part of rbtree.h).
 * Returns 0 and fills the pool_stats outputs plus the pool's objs_per_slab;
 * returns -1, outputs untouched, for a plain (rb_create) tree. */
int rb_tree_pool_stats(const rbtree_t *t, size_t *slabs, size_t *live,
		       size_t *free_objs, size_t *objs_per_slab);

/* Test-only: one node's key and links, as raw pointers. */
typedef struct {
	const void *self, *left, *right, *parent;
	const char *key;
} rb_node_shape_t;

/* Test-only: preorder shape of every node reachable from the root. nil is
 * recorded as the sentinel's own address, so a NULL link stays
 * distinguishable from nil. Never descends into nil or NULL. Returns the
 * node count, or cap + 1 if more than cap nodes are reachable (which is how
 * a cycle from a leftover thread shows up). */
size_t rb_tree_shape(const rbtree_t *t, rb_node_shape_t *out, size_t cap);

#endif
