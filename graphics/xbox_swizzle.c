/* SPDX-License-Identifier: GPL-3.0-only
 * Independent bounded NV2A 2D Morton-layout adapter. Algorithm reference:
 * sp00nznet/xboxrecomp src/d3d/d3d8_swizzle.h, and its xemu masked-increment
 * observation; no upstream code copied. See docs/REFERENCE_AUDIT.md. */
#include "xbox_swizzle.h"
#include <string.h>

static int is_pow2(uint32_t value) { return value && !(value & (value - 1u)); }
static int valid_dimensions(uint32_t width, uint32_t height) {
    return is_pow2(width) && is_pow2(height) &&
        width <= TSFP_SWIZZLE_MAX_DIMENSION &&
        height <= TSFP_SWIZZLE_MAX_DIMENSION;
}

enum TsfpSwizzleResult tsfp_swizzle_index(uint32_t width, uint32_t height,
    uint32_t x, uint32_t y, size_t *index) {
    if (!index || !valid_dimensions(width, height)) return TSFP_SWIZZLE_INVALID;
    if (x >= width || y >= height) return TSFP_SWIZZLE_RANGE;
    /* Independently walk each active coordinate's bits, low to high. Once the
     * shorter axis is exhausted, the longer axis gets consecutive index bits. */
    uint32_t number = 0, out_bit = 1u;
    while (width > 1u || height > 1u) {
        if (width > 1u) {
            if (x & 1u) number |= out_bit;
            x >>= 1; width >>= 1; out_bit <<= 1;
        }
        if (height > 1u) {
            if (y & 1u) number |= out_bit;
            y >>= 1; height >>= 1; out_bit <<= 1;
        }
    }
    *index = number;
    return TSFP_SWIZZLE_OK;
}

static void coordinate_masks(uint32_t width, uint32_t height,
    uint32_t *x_mask, uint32_t *y_mask) {
    uint32_t scale = 1u, out_bit = 1u, mx = 0u, my = 0u;
    while (scale < width || scale < height) {
        if (scale < width) { mx |= out_bit; out_bit <<= 1; }
        if (scale < height) { my |= out_bit; out_bit <<= 1; }
        scale <<= 1;
    }
    *x_mask = mx;
    *y_mask = my;
}

static int checked_extent(uintptr_t start, size_t bytes) {
    /* Pointer ranges are inclusive for this overflow check. */
    return bytes && bytes - 1u <= UINTPTR_MAX - start;
}

enum TsfpSwizzleResult tsfp_unswizzle_2d(void *destination,
    size_t destination_bytes, size_t destination_pitch,
    const void *source, size_t source_bytes,
    uint32_t width, uint32_t height, unsigned bytes_per_pixel) {
    if (!destination || !source || !valid_dimensions(width, height) ||
        (bytes_per_pixel != 1 && bytes_per_pixel != 2 && bytes_per_pixel != 4))
        return TSFP_SWIZZLE_INVALID;
    const size_t row_bytes = (size_t)width * bytes_per_pixel;
    const size_t source_required = row_bytes * height;
    if (destination_pitch < row_bytes) return TSFP_SWIZZLE_RANGE;
    if (height > 1 && (destination_pitch > (SIZE_MAX - row_bytes) / (height - 1u)))
        return TSFP_SWIZZLE_RANGE;
    const size_t destination_required = (size_t)(height - 1u) * destination_pitch + row_bytes;
    if (source_bytes < source_required || destination_bytes < destination_required)
        return TSFP_SWIZZLE_RANGE;
    const uintptr_t dst = (uintptr_t)destination, src = (uintptr_t)source;
    if (!checked_extent(dst, destination_required) ||
        !checked_extent(src, source_required)) return TSFP_SWIZZLE_RANGE;
    /* Validate the complete bounding spans, including destination row padding.
     * False-positive overlap is preferable to silent corruption. */
    if (dst <= src ? src - dst < destination_required : dst - src < source_required)
        return TSFP_SWIZZLE_OVERLAP;

    uint32_t x_mask, y_mask;
    coordinate_masks(width, height, &x_mask, &y_mask);
    size_t y_offset = 0u;
    for (uint32_t y = 0; y < height; ++y) {
        size_t x_offset = 0u;
        unsigned char *dst_row = (unsigned char *)destination + (size_t)y * destination_pitch;
        for (uint32_t x = 0; x < width; ++x) {
            /* Masks are disjoint; | forms the Morton texel index. No per-pixel
             * mask generation, unaligned integer loads, or temporary buffer. */
            const size_t src_offset = (x_offset | y_offset) * bytes_per_pixel;
            memcpy(dst_row + (size_t)x * bytes_per_pixel,
                (const unsigned char *)source + src_offset, bytes_per_pixel);
            x_offset = (x_offset - x_mask) & x_mask;
        }
        y_offset = (y_offset - y_mask) & y_mask;
    }
    return TSFP_SWIZZLE_OK;
}
