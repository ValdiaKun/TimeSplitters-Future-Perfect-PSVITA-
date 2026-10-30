/* SPDX-License-Identifier: GPL-3.0-only */
#include "usse_arena.h"

#include <stddef.h>

int tsfp_usse_arena_init(TsfpUsseArena *arena, void *base, uint32_t size,
                         uint32_t offset_base) {
    if (!arena || !base || size == 0u) return -1;
    arena->base = (uint8_t *)base;
    arena->size = size;
    arena->offset_base = offset_base;
    arena->used = 0u;
    arena->live = 0u;
    return 0;
}

void *tsfp_usse_arena_alloc(TsfpUsseArena *arena, uint32_t size,
                            uint32_t align, uint32_t *usse_offset) {
    if (!arena || !arena->base || !usse_offset || size == 0u) return NULL;
    if (align == 0u || (align & (align - 1u)) != 0u) return NULL;

    /* Align the absolute address, not just the offset: the mapped block is
     * only guaranteed to be page aligned. All arithmetic is overflow checked
     * in 64 bits. */
    uint64_t address = (uint64_t)(uintptr_t)arena->base + arena->used;
    uint64_t pad = (align - (address & (align - 1u))) & (align - 1u);
    uint64_t start = (uint64_t)arena->used + pad;
    uint64_t end = start + size;
    if (end > arena->size) return NULL;
    /* The whole allocation, not only its first byte, must stay representable
     * as a 32-bit USSE offset. */
    if ((uint64_t)arena->offset_base + end > UINT32_MAX) return NULL;

    arena->used = (uint32_t)end;
    arena->live += 1u;
    *usse_offset = (uint32_t)((uint64_t)arena->offset_base + start);
    return arena->base + start;
}

int tsfp_usse_arena_release(TsfpUsseArena *arena, const void *pointer) {
    if (!arena || !arena->base || !pointer || arena->live == 0u) return -1;
    const uint8_t *p = (const uint8_t *)pointer;
    if (p < arena->base || p >= arena->base + arena->used) return -1;
    arena->live -= 1u;
    return 0;
}

int tsfp_usse_arena_reset(TsfpUsseArena *arena) {
    if (!arena || !arena->base || arena->live != 0u) return -1;
    arena->used = 0u;
    return 0;
}
