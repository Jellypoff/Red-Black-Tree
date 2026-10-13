#include "rbtree.h"
#include "rbtree_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int free_count = 0;
/* M2: the whole A1 suite runs once per constructor; main switches this. */
static rbtree_t *(*make_tree)(rb_value_free_fn value_free) = rb_create;

static void count_free(void *value)
{
	free_count++;
	free(value);
}

static int *make_int(int n)
{
	int *p = malloc(sizeof *p);
	assert(p != NULL);
	*p = n;
	return p;
}

static void test_create_destroy(void)
{
	rbtree_t *t = make_tree(NULL);
	assert(t != NULL);
	assert(rb_size(t) == 0);
	rb_destroy(t);
	rb_destroy(NULL);
	printf("test_create_destroy passed\n");
}

static void test_insert_find_basic(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	free_count = 0;

	assert(rb_insert(t, "alpha", make_int(1)) == 0);
	assert(rb_insert(t, "beta", make_int(2)) == 0);
	assert(rb_insert(t, "gamma", make_int(3)) == 0);
	assert(rb_insert(t, "delta", make_int(4)) == 0);

	assert(rb_size(t) == 4);
	assert(*(int *)rb_find(t, "alpha") == 1);
	assert(*(int *)rb_find(t, "beta") == 2);
	assert(*(int *)rb_find(t, "gamma") == 3);
	assert(*(int *)rb_find(t, "delta") == 4);

	rb_destroy(t);
	assert(free_count == 4);
	printf("test_insert_find_basic passed\n");
}

static void test_find_missing(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "one", make_int(1)) == 0);
	assert(rb_find(t, "two") == NULL);
	assert(rb_find(t, "") == NULL);

	rb_destroy(t);
	printf("test_find_missing passed\n");
}

static void test_overwrite(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	free_count = 0;

	assert(rb_insert(t, "key", make_int(10)) == 0);
	assert(rb_size(t) == 1);

	assert(rb_insert(t, "key", make_int(20)) == 0);
	assert(rb_size(t) == 1);
	assert(free_count == 1);
	assert(*(int *)rb_find(t, "key") == 20);
	assert(rb_validate(t) == 0);

	/* M3: overwrite-only-key -- repeated overwrites of the sole key in the
	 * tree must keep freeing exactly the old value, never touching size. */
	assert(rb_insert(t, "key", make_int(30)) == 0);
	assert(rb_size(t) == 1);
	assert(free_count == 2);
	assert(*(int *)rb_find(t, "key") == 30);
	assert(rb_validate(t) == 0);

	rb_destroy(t);
	assert(free_count == 3);
	printf("test_overwrite passed\n");
}

static void test_size_tracking(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	const char *keys[] = { "m", "d", "z", "a", "f", "y" };
	size_t n = sizeof keys / sizeof keys[0];

	/* invariant: after i inserts of distinct keys, rb_size(t) == i */
	for (size_t i = 0; i < n; i++) {
		assert(rb_insert(t, keys[i], make_int((int)i)) == 0);
		assert(rb_size(t) == i + 1);
	}

	assert(rb_insert(t, keys[0], make_int(100)) == 0);
	assert(rb_size(t) == n);

	rb_destroy(t);
	printf("test_size_tracking passed\n");
}

/* Each of the four tests below inserts three keys in an order that forces
 * rb_insert_fixup to rotate the true root. If a rotation forgets to update
 * t->root (the "stale root" bug), the promoted node becomes unreachable from
 * t->root and rb_find silently returns NULL for keys that are still present,
 * even though rb_size still reports the correct count. */

static void assert_three_keys_findable(rbtree_t *t, const char *k0, int v0,
					const char *k1, int v1,
					const char *k2, int v2)
{
	assert(rb_size(t) == 3);
	assert(*(int *)rb_find(t, k0) == v0);
	assert(*(int *)rb_find(t, k1) == v1);
	assert(*(int *)rb_find(t, k2) == v2);
}

static void test_stale_root_left_rotation(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "a", make_int(1)) == 0);
	assert(rb_insert(t, "b", make_int(2)) == 0);
	assert(rb_insert(t, "c", make_int(3)) == 0);

	assert_three_keys_findable(t, "a", 1, "b", 2, "c", 3);

	rb_destroy(t);
	printf("test_stale_root_left_rotation passed\n");
}

static void test_stale_root_right_rotation(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "c", make_int(3)) == 0);
	assert(rb_insert(t, "b", make_int(2)) == 0);
	assert(rb_insert(t, "a", make_int(1)) == 0);

	assert_three_keys_findable(t, "a", 1, "b", 2, "c", 3);

	rb_destroy(t);
	printf("test_stale_root_right_rotation passed\n");
}

