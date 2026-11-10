/* SPDX-License-Identifier: GPL-3.0-only */
#include "xbox_bc_mips.h"
#include <string.h>

enum TsfpBcResult tsfp_bc_plan_mips(uint32_t width, uint32_t height,
    unsigned block_bytes, unsigned requested_levels, struct TsfpBcMipPlan *out) {
    if (!out) return TSFP_BC_INVALID;
    size_t first_size;
    enum TsfpBcResult result = tsfp_bc_level_size(width, height, block_bytes, &first_size);
    if (result != TSFP_BC_OK) return result;
    unsigned full = 1;
    for (uint32_t w = width, h = height; w > 1 || h > 1; ) {
        if (w > 1) w >>= 1;
        if (h > 1) h >>= 1;
        ++full;
    }
    unsigned count = requested_levels ? requested_levels : full;
    if (count > full || count > TSFP_BC_MAX_MIPS) return TSFP_BC_RANGE;
    struct TsfpBcMipPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.count = count;
    size_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t w = width >> i, h = height >> i;
        if (!w) w = 1;
        if (!h) h = 1;
        size_t bytes = 0;
        result = tsfp_bc_level_size(w, h, block_bytes, &bytes);
        if (result != TSFP_BC_OK) return result;
        if (bytes > TSFP_BC_MAX_CHAIN_BYTES - total) return TSFP_BC_RANGE;
        plan.levels[i].width = w;
        plan.levels[i].height = h;
        plan.levels[i].offset = total;
        plan.levels[i].bytes = bytes;
        total += bytes;
    }
    plan.total_bytes = total;
    memcpy(out, &plan, sizeof(plan));
    return TSFP_BC_OK;
}
