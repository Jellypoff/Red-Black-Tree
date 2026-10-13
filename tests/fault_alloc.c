/* Mutation 1: fault-injecting allocator behind the rb_malloc/rb_free seam.
 * Arming is one-shot: exactly one future call fails, then the injector
 * disarms itself so retries and the rest of a scenario run normally. */
#include "fault_alloc.h"

#include <stdlib.h>

static long total = 0;     /* cumulative rb_malloc calls; never reset */
static int armed = 0;
static long countdown = 0; /* calls remaining until the injected failure */
static long live = 0;      /* successful rb_malloc minus rb_free(non-NULL) */

void *rb_malloc(size_t n)
{
	total++;
	if (armed && --countdown == 0) {
		armed = 0;
		return NULL;
	}
	void *p = malloc(n);
	if (p != NULL) {
		live++;
	}
	return p;
}

void rb_free(void *p)
{
	if (p != NULL) {
		live--;
	}
	free(p);
}

void fault_alloc_arm(long n)
{
	armed = (n > 0);
	countdown = armed ? n : 0;
}

void fault_alloc_disarm(void)
{
	armed = 0;
	countdown = 0;
}

long fault_alloc_total(void)
{
	return total;
}

long fault_alloc_live(void)
{
	return live;
}