static void test_stale_root_left_right_zigzag(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "c", make_int(3)) == 0);
	assert(rb_insert(t, "a", make_int(1)) == 0);
	assert(rb_insert(t, "b", make_int(2)) == 0);

	assert_three_keys_findable(t, "a", 1, "b", 2, "c", 3);

	rb_destroy(t);
	printf("test_stale_root_left_right_zigzag passed\n");
}

static void test_stale_root_right_left_zigzag(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "a", make_int(1)) == 0);
	assert(rb_insert(t, "c", make_int(3)) == 0);
	assert(rb_insert(t, "b", make_int(2)) == 0);

	assert_three_keys_findable(t, "a", 1, "b", 2, "c", 3);

	rb_destroy(t);
	printf("test_stale_root_right_left_zigzag passed\n");
}

static void test_validate_empty_tree(void)
{
	rbtree_t *t = make_tree(NULL);
	assert(t != NULL);

	assert(rb_validate(t) == 0);

	rb_destroy(t);
	printf("test_validate_empty_tree passed\n");
}

static void test_validate_single_insert(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "only", make_int(1)) == 0);
	assert(rb_validate(t) == 0);

	rb_destroy(t);
	printf("test_validate_single_insert passed\n");
}

static void test_validate_after_rotations(void)
{
	static const char *sequences[][3] = {
		{ "a", "b", "c" },
		{ "c", "b", "a" },
		{ "c", "a", "b" },
		{ "a", "c", "b" },
	};

	/* invariant: every sequence inserted so far has left the tree valid */
	for (size_t s = 0; s < sizeof sequences / sizeof sequences[0]; s++) {
		rbtree_t *t = make_tree(count_free);
		assert(t != NULL);

		for (size_t i = 0; i < 3; i++) {
			assert(rb_insert(t, sequences[s][i], make_int((int)i)) == 0);
			assert(rb_validate(t) == 0);
		}

		rb_destroy(t);
	}
	printf("test_validate_after_rotations passed\n");
}

static void test_validate_ascending_run(void)
{
	static const char *keys[] = { "a", "b", "c", "d", "e", "f", "g",
				       "h", "i", "j", "k", "l", "m" };
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	/* invariant: after each insert of a strictly increasing key, the
	 * tree is still a valid red-black tree */
	for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
		assert(rb_insert(t, keys[i], make_int((int)i)) == 0);
		assert(rb_validate(t) == 0);
	}

	rb_destroy(t);
	printf("test_validate_ascending_run passed\n");
}

static void count_keys_cb(const char *key, void *value, void *ctx)
{
	(void)key;
	(void)value;
	(*(int *)ctx)++;
}

static void test_foreach_empty(void)
{
	rbtree_t *t = make_tree(NULL);
	assert(t != NULL);

	int count = 0;
	rb_foreach(t, count_keys_cb, &count);
	assert(count == 0);

	rb_destroy(t);
	printf("test_foreach_empty passed\n");
}

typedef struct {
	const char *keys[16];
	int count;
} foreach_keys_ctx_t;

static void collect_keys_cb(const char *key, void *value, void *ctx)
{
	(void)value;
	foreach_keys_ctx_t *c = ctx;
	c->keys[c->count++] = key;
}

static void test_foreach_inorder(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	const char *keys[] = { "m", "d", "z", "a", "f", "y", "b" };
	size_t n = sizeof keys / sizeof keys[0];
	for (size_t i = 0; i < n; i++) {
		assert(rb_insert(t, keys[i], make_int((int)i)) == 0);
	}

	foreach_keys_ctx_t ctx = { .count = 0 };
	rb_foreach(t, collect_keys_cb, &ctx);

	assert((size_t)ctx.count == n);
	assert((size_t)ctx.count == rb_size(t));
	/* invariant: keys collected so far are in strictly increasing order */
	for (int i = 1; i < ctx.count; i++) {
		assert(strcmp(ctx.keys[i - 1], ctx.keys[i]) < 0);
	}

	rb_destroy(t);
	printf("test_foreach_inorder passed\n");
}

typedef struct {
	const char *key;
	int value;
} kv_pair_t;

typedef struct {
	kv_pair_t pairs[16];
	int count;
} foreach_kv_ctx_t;

static void collect_kv_cb(const char *key, void *value, void *ctx)
{
	foreach_kv_ctx_t *c = ctx;
	c->pairs[c->count].key = key;
	c->pairs[c->count].value = *(int *)value;
	c->count++;
}

