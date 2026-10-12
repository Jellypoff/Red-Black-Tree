/* Mutation 1: allocation-failure sweep over a fixed, deterministic scenario.
 * For k = 1, 2, ... the k-th rb_malloc call is made to fail; every failed
 * operation must leave the tree unchanged and the caller's value unconsumed.
 * Usage: test_fault */
#include "fault_alloc.h"
#include "rbtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Always-on check: unlike assert(), -DNDEBUG cannot disable it. Never put
 * side effects inside; capture results first. */
#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n",         \
				__FILE__, __LINE__, #cond);                  \
			exit(1);                                             \
		}                                                            \
	} while (0)

#define N_KEYS 200
#define N_OVERWRITES 50
#define N_DELETES 100
/* gcd(73, N_KEYS) == 1, so i -> (i * 73) % N_KEYS is a bijection. */
#define PERM_STEP 73
/* Most rb_malloc calls one rb_insert makes (node + key copy). A failed
 * insert's calls still count toward the total and its retry repeats them, so
 * an injected run may exceed the unarmed baseline by at most this much. */
#define MAX_ALLOCS_PER_INSERT 2
/* 200 inserts + 50 overwrites; freed as 50 overwritten + 100 deleted + 100
 * destroyed. */
#define VALUES_HANDED_TO_TREE (N_KEYS + N_OVERWRITES)

typedef rbtree_t *(*tree_ctor_fn)(rb_value_free_fn value_free);

typedef struct {
	int present[N_KEYS];
	int value_id[N_KEYS];
	size_t count;
} model_t;

typedef struct {
	int create_failed;
	int insert_failed;
} scen_result_t;

/* "k" + any int + NUL fits in 16, so -Wformat-truncation stays quiet even
 * when the sanitizer build loses the i < N_KEYS range. */
static char keys[N_KEYS][16];
static int free_count = 0;
static int next_id = 0;

static void count_free(void *value)
{
	free_count++;
	free(value);
}

/* Test values use plain malloc, never rb_malloc, so they never consume an
 * injection point or show up in fault_alloc_total(). */
static int *make_int(int n)
{
	int *p = malloc(sizeof *p);
	CHECK(p != NULL);
	*p = n;
	return p;
}

static void build_keys(void)
{
	for (int i = 0; i < N_KEYS; i++) {
		snprintf(keys[i], sizeof keys[i], "k%03d", i);
	}
}

static int perm(int i)
{
	return (i * PERM_STEP) % N_KEYS;
}

/* Key idx is absent, or present with the value id the model recorded. */
static void check_key(const rbtree_t *t, const model_t *m, int idx)
{
	int *v = rb_find(t, keys[idx]);
	if (m->present[idx]) {
		CHECK(v != NULL);
		CHECK(*v == m->value_id[idx]);
	} else {
		CHECK(v == NULL);
	}
}

static void check_model(const rbtree_t *t, const model_t *m)
{
	int valid = rb_validate(t);
	CHECK(valid == 0);
	size_t size = rb_size(t);
	CHECK(size == m->count);
	/* invariant: keys 0..i-1 all agree with the model */
	for (int i = 0; i < N_KEYS; i++) {
		check_key(t, m, i);
	}
}

/* Shared check after every failed op: valid tree, unchanged size, failed key
 * as before, every model key with its original value, destructor not run. */
static void assert_unchanged(const rbtree_t *t, const model_t *m, int idx,
			     int fc0)
{
	check_model(t, m);
	check_key(t, m, idx);
	CHECK(free_count == fc0);
}

static void do_insert(rbtree_t *t, model_t *m, int idx, scen_result_t *r)
{
	int id = next_id++;
	int *v = make_int(id);
	int was = m->present[idx];
	int fc0 = free_count;

	int rc = rb_insert(t, keys[idx], v);
	if (rc == -1) {
		r->insert_failed++;
		CHECK(*v == id); /* value intact: ownership never moved */
		assert_unchanged(t, m, idx, fc0);
		/* the injector is one-shot, so retrying the same value succeeds */
		rc = rb_insert(t, keys[idx], v);
	}
	CHECK(rc == 0);
	CHECK(free_count == fc0 + (was ? 1 : 0));

	if (!was) {
		m->count++;
	}
	m->present[idx] = 1;
	m->value_id[idx] = id;

	size_t size = rb_size(t);
	CHECK(size == m->count);
	void *found = rb_find(t, keys[idx]);
	CHECK(found == v);
}

