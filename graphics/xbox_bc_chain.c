/* SPDX-License-Identifier: GPL-3.0-only */
#include "xbox_bc_chain.h"
#include <stdint.h>
enum TsfpBcResult tsfp_bc_reorder_chain(void *destination, size_t destination_bytes,
    const void *source, size_t source_bytes, const struct TsfpBcMipPlan *plan,
    unsigned block_bytes) {
    if (!destination || !source || !plan || !plan->count ||
        plan->count > TSFP_BC_MAX_MIPS || (block_bytes != 8 && block_bytes != 16))
        return TSFP_BC_INVALID;
    struct TsfpBcMipPlan checked;
    enum TsfpBcResult result = tsfp_bc_plan_mips(plan->levels[0].width,
        plan->levels[0].height, block_bytes, plan->count, &checked);
    if (result != TSFP_BC_OK) return result;
    if (checked.total_bytes != plan->total_bytes || checked.count != plan->count)
        return TSFP_BC_INVALID;
    for (unsigned i=0;i<TSFP_BC_MAX_MIPS;++i) {
        const struct TsfpBcMipLevel *a=&checked.levels[i],*b=&plan->levels[i];
        if (a->width!=b->width || a->height!=b->height ||
            a->offset!=b->offset || a->bytes!=b->bytes) return TSFP_BC_INVALID;
    }
    size_t bytes=checked.total_bytes;
    if (source_bytes<bytes || destination_bytes<bytes) return TSFP_BC_RANGE;
    uintptr_t src=(uintptr_t)source,dst=(uintptr_t)destination;
    if (bytes-1>UINTPTR_MAX-src || bytes-1>UINTPTR_MAX-dst) return TSFP_BC_RANGE;
    if ((src<=dst && dst-src<bytes) || (dst<src && src-dst<bytes))
        return TSFP_BC_OVERLAP;
    for (unsigned i=0;i<checked.count;++i) {
        const struct TsfpBcMipLevel *l=&checked.levels[i];
        result=tsfp_bc_reorder_range((unsigned char*)destination+l->offset,
            l->bytes,(const unsigned char*)source+l->offset,l->bytes,
            l->width,l->height,block_bytes,0,l->bytes/block_bytes);
        if (result!=TSFP_BC_OK) return result;
    }
    return TSFP_BC_OK;
}