static void test_foreach_values(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_insert(t, "beta", make_int(20)) == 0);
	assert(rb_insert(t, "alpha", make_int(10)) == 0);
	assert(rb_insert(t, "gamma", make_int(30)) == 0);

	foreach_kv_ctx_t ctx = { .count = 0 };
	rb_foreach(t, collect_kv_cb, &ctx);

	assert(ctx.count == 3);
	assert(strcmp(ctx.pairs[0].key, "alpha") == 0 && ctx.pairs[0].value == 10);
	assert(strcmp(ctx.pairs[1].key, "beta") == 0 && ctx.pairs[1].value == 20);
	assert(strcmp(ctx.pairs[2].key, "gamma") == 0 && ctx.pairs[2].value == 30);

	rb_destroy(t);
	printf("test_foreach_values passed\n");
}

/* M3 tests share one deep-enough tree: SHAPE_KEYS keys "k000".."k199",
 * inserted in a scrambled order (37 is coprime to 200). */
#define SHAPE_KEYS 200

static void insert_scrambled(rbtree_t *t)
{
	char key[16];
	for (int i = 0; i < SHAPE_KEYS; i++) {
		int k = (i * 37) % SHAPE_KEYS;
		snprintf(key, sizeof key, "k%03d", k);
		assert(rb_insert(t, key, make_int(k)) == 0);
	}
}

typedef struct {
	uintptr_t first; /* address of the callback's local on the first call */
	int calls;
	int mismatches;
} stack_addr_ctx_t;

/* Exempt from ASan: its fake stacks give every call's locals a fresh
 * address, which would fail this check for reasons unrelated to rb_foreach.
 * noinline keeps marker in this function's own frame. */
__attribute__((no_sanitize_address, noinline))
static void stack_addr_cb(const char *key, void *value, void *ctx)
{
	(void)key;
	(void)value;
	volatile char marker = 0;
	stack_addr_ctx_t *c = ctx;
	uintptr_t here = (uintptr_t)&marker;
	if (c->calls++ == 0) {
		c->first = here;
	} else if (here != c->first) {
		c->mismatches++;
	}
}

/* A non-recursive rb_foreach calls fn from one frame, so a local in fn sits
 * at the same address on every call; a recursive one calls it from frames
 * at different depths. */
static void test_foreach_constant_stack(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	insert_scrambled(t);

	stack_addr_ctx_t ctx = { 0 };
	rb_foreach(t, stack_addr_cb, &ctx);
	assert(ctx.calls == SHAPE_KEYS);
	assert(ctx.mismatches == 0);

	rb_destroy(t);
	printf("test_foreach_constant_stack passed\n");
}

#define SHAPE_CAP 256

typedef struct {
	const char *keys[SHAPE_CAP];
	int count;
} foreach_many_ctx_t;

static void collect_many_cb(const char *key, void *value, void *ctx)
{
	(void)value;
	foreach_many_ctx_t *c = ctx;
	assert(c->count < SHAPE_CAP);
	c->keys[c->count++] = key;
}

/* Morris restoration check. The shape compare runs first, straight after one
 * rb_foreach, so a leftover thread or a NULL link fails here instead of
 * crashing a later walk. */
static void check_foreach_restores(rbtree_t *t)
{
	static rb_node_shape_t before[SHAPE_CAP], after[SHAPE_CAP];
	size_t n = rb_size(t);
	size_t nb = rb_tree_shape(t, before, SHAPE_CAP);
	assert(nb == n);

	int count = 0;
	rb_foreach(t, count_keys_cb, &count);
	size_t na = rb_tree_shape(t, after, SHAPE_CAP);
	assert(na == nb && memcmp(before, after, nb * sizeof before[0]) == 0);
	assert((size_t)count == n);

	static foreach_many_ctx_t w1, w2;
	w1.count = 0;
	w2.count = 0;
	rb_foreach(t, collect_many_cb, &w1);
	rb_foreach(t, collect_many_cb, &w2);
	assert(w1.count == w2.count && (size_t)w1.count == n);
	/* invariant: walks agree on keys [0, i) and those keys are increasing */
	for (int i = 0; i < w1.count; i++) {
		assert(strcmp(w1.keys[i], w2.keys[i]) == 0);
		assert(i == 0 || strcmp(w1.keys[i - 1], w1.keys[i]) < 0);
	}

	assert(rb_validate(t) == 0);
	assert(rb_size(t) == n);
	for (int i = 0; i < w1.count; i++) {
		assert(rb_find(t, w1.keys[i]) != NULL);
	}
	assert(rb_find(t, "absent") == NULL);
}

