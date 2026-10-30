/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_XBOX_BC_LAYOUT_H
#define TSFP_XBOX_BC_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
/* Asset-free, single-level Xbox DXT block layout adapter. This profile supports
 * power-of-two 2D dimensions 1..4096, with 8-byte (DXT1) or 16-byte (DXT3/5)
 * blocks. Small mip levels still occupy one whole block. No guest pointers,
 * allocation, GPU mapping, cache maintenance or submission occur here. */
#define TSFP_BC_MAX_DIMENSION 4096u
enum TsfpBcResult { TSFP_BC_OK, TSFP_BC_INVALID, TSFP_BC_RANGE, TSFP_BC_OVERLAP };
/* On failure, *bytes is unchanged. */
enum TsfpBcResult tsfp_bc_level_size(uint32_t width,uint32_t height,
    unsigned block_bytes,size_t *bytes);
/* Convert row-major Xbox blocks to Y-first rectangular GXM twiddle order.
 * first/last address output blocks [first,last). All input spans, arithmetic,
 * ranges and disjoint ownership are checked before any write. Source/destination
 * must be valid native buffers for their supplied capacities and remain stable
 * during conversion. Separate disjoint output ranges can be processed by workers
 * only if source ownership and synchronization are established by their caller.
 * Whole-level source/destination overlap is rejected, including an empty range. */
enum TsfpBcResult tsfp_bc_reorder_range(void *destination,size_t destination_bytes,
    const void *source,size_t source_bytes,uint32_t width,uint32_t height,
    unsigned block_bytes,size_t first,size_t last);
#endif
