#ifndef POOL_H
#define POOL_H
#include <stddef.h>

/* Mutation 2: slab pool internals shared by src/ and the pool unit test. */
#define POOL_SLAB_SIZE 4096

/* Slab layout for one object size: slot i starts at hdr_off + i * stride. */
typedef struct {
	size_t stride;
	size_t hdr_off;
	size_t objs_per_slab;
} pool_geom_t;

/* 0 on success; -1 if obj_size is 0 or no slot fits in one slab. */
int pool_geometry(size_t obj_size, pool_geom_t *g);

typedef struct rb_pool rb_pool_t;
/* slabs are 4096 bytes; NULL on allocation failure or unusable obj_size */
rb_pool_t *pool_create(size_t obj_size);
/* O(1), from a free list; NULL if a new slab is needed and cannot be had */
void *pool_alloc(rb_pool_t *p);
/* O(1), returns to the free list; obj NULL is a no-op */
void pool_free(rb_pool_t *p, void *obj);
/* free_objs counts free-list slots plus slots not carved yet, so
 * live + free_objs == slabs * objs_per_slab always holds */
void pool_stats(const rb_pool_t *p,
		size_t *slabs, size_t *live, size_t *free_objs);
/* releases all slabs, even with live objects; NULL-safe */
void pool_destroy(rb_pool_t *p);

#endif