static void test_foreach_restores_tree(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	check_foreach_restores(t); /* empty */
	assert(rb_insert(t, "only", make_int(1)) == 0);
	check_foreach_restores(t); /* one node */
	rb_destroy(t);

	t = make_tree(count_free);
	assert(t != NULL);
	insert_scrambled(t);
	check_foreach_restores(t);
	rb_destroy(t);

	t = make_tree(count_free);
	assert(t != NULL);
	char key[16];
	for (int i = 0; i < SHAPE_KEYS; i++) {
		snprintf(key, sizeof key, "k%03d", i);
		assert(rb_insert(t, key, make_int(i)) == 0);
	}
	check_foreach_restores(t); /* ascending inserts */
	rb_destroy(t);

	t = make_tree(count_free);
	assert(t != NULL);
	insert_scrambled(t);
	for (int i = 0; i < SHAPE_KEYS; i += 3) {
		snprintf(key, sizeof key, "k%03d", i);
		assert(rb_delete(t, key) == 0);
	}
	static rb_node_shape_t root_probe[SHAPE_CAP];
	assert(rb_tree_shape(t, root_probe, SHAPE_CAP) == rb_size(t));
	snprintf(key, sizeof key, "%s", root_probe[0].key); /* preorder: root first */
	assert(rb_delete(t, key) == 0);
	assert(rb_validate(t) == 0);
	check_foreach_restores(t); /* after deletes, including the root */
	rb_destroy(t);

	printf("test_foreach_restores_tree passed\n");
}

static void bump_value_cb(const char *key, void *value, void *ctx)
{
	(void)key;
	(void)ctx;
	(*(int *)value)++;
}

/* Contract: the callback may modify *value (but not call rb_* on the tree). */
static void test_foreach_callback_may_write_value(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	insert_scrambled(t);

	rb_foreach(t, bump_value_cb, NULL);
	char key[16];
	for (int i = 0; i < SHAPE_KEYS; i++) {
		snprintf(key, sizeof key, "k%03d", i);
		int *v = rb_find(t, key);
		assert(v != NULL && *v == i + 1);
	}

	rb_destroy(t);
	printf("test_foreach_callback_may_write_value passed\n");
}

typedef struct {
	const char *name;
	const char *insert_order[8];
	size_t insert_count;
	const char *delete_key;
} delete_case_t;

static const delete_case_t delete_cases[] = {
	/* red leaf: no fixup needed at all -- simplest possible delete. */
	{ "red_leaf_left",  { "b", "a", "c" }, 3, "a" },
	{ "red_leaf_right", { "b", "a", "c" }, 3, "c" },

	/* root deletion, trivial: tree becomes empty. */
	{ "root_only_node", { "only" }, 1, "only" },

	/* root deletion + two children, successor is NOT the direct right
	 * child (b's successor is c, found two levels down via d->left). */
	{ "two_children_root_deep_successor",
	  { "a", "b", "c", "d", "e", "f" }, 6, "b" },

	/* two children, successor IS the direct right child (d's successor
	 * is e itself), exercising the y == z->right transplant edge case. */
	{ "two_children_direct_successor",
	  { "a", "b", "c", "d", "e", "f" }, 6, "d" },

	/* black leaf (a) whose sibling (d) is red -- the sibling-red
	 * delete-fixup entry case, and its mirror (sibling on the left). */
	{ "black_leaf_red_sibling",        { "a", "b", "c", "d", "e", "f" }, 6, "a" },
	{ "black_leaf_red_sibling_mirror", { "f", "e", "d", "c", "b", "a" }, 6, "f" },

	/* black node with exactly one red child -- simple splice, and its
	 * mirror (red child on the left instead of the right). */
	{ "black_one_red_child_right",       { "a", "b", "c", "d", "e", "f" }, 6, "e" },
	{ "black_one_red_child_left_mirror", { "f", "e", "d", "c", "b", "a" }, 6, "b" },
};

