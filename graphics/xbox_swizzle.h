/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_XBOX_SWIZZLE_H
#define TSFP_XBOX_SWIZZLE_H
#include <stddef.h>
#include <stdint.h>

/* Xbox NV2A uncompressed 2D Morton-layout utility. DXT/BC formats must use
 * xbox_bc_layout instead; LIN_* Xbox formats are already linear.
 * The bounded profile is 1..4096 power-of-two pixels per axis and 1, 2 or 4
 * bytes per texel, with no allocation, host-GPU calls or guest pointers. */
#define TSFP_SWIZZLE_MAX_DIMENSION 4096u
enum TsfpSwizzleResult {
    TSFP_SWIZZLE_OK, TSFP_SWIZZLE_INVALID, TSFP_SWIZZLE_RANGE,
    TSFP_SWIZZLE_OVERLAP
};

/* Return a TEXEL index, not a byte offset. *index remains unchanged on error.
 * This method is suitable for test or sparse sampling, not the bulk hot path. */
enum TsfpSwizzleResult tsfp_swizzle_index(uint32_t width, uint32_t height,
    uint32_t x, uint32_t y, size_t *index);

/* Copy packed Xbox Morton texels into a caller-owned, optionally pitched linear
 * destination. Capacities are in BYTES; pitch is bytes between output rows.
 * All arithmetic, spans and overlap are checked before writing anything.
 * Both native buffers must stay readable/writable for their declared spans;
 * source/destination overlap is rejected (in-place conversion unsupported).
 * The conversion is pure CPU data layout; it does NOT flush GPU caches, upload
 * textures, select a Vita texture format or establish GPU correctness. */
enum TsfpSwizzleResult tsfp_unswizzle_2d(void *destination,
    size_t destination_bytes, size_t destination_pitch,
    const void *source, size_t source_bytes,
    uint32_t width, uint32_t height, unsigned bytes_per_pixel);
#endif
