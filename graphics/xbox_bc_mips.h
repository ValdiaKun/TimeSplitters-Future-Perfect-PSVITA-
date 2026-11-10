/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_XBOX_BC_MIPS_H
#define TSFP_XBOX_BC_MIPS_H
#include "xbox_bc_layout.h"
#define TSFP_BC_MAX_MIPS 13u
#define TSFP_BC_MAX_CHAIN_BYTES (32u * 1024u * 1024u)
struct TsfpBcMipLevel {
    uint32_t width;
    uint32_t height;
    size_t offset;
    size_t bytes;
};
struct TsfpBcMipPlan {
    unsigned count;
    size_t total_bytes;
    struct TsfpBcMipLevel levels[TSFP_BC_MAX_MIPS];
};
/* Asset-free contiguous mip extents, not hardware mip layout, alignment or GPU allocation.
 * requested_levels=0 selects the complete chain down to 1x1.
 * On any failure, *out remains byte-for-byte unchanged. */
enum TsfpBcResult tsfp_bc_plan_mips(uint32_t width, uint32_t height,
    unsigned block_bytes, unsigned requested_levels, struct TsfpBcMipPlan *out);
#endif
