#ifndef FAULT_ALLOC_H
#define FAULT_ALLOC_H
#include <stddef.h>

/* Allocation seam: every allocation in src/ goes through these. */
void *rb_malloc(size_t n);
void rb_free(void *p);

/* Make the n-th rb_malloc call after arming (1-based) return NULL.
 * One-shot: the injector disarms itself after firing. n <= 0 disarms. */
void fault_alloc_arm(long n);
/* Cancel any pending injection. */
void fault_alloc_disarm(void);
/* Cumulative rb_malloc calls since program start; never reset. */
long fault_alloc_total(void);
/* Blocks currently held: successful rb_malloc calls minus rb_free calls with
 * a non-NULL pointer. Failed allocations and rb_free(NULL) do not count. */
long fault_alloc_live(void);

#endif
