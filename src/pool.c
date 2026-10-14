/* Mutation 2: slab-style pool allocator (4096-byte slabs, intrusive free
 * list).
 *
 * Slab layout (one rb_malloc(POOL_SLAB_SIZE) block):
 *
 *   [slab_hdr | pad to A][slot 0][slot 1] ... [slot k-1][slack < stride]
 *   ^ offset 0           ^ hdr_off            k = objs_per_slab
 *
 * Header: in-band at offset 0, holding only the link that chains slabs so
 * pool_destroy can reach every one. In-band costs header bytes but needs no
 * second allocation per slab, so slab growth has a single failure point.
 * rb_malloc only guarantees malloc alignment, not 4096, so a slot's slab is
 * never found by address masking; nothing needs it, because pool_free goes
 * through the free list, not the slab.
 *
 * Alignment: A = alignof(max_align_t). The slab base is malloc-aligned (a
 * multiple of A), and hdr_off and stride are both rounded up to A, so every
 * slot address is a multiple of A: valid for the stored object and for the
 * free-list pointer written into a dead slot.
 *
 * Slack: the tail bytes after slot k-1 are fewer than one stride and are
 * never handed out.
 *
 * Handing out slots: pool_alloc pops the free list first, then bump-carves
 * the newest slab (carve_next/carve_left), and only when both are empty
 * grows one slab. The first slab is grown lazily, so pool_create makes a
 * single allocation. Slab growth is the only rb_malloc in alloc and its only
 * failure point; it fails before touching the pool, so a NULL return leaves
 * the pool unchanged. No path loops, so alloc and free are O(1).
 *
 * Free list: intrusive. A dead slot's first sizeof(void *) bytes hold the
 * link to the next dead slot (struct free_slot). Slots return LIFO and may
 * wander between slabs.
 *
 * Poisoning (debug builds, i.e. NDEBUG not defined, like pool_check): on
 * free, the whole slot, padding included, is filled with POOL_POISON_BYTE
 * first, and only then is the link written over its front. The link owns
 * the first sizeof(void *) bytes; the poison owns the rest. The other order
 * would overwrite the link and lose the list. ASan sees the slab as one live
 * block, so a stale pointer into a dead slot is invisible to it; poisoning
 * makes such a reader see 0xDD past the link instead of the old node, and a
 * pointer into the pool in the link. Cost is O(stride) per free, a constant
 * of at most one slab, so pool_free stays O(1) in the number of objects.
 *
 * Stats: free_objs = free-list length + slots not carved yet, so
 * live + free_objs == slabs * objs_per_slab after every operation. */
#include "pool.h"

#include "fault_alloc.h"

#include <assert.h>
#include <string.h>

#define POOL_ALIGN alignof(max_align_t)
#define POOL_POISON_BYTE 0xDD

struct slab_hdr {
	struct slab_hdr *next;
};

struct free_slot {
	struct free_slot *next;
};

struct rb_pool {
	pool_geom_t g;
	struct slab_hdr *slabs;   /* every slab, newest first */
	struct free_slot *free;   /* intrusive free list head */
	char *carve_next;         /* next uncarved slot in the newest slab */
	size_t carve_left;        /* uncarved slots left in the newest slab */
	size_t nslabs;
	size_t live;
	size_t nfree;             /* free-list length */
};

/* a must be a power of two; callers keep n far below SIZE_MAX - a. */
static size_t align_up(size_t n, size_t a)
{
	return (n + a - 1) & ~(a - 1);
}

int pool_geometry(size_t obj_size, pool_geom_t *g)
{
	/* reject before rounding, so align_up cannot wrap on huge sizes */
	if (obj_size == 0 || obj_size > POOL_SLAB_SIZE) {
		return -1;
	}

	/* a dead slot must hold the free-list link, so never below a pointer */
	size_t need = obj_size < sizeof(void *) ? sizeof(void *) : obj_size;
	size_t stride = align_up(need, POOL_ALIGN);
	size_t hdr_off = align_up(sizeof(struct slab_hdr), POOL_ALIGN);

	if (hdr_off + stride > POOL_SLAB_SIZE) {
		return -1;
	}

	g->stride = stride;
	g->hdr_off = hdr_off;
	g->objs_per_slab = (POOL_SLAB_SIZE - hdr_off) / stride;
	return 0;
}

