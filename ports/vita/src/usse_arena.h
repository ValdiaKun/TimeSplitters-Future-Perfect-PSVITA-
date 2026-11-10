/* SPDX-License-Identifier: GPL-3.0-only
 * Fixed-capacity bump arena for GXM shader-patcher USSE code memory.
 *
 * Portable and host-testable. It never grows, never reuses freed space until
 * every allocation has been released and the arena is explicitly reset, and it
 * fails closed (NULL / -1, state unchanged) on exhaustion, overflow, bad
 * alignment, foreign pointers and double release.
 */
#ifndef TSFP_USSE_ARENA_H
#define TSFP_USSE_ARENA_H

#include <stdint.h>

typedef struct {
    uint8_t *base;         /* start of the mapped block, NULL when uninitialised */
    uint32_t size;         /* capacity in bytes */
    uint32_t offset_base;  /* USSE offset reported by the GXM map call for base */
    uint32_t used;         /* bump cursor */
    uint32_t live;         /* outstanding allocations */
} TsfpUsseArena;

/* Returns 0, or -1 for a NULL argument or zero size. */
int tsfp_usse_arena_init(TsfpUsseArena *arena, void *base, uint32_t size,
                         uint32_t offset_base);

/* align must be a non-zero power of two. On success returns an address aligned
 * to align and stores the matching USSE offset in *usse_offset. */
void *tsfp_usse_arena_alloc(TsfpUsseArena *arena, uint32_t size,
                            uint32_t align, uint32_t *usse_offset);

/* Releases one allocation. Returns -1 for a pointer outside the arena or when
 * nothing is outstanding (double release). */
int tsfp_usse_arena_release(TsfpUsseArena *arena, const void *pointer);

/* Rewinds the cursor. Returns -1 while any allocation is still outstanding. */
int tsfp_usse_arena_reset(TsfpUsseArena *arena);

#endif