static void do_delete(rbtree_t *t, model_t *m, int idx)
{
	int was = m->present[idx];
	int fc0 = free_count;

	int rc = rb_delete(t, keys[idx]);
	CHECK(rc == (was ? 0 : -1));
	CHECK(free_count == fc0 + (was ? 1 : 0));

	if (was) {
		m->present[idx] = 0;
		m->count--;
	}
	check_key(t, m, idx);
}

/* Fixed scenario; ctor lets M2 rerun it with rb_create_pooled. Resets all
 * test state first so every run is independent of the previous one. */
static void run_scenario(tree_ctor_fn ctor, scen_result_t *r)
{
	model_t m;
	memset(&m, 0, sizeof m);
	free_count = 0;
	next_id = 0;
	r->create_failed = 0;
	r->insert_failed = 0;

	rbtree_t *t = ctor(count_free);
	if (t == NULL) {
		r->create_failed = 1;
		rb_destroy(NULL); /* must be safe */
		CHECK(free_count == 0);
		return;
	}
	check_model(t, &m);

	/* invariant: the tree agrees with the model after each op */
	for (int i = 0; i < N_KEYS; i++) {
		do_insert(t, &m, perm(i), r);
	}
	for (int j = 0; j < N_OVERWRITES; j++) {
		do_insert(t, &m, perm(4 * j), r);
	}
	for (int j = 0; j < N_DELETES; j++) {
		do_delete(t, &m, perm(2 * j + 1));
	}

	check_model(t, &m);
	rb_destroy(t);
	CHECK(free_count == VALUES_HANDED_TO_TREE);
}

static void sweep(tree_ctor_fn ctor, const char *name)
{
	scen_result_t r;

	long before = fault_alloc_total();
	run_scenario(ctor, &r);
	long baseline = fault_alloc_total() - before;
	CHECK(r.create_failed == 0 && r.insert_failed == 0);

	/* injected = number of k with ran >= k, i.e. the k-th call happened */
	long injected = 0;
	long create_failures = 0;
	long insert_failures = 0;

	/* invariant: injection points 1..k-1 all fired and passed their checks.
	 * Terminates: ran <= baseline + MAX_ALLOCS_PER_INSERT, so ran < k by
	 * then; measure is baseline + MAX_ALLOCS_PER_INSERT + 1 - k. */
	for (long k = 1;; k++) {
		before = fault_alloc_total();
		fault_alloc_arm(k);
		run_scenario(ctor, &r);
		long ran = fault_alloc_total() - before;
		fault_alloc_disarm();

		CHECK(ran <= baseline + MAX_ALLOCS_PER_INSERT);
		if (ran < k) {
			CHECK(r.create_failed + r.insert_failed == 0);
			break;
		}
		injected++;
		/* exactly one op observed the failure; none was swallowed */
		CHECK(r.create_failed + r.insert_failed == 1);
		create_failures += r.create_failed;
		insert_failures += r.insert_failed;
	}

	printf("%s: baseline=%ld injected=%ld create_failures=%ld "
	       "insert_failures=%ld\n",
	       name, baseline, injected, create_failures, insert_failures);
	if (create_failures < 1 || insert_failures < 1) {
		fprintf(stderr, "%s: no failed rb_create and rb_insert reached; "
			"src/rbtree.c does not route allocation through "
			"rb_malloc yet\n", name);
	}
	CHECK(create_failures >= 1 && insert_failures >= 1);
}

int main(void)
{
	build_keys();
	sweep(rb_create, "rb_create");
	printf("All fault sweeps passed\n");
	return 0;
}
