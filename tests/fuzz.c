/* Random inserts/finds/deletes checked against a reference model.
 * Usage: fuzz <op-count> [seed] [pooled] */
#include "rbtree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POOL_SIZE 2000
#define VALIDATE_EVERY 32

static char pool_keys[POOL_SIZE][12];
static int model_present[POOL_SIZE];
static int model_value[POOL_SIZE];

static void build_pool(void)
{
	for (int i = 0; i < POOL_SIZE; i++) {
		snprintf(pool_keys[i], sizeof pool_keys[i], "%d", i);
	}
}

typedef struct {
	const char *last_key;
	int count;
	int ok;
} inorder_check_ctx_t;

static void inorder_check_cb(const char *key, void *value, void *ctx)
{
	(void)value;
	inorder_check_ctx_t *c = ctx;
	if (c->last_key != NULL && strcmp(c->last_key, key) >= 0) {
		c->ok = 0;
	}
	c->last_key = key;
	c->count++;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <op-count> [seed] [pooled]\n", argv[0]);
		return 1;
	}
	long ops = atol(argv[1]);
	unsigned int seed = (argc >= 3) ? (unsigned int)strtoul(argv[2], NULL, 10)
					 : (unsigned int)time(NULL);
	int pooled = argc >= 4 && strcmp(argv[3], "pooled") == 0;
	const char *mode = pooled ? "rb_create_pooled" : "rb_create";
	printf("fuzz seed: %u (%s)\n", seed, mode);
	srand(seed);

	build_pool();

	rbtree_t *t = pooled ? rb_create_pooled(free) : rb_create(free);
	assert(t != NULL);

	/* invariant: after each op, the tree agrees with model_present/model_value
	 * for every pool key touched so far */
	for (long op = 0; op < ops; op++) {
		int i = rand() % POOL_SIZE;
		int op_kind = rand() % 3;

		if (op_kind == 0) {
			int v = rand();
			int *heap_v = malloc(sizeof *heap_v);
			assert(heap_v != NULL);
			*heap_v = v;
			assert(rb_insert(t, pool_keys[i], heap_v) == 0);
			model_present[i] = 1;
			model_value[i] = v;
		} else if (op_kind == 1) {
			void *found = rb_find(t, pool_keys[i]);
			if (model_present[i]) {
				assert(found != NULL);
				assert(*(int *)found == model_value[i]);
			} else {
				assert(found == NULL);
			}
		} else {
			int rc = rb_delete(t, pool_keys[i]);
			if (model_present[i]) {
				assert(rc == 0);
				model_present[i] = 0;
			} else {
				assert(rc == -1);
			}
		}

		if (op % VALIDATE_EVERY == 0) {
			assert(rb_validate(t) == 0);
		}
	}

	assert(rb_validate(t) == 0);

	size_t expected_size = 0;
	for (int i = 0; i < POOL_SIZE; i++) {
		void *found = rb_find(t, pool_keys[i]);
		if (model_present[i]) {
			assert(found != NULL);
			assert(*(int *)found == model_value[i]);
			expected_size++;
		} else {
			assert(found == NULL);
		}
	}
	assert(rb_size(t) == expected_size);

	inorder_check_ctx_t ctx = { .last_key = NULL, .count = 0, .ok = 1 };
	rb_foreach(t, inorder_check_cb, &ctx);
	assert(ctx.ok);
	assert((size_t)ctx.count == expected_size);

	rb_destroy(t);
	printf("fuzz (%s): %ld ops, %zu final keys, all checks passed\n", mode,
	       ops, expected_size);
	return 0;
}
