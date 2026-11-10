/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "xbox_bc_mips.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static size_t expected_size(unsigned w, unsigned h, unsigned block) {
    size_t x = ((size_t)w + 3u) / 4u, y = ((size_t)h + 3u) / 4u;
    return x * y * block;
}
int main(void) {
    unsigned cases = 0;
    for (unsigned w = 1; w <= 4096; w *= 2)
        for (unsigned h = 1; h <= 4096; h *= 2)
            for (unsigned block = 8; block <= 16; block *= 2) {
                struct TsfpBcMipPlan p;
                memset(&p, 0xa7, sizeof(p));
                assert(tsfp_bc_plan_mips(w,h,block,0,&p) == TSFP_BC_OK);
                size_t offset = 0;
                unsigned want_count = 1;
                for (unsigned d = (w > h ? w : h); d > 1; d >>= 1) ++want_count;
                assert(p.count == want_count);
                for (unsigned i = 0; i < p.count; ++i) {
                    unsigned lw = w >> i, lh = h >> i;
                    if (!lw) lw = 1;
                    if (!lh) lh = 1;
                    assert(p.levels[i].width == lw && p.levels[i].height == lh);
                    assert(p.levels[i].offset == offset);
                    assert(p.levels[i].bytes == expected_size(lw, lh, block));
                    offset += p.levels[i].bytes;
                }
                assert(p.total_bytes == offset);
                struct TsfpBcMipPlan one;
                assert(tsfp_bc_plan_mips(w,h,block,1,&one) == TSFP_BC_OK);
                assert(one.count == 1 && one.total_bytes == expected_size(w,h,block));
                ++cases;
            }
    struct TsfpBcMipPlan a, before;
    memset(&a,0x3d,sizeof(a)); memcpy(&before,&a,sizeof(a));
    assert(tsfp_bc_plan_mips(3,4,8,0,&a) == TSFP_BC_INVALID);
    assert(!memcmp(&a,&before,sizeof(a)));
    assert(tsfp_bc_plan_mips(4,4,8,4,&a) == TSFP_BC_RANGE);
    assert(!memcmp(&a,&before,sizeof(a)));
    assert(tsfp_bc_plan_mips(4,4,8,14,&a) == TSFP_BC_RANGE);
    assert(!memcmp(&a,&before,sizeof(a)));
    assert(tsfp_bc_plan_mips(4,4,8,0,NULL) == TSFP_BC_INVALID);
    assert(tsfp_bc_plan_mips(16,8,8,0,&a) == TSFP_BC_OK);
    assert(a.count == 5 && a.total_bytes == 104);
    puts("bc-mips: asset-free cases=338 partial=pass failures-transactional=pass");
    assert(cases == 338);
    return 0;
}
