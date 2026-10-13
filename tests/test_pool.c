/* Mutation 2: pool unit tests, run against the pool API directly before the
 * tree ever uses it. Usage: test_pool */
#include "fault_alloc.h"
#include "pool.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Allocator tests use the node size, so slab numbers match the tree's. */
#define OBJ 48
#define N_CARVE 256      /* 3 full slabs + 1: forces a 4th slab */
#define N_STRESS 1000
#define N_REFILL 500

typedef struct {
	size_t slabs;
	size_t live;
	size_t free_objs;
} stats_t;

static void *objs[N_STRESS + N_REFILL];

static size_t per_slab(void)
{
	pool_geom_t g;
	int rc = pool_geometry(OBJ, &g);
	CHECK(rc == 0);
	return g.objs_per_slab;
}

static stats_t get_stats(const rb_pool_t *p)
{
	stats_t s;
	pool_stats(p, &s.slabs, &s.live, &s.free_objs);
	return s;
}

/* The spec's pool invariant; called after every pool operation below. */
static stats_t check_inv(const rb_pool_t *p)
{
	stats_t s = get_stats(p);
	CHECK(s.live + s.free_objs == s.slabs * per_slab());
	return s;
}

static int same_stats(stats_t a, stats_t b)
{
	return a.slabs == b.slabs && a.live == b.live &&
	       a.free_objs == b.free_objs;
}

static void test_create_destroy_empty(void)
{
	long live0 = fault_alloc_live();
	long total0 = fault_alloc_total();

	rb_pool_t *p = pool_create(OBJ);
	CHECK(p != NULL);
	/* lazy first slab: creating a pool allocates only the pool itself */
	CHECK(fault_alloc_total() - total0 == 1);
	stats_t s = check_inv(p);
	CHECK(s.slabs == 0 && s.live == 0 && s.free_objs == 0);
	pool_destroy(p);
	CHECK(fault_alloc_live() == live0);
	pool_destroy(NULL); /* must be safe */

	/* unusable sizes are rejected before anything is allocated */
	total0 = fault_alloc_total();
	rb_pool_t *zero = pool_create(0);
	rb_pool_t *huge = pool_create(4081);
	CHECK(zero == NULL && huge == NULL);
	CHECK(fault_alloc_total() == total0);
	printf("test_create_destroy_empty passed\n");
}

static void test_carving(void)
{
	long live0 = fault_alloc_live();
	size_t per = per_slab();
	CHECK(per == 85);
	rb_pool_t *p = pool_create(OBJ);
	CHECK(p != NULL);

	/* invariant: objs[0..i-1] are distinct, aligned, and the pool is
	 * consistent */
	for (int i = 0; i < N_CARVE; i++) {
		objs[i] = pool_alloc(p);
		CHECK(objs[i] != NULL);
		CHECK((uintptr_t)objs[i] % A == 0);
		check_inv(p);
	}
	/* every slot gets its own byte; any overlap clobbers a pattern */
	for (int i = 0; i < N_CARVE; i++) {
		memset(objs[i], i, OBJ);
	}
	/* invariant: slots 0..i-1 still hold exactly their own pattern */
	for (int i = 0; i < N_CARVE; i++) {
		const unsigned char *b = objs[i];
		for (int j = 0; j < OBJ; j++) {
			CHECK(b[j] == (unsigned char)i);
		}
	}
	stats_t s = check_inv(p);
	CHECK(s.slabs == 4);
	CHECK(s.live == N_CARVE);
	CHECK(s.free_objs == 4 * per - N_CARVE);

	pool_destroy(p);
	CHECK(fault_alloc_live() == live0);
	printf("test_carving passed\n");
}

static void test_no_malloc_within_slab(void)
{
	size_t per = per_slab();
	rb_pool_t *p = pool_create(OBJ);
	CHECK(p != NULL);

	long total0 = fault_alloc_total();
	void *first = pool_alloc(p);
	CHECK(first != NULL);
	CHECK(fault_alloc_total() - total0 == 1); /* first slab */

	total0 = fault_alloc_total();
	/* invariant: the slab still has uncarved slots, so no rb_malloc */
	for (size_t i = 1; i < per; i++) {
		void *o = pool_alloc(p);
		CHECK(o != NULL);
	}
	CHECK(fault_alloc_total() == total0);

	void *next = pool_alloc(p);
	CHECK(next != NULL);
	CHECK(fault_alloc_total() - total0 == 1); /* second slab */
	check_inv(p);

	pool_destroy(p);
	printf("test_no_malloc_within_slab passed\n");
}

