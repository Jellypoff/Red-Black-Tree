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

#endif
