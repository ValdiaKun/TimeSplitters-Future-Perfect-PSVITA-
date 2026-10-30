/* SPDX-License-Identifier: GPL-3.0-only
 * Block twiddle implementation adapted from Xita runtime/xv_bc_layout.h,
 * revision 896d6512b6d8c11279de927284e23f86d674bd2d.
 * Copyright (C) 2026 Xita contributors. https://github.com/Xita-Project/xita
 * Original header SHA-256:
 * 5b686bd87f30f7ca179b34b32fefa981ead8ccbe28cdf661bcfacffa18aa0b88
 * Adaptation adds bounded dimensions, checked extents/ranges, ownership checks
 * and transactional validation. See NOTICE and docs/ACCELERATION_AUDIT.md.
 */
#include "xbox_bc_layout.h"
#include <string.h>
static int power_of_two(uint32_t n) { return n && !(n&(n-1)); }
enum TsfpBcResult tsfp_bc_level_size(uint32_t width,uint32_t height,
    unsigned block_bytes,size_t *bytes) {
    if (!bytes || !power_of_two(width) || !power_of_two(height) ||
        width>TSFP_BC_MAX_DIMENSION || height>TSFP_BC_MAX_DIMENSION ||
        (block_bytes!=8 && block_bytes!=16)) return TSFP_BC_INVALID;
    size_t bw=(width+3u)/4u,bh=(height+3u)/4u;
    if (bw>SIZE_MAX/bh || bw*bh>SIZE_MAX/block_bytes) return TSFP_BC_RANGE;
    *bytes=bw*bh*block_bytes; return TSFP_BC_OK;
}
static uint32_t compact(uint32_t x) {
    x&=UINT32_C(0x55555555); x=(x|(x>>1))&UINT32_C(0x33333333);
    x=(x|(x>>2))&UINT32_C(0x0f0f0f0f);
    x=(x|(x>>4))&UINT32_C(0x00ff00ff); return (x|(x>>8))&UINT32_C(0xffff);
}
enum TsfpBcResult tsfp_bc_reorder_range(void *destination,size_t destination_bytes,
    const void *source,size_t source_bytes,uint32_t width,uint32_t height,
    unsigned block_bytes,size_t first,size_t last) {
    size_t bytes; enum TsfpBcResult result=tsfp_bc_level_size(width,height,block_bytes,&bytes);
    if (result) return result;
    if (!destination || !source) return TSFP_BC_INVALID;
    if (source_bytes<bytes || destination_bytes<bytes || first>last || last>bytes/block_bytes)
        return TSFP_BC_RANGE;
    uintptr_t dst=(uintptr_t)destination,src=(uintptr_t)source;
    if (bytes-1>UINTPTR_MAX-dst || bytes-1>UINTPTR_MAX-src) return TSFP_BC_RANGE;
    if (dst<=src ? src-dst<bytes : dst-src<bytes) return TSFP_BC_OVERLAP;
    unsigned bw=(width+3u)/4u,bh=(height+3u)/4u;
    unsigned minimum=bw<bh ? bw : bh,bits=0;
    while ((1u<<bits)<minimum) ++bits;
    for (size_t i=first;i<last;++i) {
        unsigned x=compact((uint32_t)i>>1)&(minimum-1),y=compact((uint32_t)i)&(minimum-1);
        unsigned upper=((unsigned)i>>(2u*bits))<<bits;
        if (bw>=bh) x|=upper; else y|=upper;
        memcpy((unsigned char *)destination+i*block_bytes,
            (const unsigned char *)source+((size_t)y*bw+x)*block_bytes,block_bytes);
    }
    return TSFP_BC_OK;
}