/* The spec's pool invariant, checked after every alloc and free. */
static void pool_check(const rb_pool_t *p)
{
	assert(p->live + p->nfree + p->carve_left ==
	       p->nslabs * p->g.objs_per_slab);
	(void)p;
}

static void freelist_push(rb_pool_t *p, void *obj)
{
#ifndef NDEBUG
	/* poison first; the link below then claims the front of the slot */
	memset(obj, POOL_POISON_BYTE, p->g.stride);
#endif
	struct free_slot *s = obj;
	s->next = p->free;
	p->free = s;
	p->nfree++;
}

#ifndef NDEBUG
/* A dead slot is the link plus poison. A byte past the link that is not
 * poison was written through a stale pointer after the free. O(stride). */
static int poison_intact(const rb_pool_t *p, const void *slot)
{
	const unsigned char *b = slot;
	/* invariant: bytes [sizeof(void *), i) are all poison */
	for (size_t i = sizeof(void *); i < p->g.stride; i++) {
		if (b[i] != POOL_POISON_BYTE) {
			return 0;
		}
	}
	return 1;
}
#endif

static void *freelist_pop(rb_pool_t *p)
{
	struct free_slot *s = p->free;
#ifndef NDEBUG
	assert(poison_intact(p, s)); /* write-after-free into this slot */
#endif
	p->free = s->next;
	p->nfree--;
	return s;
}

/* Called only when the newest slab is fully carved. 0, or -1 with the pool
 * untouched. */
static int slab_grow(rb_pool_t *p)
{
	assert(p->carve_left == 0);
	struct slab_hdr *h = rb_malloc(POOL_SLAB_SIZE);
	if (h == NULL) {
		return -1;
	}
	h->next = p->slabs;
	p->slabs = h;
	p->nslabs++;
	p->carve_next = (char *)h + p->g.hdr_off;
	p->carve_left = p->g.objs_per_slab;
	return 0;
}

rb_pool_t *pool_create(size_t obj_size)
{
	pool_geom_t g;
	if (pool_geometry(obj_size, &g) != 0) {
		return NULL;
	}
	rb_pool_t *p = rb_malloc(sizeof *p);
	if (p == NULL) {
		return NULL;
	}
	p->g = g;
	p->slabs = NULL;
	p->free = NULL;
	p->carve_next = NULL;
	p->carve_left = 0;
	p->nslabs = 0;
	p->live = 0;
	p->nfree = 0;
	return p;
}

void *pool_alloc(rb_pool_t *p)
{
	void *obj;
	if (p->free != NULL) {
		obj = freelist_pop(p);
	} else {
		if (p->carve_left == 0 && slab_grow(p) != 0) {
			return NULL;
		}
		obj = p->carve_next;
		p->carve_next += p->g.stride;
		p->carve_left--;
	}
	p->live++;
	pool_check(p);
	return obj;
}

void pool_free(rb_pool_t *p, void *obj)
{
	if (obj == NULL) {
		return;
	}
#ifndef NDEBUG
	/* Cheap O(1) misuse checks; pool_check cannot catch these, because a
	 * free keeps live + free_objs unchanged even when live wraps. Only a
	 * double free of the current free-list head is caught; one deeper in
	 * the list would need an O(n) walk. */
	assert(p->live > 0);      /* nothing is live: double or foreign free */
	assert(obj != p->free);   /* double free of the free-list head */
#endif
	freelist_push(p, obj);
	p->live--;
	pool_check(p);
}

void pool_stats(const rb_pool_t *p,
		size_t *slabs, size_t *live, size_t *free_objs)
{
	*slabs = p->nslabs;
	*live = p->live;
	*free_objs = p->nfree + p->carve_left;
}

void pool_destroy(rb_pool_t *p)
{
	if (p == NULL) {
		return;
	}
	struct slab_hdr *h = p->slabs;
	/* invariant: h heads the chain of slabs not yet freed; live objects
	 * are irrelevant because their storage goes with their slab.
	 * Terminates: each pass frees one slab, so the count of slabs left on
	 * the chain (at most nslabs) strictly decreases to 0. */
	while (h != NULL) {
		struct slab_hdr *next = h->next;
		rb_free(h);
		h = next;
	}
	rb_free(p);
}
