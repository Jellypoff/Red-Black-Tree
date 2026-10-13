#ifndef RBTREE_INTERNAL_H
#define RBTREE_INTERNAL_H
#include "rbtree.h"

#include <stddef.h>

/* Test-only view of a pooled tree's node pool (never part of rbtree.h).
 * Returns 0 and fills the pool_stats outputs plus the pool's objs_per_slab;
 * returns -1, outputs untouched, for a plain (rb_create) tree. */
int rb_tree_pool_stats(const rbtree_t *t, size_t *slabs, size_t *live,
		       size_t *free_objs, size_t *objs_per_slab);

#endif
