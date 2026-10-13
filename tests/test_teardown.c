/* M3: rb_foreach then rb_destroy of a large tree inside a thread with a
 * 64 KB stack.
 * Usage: test_teardown [N]   (default 1000000; test, asan, memcheck all use it)
 * Passing this does not prove the absence of recursion (an RB tree of 10^6
 * nodes is ~40 levels deep); the code does. It proves teardown survives a
 * kernel-sized stack at scale with every node, key copy, and value released. */
#include "rbtree.h"
#include "fault_alloc.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMALL_STACK 65536
#define KEY_STRIDE 7919 /* prime: visits keys out of order when N % 7919 != 0 */

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
				__LINE__, #cond);                            \
			exit(1);                                             \
		}                                                            \
	} while (0)

static size_t values_freed;
static char dummy_value; /* every node's value; the destructor only counts */

static void count_value_free(void *value)
{
	CHECK(value == &dummy_value);
	values_freed++;
}

typedef struct {
	size_t visited;
	char prev[32]; /* last key seen; keys must arrive strictly increasing */
} walk_ctx_t;

static void walk_cb(const char *key, void *value, void *ctx)
{
	walk_ctx_t *w = ctx;
	CHECK(value == &dummy_value);
	CHECK(w->visited == 0 || strcmp(w->prev, key) < 0);
	snprintf(w->prev, sizeof w->prev, "%s", key);
	w->visited++;
}

static walk_ctx_t walk;

/* Runs on the 64 KB stack: a full rb_foreach, then rb_destroy. */
static void *small_stack_thread(void *arg)
{
	walk.visited = 0;
	rb_foreach(arg, walk_cb, &walk);
	rb_destroy(arg);
	return NULL;
}

static void run(const char *name, rbtree_t *(*ctor)(rb_value_free_fn), size_t n)
{
	long live_before = fault_alloc_live();
	size_t stride = (n % KEY_STRIDE != 0) ? KEY_STRIDE : 1;
	rbtree_t *t = ctor(count_value_free);
	CHECK(t != NULL);

	char key[32];
	for (size_t i = 0; i < n; i++) {
		snprintf(key, sizeof key, "k%zu", (i * stride) % n);
		CHECK(rb_insert(t, key, &dummy_value) == 0);
	}
	CHECK(rb_size(t) == n);

	pthread_attr_t attr;
	pthread_t tid;
	CHECK(pthread_attr_init(&attr) == 0);
	CHECK(pthread_attr_setstacksize(&attr, SMALL_STACK) == 0);
	values_freed = 0;
	CHECK(pthread_create(&tid, &attr, small_stack_thread, t) == 0);
	CHECK(pthread_join(tid, NULL) == 0);
	pthread_attr_destroy(&attr);

	CHECK(walk.visited == n);
	CHECK(values_freed == n);
	CHECK(fault_alloc_live() == live_before);
	printf("teardown: ctor=%s N=%zu stack=%d foreach+destroy ok\n", name, n,
	       SMALL_STACK);
}

int main(int argc, char **argv)
{
	size_t n = (argc > 1) ? strtoul(argv[1], NULL, 10) : 1000000;
	CHECK(n > 0);
	run("rb_create_pooled", rb_create_pooled, n);
	run("rb_create", rb_create, n);
	return 0;
}
