/* Mutation 2: slab-style pool allocator (4096-byte slabs, intrusive free
 * list). This slice holds only the slab geometry; the allocator follows.
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
 * never handed out. */
#include "pool.h"

#define POOL_ALIGN alignof(max_align_t)

struct slab_hdr {
	struct slab_hdr *next;
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