static void test_lifo_reuse(void)
{
	rb_pool_t *p = pool_create(OBJ);
	CHECK(p != NULL);
	void *a = pool_alloc(p);
	void *b = pool_alloc(p);
	CHECK(a != NULL && b != NULL && a != b);

	pool_free(p, a);
	stats_t s = check_inv(p);
	CHECK(s.live == 1);
	void *c = pool_alloc(p);
	CHECK(c == a); /* the most recently freed slot comes back first */

	stats_t before = check_inv(p);
	pool_free(p, NULL);
	stats_t after = check_inv(p);
	CHECK(same_stats(before, after));
	CHECK(after.slabs == 1 && after.live == 2);

	pool_destroy(p);
	printf("test_lifo_reuse passed\n");
}

/* Companion 4.3: allocate many, free every third, allocate more, destroy
 * with objects still live. */
static void test_stress_and_destroy_live(void)
{
	long live0 = fault_alloc_live();
	rb_pool_t *p = pool_create(OBJ);
	CHECK(p != NULL);

	/* invariant: objs[0..i-1] live, pool consistent */
	for (int i = 0; i < N_STRESS; i++) {
		objs[i] = pool_alloc(p);
		CHECK(objs[i] != NULL);
		check_inv(p);
	}
	/* invariant: every third object below i has been freed */
	for (int i = 0; i < N_STRESS; i += 3) {
		pool_free(p, objs[i]);
		objs[i] = NULL;
		check_inv(p);
	}
	/* invariant: no slab was grown while a free slot was available */
	for (int i = N_STRESS; i < N_STRESS + N_REFILL; i++) {
		stats_t before = get_stats(p);
		objs[i] = pool_alloc(p);
		CHECK(objs[i] != NULL);
		stats_t after = check_inv(p);
		if (before.free_objs > 0) {
			CHECK(after.slabs == before.slabs);
		}
		CHECK(after.live == before.live + 1);
	}

	stats_t s = check_inv(p);
	CHECK(s.live == N_STRESS - (N_STRESS + 2) / 3 + N_REFILL);
	pool_destroy(p); /* objects still live: every slab must go anyway */
	CHECK(fault_alloc_live() == live0);
	printf("test_stress_and_destroy_live passed\n");
}

static void test_slab_failure(void)
{
	long live0 = fault_alloc_live();
	size_t per = per_slab();

	/* create itself can fail: NULL, nothing left behind */
	fault_alloc_arm(1);
	rb_pool_t *none = pool_create(OBJ);
	fault_alloc_disarm();
	CHECK(none == NULL);
	CHECK(fault_alloc_live() == live0);

	rb_pool_t *p = pool_create(OBJ);
	CHECK(p != NULL);

	/* first slab fails on an empty pool */
	fault_alloc_arm(1);
	void *x = pool_alloc(p);
	fault_alloc_disarm();
	CHECK(x == NULL);
	stats_t s = check_inv(p);
	CHECK(s.slabs == 0 && s.live == 0 && s.free_objs == 0);

	/* invariant: i objects carved from the first slab */
	for (size_t i = 0; i < per; i++) {
		void *o = pool_alloc(p);
		CHECK(o != NULL);
	}
	stats_t before = check_inv(p);
	CHECK(before.slabs == 1 && before.free_objs == 0);

	/* growth fails: NULL and the pool is exactly as it was */
	fault_alloc_arm(1);
	void *y = pool_alloc(p);
	fault_alloc_disarm();
	CHECK(y == NULL);
	stats_t after = check_inv(p);
	CHECK(same_stats(before, after));

	/* the injector is one-shot, so the retry grows the slab */
	void *z = pool_alloc(p);
	CHECK(z != NULL);
	s = check_inv(p);
	CHECK(s.slabs == 2 && s.live == per + 1);

	pool_destroy(p);
	CHECK(fault_alloc_live() == live0);
	printf("test_slab_failure passed\n");
}

int main(void)
{
	test_geometry();
	test_create_destroy_empty();
	test_carving();
	test_no_malloc_within_slab();
	test_lifo_reuse();
	test_stress_and_destroy_live();
	test_slab_failure();
	printf("All pool tests passed\n");
	return 0;
}
