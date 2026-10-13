/* Mutation 2: pool unit tests, run against the pool API directly before the
 * tree ever uses it. Usage: test_pool */
#include "pool.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Always-on check: unlike assert(), -DNDEBUG cannot disable it. Never put
 * side effects inside; capture results first. */
#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fflush(stdout);                                      \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n",         \
				__FILE__, __LINE__, #cond);                  \
			exit(1);                                             \
		}                                                            \
	} while (0)

#define A alignof(max_align_t)

/* The exact rows below assume x86-64: 16-byte max alignment, 8-byte
 * pointers, so the in-band header rounds up to 16. */
static_assert(alignof(max_align_t) == 16 && sizeof(void *) == 8,
	      "geometry table assumes x86-64 alignment");

typedef struct {
	size_t obj_size;
	int rc;
	size_t stride;
	size_t hdr_off;
	size_t objs_per_slab;
} geom_case_t;

static const geom_case_t geom_cases[] = {
	{ 0, -1, 0, 0, 0 },          /* no object to store */
	{ 1, 0, 16, 16, 255 },       /* raised to pointer size, then A */
	{ 8, 0, 16, 16, 255 },
	{ 16, 0, 16, 16, 255 },
	{ 17, 0, 32, 16, 127 },      /* 16 B slack at the tail */
	{ 48, 0, 48, 16, 85 },       /* node size: 16 + 85 * 48 == 4096 */
	{ 4080, 0, 4080, 16, 1 },    /* largest object that fits */
	{ 4081, -1, 0, 0, 0 },       /* header + one slot exceeds the slab */
	{ SIZE_MAX, -1, 0, 0, 0 },   /* must not wrap inside align_up */
};

/* Layout properties every accepted geometry must satisfy. */
static void check_layout(size_t obj_size, const pool_geom_t *g)
{
	CHECK(g->stride % A == 0);
	CHECK(g->stride >= obj_size);
	CHECK(g->stride >= sizeof(void *));
	CHECK(g->hdr_off % A == 0);
	CHECK(g->hdr_off >= sizeof(void *));
	CHECK(g->objs_per_slab >= 1);
	size_t used = g->hdr_off + g->objs_per_slab * g->stride;
	CHECK(used <= POOL_SLAB_SIZE);
	/* slack cannot hold another slot, so no carvable slot is wasted */
	CHECK(POOL_SLAB_SIZE - used < g->stride);
}

static void test_geometry(void)
{
	size_t n = sizeof geom_cases / sizeof geom_cases[0];
	/* invariant: rows 0..i-1 matched their expected geometry */
	for (size_t i = 0; i < n; i++) {
		const geom_case_t *c = &geom_cases[i];
		pool_geom_t g = { 0, 0, 0 };
		int rc = pool_geometry(c->obj_size, &g);
		CHECK(rc == c->rc);
		if (rc != 0) {
			continue;
		}
		CHECK(g.stride == c->stride);
		CHECK(g.hdr_off == c->hdr_off);
		CHECK(g.objs_per_slab == c->objs_per_slab);
		check_layout(c->obj_size, &g);
	}
	printf("test_geometry passed (%zu cases)\n", n);
}

int main(void)
{
	test_geometry();
	printf("All pool tests passed\n");
	return 0;
}
