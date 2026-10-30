/* SPDX-License-Identifier: GPL-3.0-only
 * Deterministic host test for the GXM USSE arena. Asset-free, no GPU.
 */
#include "../src/usse_arena.h"

#include <stdint.h>
#include <stdio.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static _Alignas(64) uint8_t storage[256];

int main(void) {
    TsfpUsseArena a;
    uint32_t off = 0xdeadbeefu;

    /* Invalid initialisation. */
    CHECK(tsfp_usse_arena_init(NULL, storage, sizeof storage, 0) == -1);
    CHECK(tsfp_usse_arena_init(&a, NULL, sizeof storage, 0) == -1);
    CHECK(tsfp_usse_arena_init(&a, storage, 0, 0) == -1);
    CHECK(tsfp_usse_arena_alloc(&a, 16, 16, &off) == NULL); /* uninitialised */

    CHECK(tsfp_usse_arena_init(&a, storage, sizeof storage, 0x1000u) == 0);

    /* First allocation starts at the aligned base and reports base offset. */
    void *p0 = tsfp_usse_arena_alloc(&a, 24, 16, &off);
    CHECK(p0 == storage);
    CHECK(off == 0x1000u);

    /* Second allocation is aligned past the first and offsets track pointers. */
    void *p1 = tsfp_usse_arena_alloc(&a, 8, 16, &off);
    CHECK(p1 == storage + 32);
    CHECK(off == 0x1000u + 32u);
    CHECK(((uintptr_t)p1 & 15u) == 0u);
    CHECK(a.live == 2u);

    /* Rejected requests leave the arena untouched. */
    uint32_t used_before = a.used, live_before = a.live;
    CHECK(tsfp_usse_arena_alloc(&a, 0, 16, &off) == NULL);
    CHECK(tsfp_usse_arena_alloc(&a, 8, 0, &off) == NULL);
    CHECK(tsfp_usse_arena_alloc(&a, 8, 24, &off) == NULL);      /* not pow2 */
    CHECK(tsfp_usse_arena_alloc(&a, 8, 16, NULL) == NULL);
    CHECK(tsfp_usse_arena_alloc(&a, UINT32_MAX, 16, &off) == NULL); /* overflow */
    CHECK(tsfp_usse_arena_alloc(&a, 1000, 16, &off) == NULL);   /* exhausted */
    CHECK(a.used == used_before && a.live == live_before);

    /* Exact fill succeeds, one byte more fails. */
    uint32_t remaining = (uint32_t)sizeof storage - a.used;
    void *p2 = tsfp_usse_arena_alloc(&a, remaining, 1, &off);
    CHECK(p2 == storage + 40);
    CHECK(tsfp_usse_arena_alloc(&a, 1, 1, &off) == NULL);

    /* Reset refuses while allocations are outstanding. */
    CHECK(tsfp_usse_arena_reset(&a) == -1);

    /* Foreign and beyond-cursor pointers are rejected. */
    uint8_t other[16];
    CHECK(tsfp_usse_arena_release(&a, other) == -1);
    CHECK(tsfp_usse_arena_release(&a, NULL) == -1);
    CHECK(a.live == 3u);

    CHECK(tsfp_usse_arena_release(&a, p0) == 0);
    CHECK(tsfp_usse_arena_release(&a, p1) == 0);
    CHECK(tsfp_usse_arena_release(&a, p2) == 0);
    CHECK(a.live == 0u);
    /* Double release fails closed. */
    CHECK(tsfp_usse_arena_release(&a, p0) == -1);
    CHECK(a.live == 0u);

    /* Reset is allowed once everything is released and space is reusable. */
    CHECK(tsfp_usse_arena_reset(&a) == 0);
    void *again = tsfp_usse_arena_alloc(&a, 64, 64, &off);
    CHECK(again == storage);
    CHECK(off == 0x1000u);

    /* A block whose base is not aligned to the request is padded by address. */
    TsfpUsseArena skew;
    CHECK(tsfp_usse_arena_init(&skew, storage + 4, 128, 0x20u) == 0);
    void *s = tsfp_usse_arena_alloc(&skew, 16, 16, &off);
    CHECK(s == storage + 16);
    CHECK(((uintptr_t)s & 15u) == 0u);
    CHECK(off == 0x20u + 12u);

    /* USSE offset overflow of 32 bits is rejected. */
    TsfpUsseArena high;
    CHECK(tsfp_usse_arena_init(&high, storage, sizeof storage, UINT32_MAX - 8u) == 0);
    CHECK(tsfp_usse_arena_alloc(&high, 4, 4, &off) != NULL);
    CHECK(tsfp_usse_arena_alloc(&high, 16, 4, &off) == NULL);

    if (failures) {
        printf("usse arena: %d failure(s)\n", failures);
        return 1;
    }
    puts("usse arena: all checks passed");
    return 0;
}