static void test_delete_cases(void)
{
	size_t n = sizeof delete_cases / sizeof delete_cases[0];

	/* invariant: every case in delete_cases so far has left the tree
	 * valid and correctly sized before moving to the next case */
	for (size_t i = 0; i < n; i++) {
		const delete_case_t *tc = &delete_cases[i];
		rbtree_t *t = make_tree(count_free);
		assert(t != NULL);

		for (size_t k = 0; k < tc->insert_count; k++) {
			assert(rb_insert(t, tc->insert_order[k], make_int((int)k)) == 0);
		}
		assert(rb_validate(t) == 0);

		free_count = 0;
		assert(rb_delete(t, tc->delete_key) == 0);
		assert(free_count == 1);
		assert(rb_size(t) == tc->insert_count - 1);
		assert(rb_validate(t) == 0);
		assert(rb_find(t, tc->delete_key) == NULL);

		for (size_t k = 0; k < tc->insert_count; k++) {
			if (strcmp(tc->insert_order[k], tc->delete_key) == 0) {
				continue;
			}
			assert(rb_find(t, tc->insert_order[k]) != NULL);
		}

		foreach_keys_ctx_t ctx = { .count = 0 };
		rb_foreach(t, collect_keys_cb, &ctx);
		assert((size_t)ctx.count == rb_size(t));
		for (int k = 1; k < ctx.count; k++) {
			assert(strcmp(ctx.keys[k - 1], ctx.keys[k]) < 0);
		}

		rb_destroy(t);
		printf("test_delete_cases[%s] passed\n", tc->name);
	}
}

/* M3 edge cases: empty tree, single-node tree, overwrite-only-key. */

static void test_delete_empty_tree(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_delete(t, "missing") == -1);
	assert(rb_size(t) == 0);
	assert(rb_validate(t) == 0);

	rb_destroy(t);
	printf("test_delete_empty_tree passed\n");
}

static void test_find_empty_tree(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);

	assert(rb_find(t, "missing") == NULL);
	assert(rb_find(t, "") == NULL);

	rb_destroy(t);
	printf("test_find_empty_tree passed\n");
}

static void test_empty_after_deletes(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	free_count = 0;

	const char *keys[] = { "m", "d", "z", "a", "f", "y" };
	size_t n = sizeof keys / sizeof keys[0];
	for (size_t i = 0; i < n; i++) {
		assert(rb_insert(t, keys[i], make_int((int)i)) == 0);
	}
	assert(rb_validate(t) == 0);

	/* delete in an order that isn't reverse-insertion, so the tree passes
	 * through non-trivial shapes on its way back to empty */
	const char *delete_order[] = { "d", "y", "m", "a", "z", "f" };

	/* invariant: after each delete so far, the tree is still valid */
	for (size_t i = 0; i < n; i++) {
		assert(rb_delete(t, delete_order[i]) == 0);
		assert(rb_validate(t) == 0);
	}

	assert(rb_size(t) == 0);
	for (size_t i = 0; i < n; i++) {
		assert(rb_find(t, keys[i]) == NULL);
	}

	int count = 0;
	rb_foreach(t, count_keys_cb, &count);
	assert(count == 0);

	rb_destroy(t);
	assert(free_count == (int)n);
	printf("test_empty_after_deletes passed\n");
}

static void test_single_node_lifecycle(void)
{
	rbtree_t *t = make_tree(count_free);
	assert(t != NULL);
	free_count = 0;

	assert(rb_insert(t, "only", make_int(42)) == 0);
	assert(rb_size(t) == 1);
	assert(*(int *)rb_find(t, "only") == 42);
	assert(rb_validate(t) == 0);

	foreach_kv_ctx_t ctx = { .count = 0 };
	rb_foreach(t, collect_kv_cb, &ctx);
	assert(ctx.count == 1);
	assert(strcmp(ctx.pairs[0].key, "only") == 0 && ctx.pairs[0].value == 42);

	assert(rb_delete(t, "only") == 0);
	assert(free_count == 1);
	assert(rb_size(t) == 0);
	assert(rb_find(t, "only") == NULL);
	assert(rb_validate(t) == 0);

	rb_destroy(t);
	printf("test_single_node_lifecycle passed\n");
}

static void run_suite(const char *ctor_name)
{
	printf("== A1 suite on %s ==\n", ctor_name);
	test_create_destroy();
	test_insert_find_basic();
	test_find_missing();
	test_overwrite();
	test_size_tracking();
	test_stale_root_left_rotation();
	test_stale_root_right_rotation();
	test_stale_root_left_right_zigzag();
	test_stale_root_right_left_zigzag();
	test_validate_empty_tree();
	test_validate_single_insert();
	test_validate_after_rotations();
	test_validate_ascending_run();
	test_foreach_empty();
	test_foreach_inorder();
	test_foreach_values();
	test_foreach_constant_stack();
	test_foreach_restores_tree();
	test_foreach_callback_may_write_value();
	test_delete_cases();
	test_delete_empty_tree();
	test_find_empty_tree();
	test_empty_after_deletes();
	test_single_node_lifecycle();
}

int main(void)
{
	make_tree = rb_create;
	run_suite("rb_create");
	make_tree = rb_create_pooled;
	run_suite("rb_create_pooled");
	printf("All tests passed\n");
	return 0;
}
