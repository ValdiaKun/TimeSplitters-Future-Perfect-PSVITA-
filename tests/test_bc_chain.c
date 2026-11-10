/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "xbox_bc_chain.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void positive(unsigned w,unsigned h,unsigned b) {
    struct TsfpBcMipPlan p;
    assert(tsfp_bc_plan_mips(w,h,b,0,&p)==TSFP_BC_OK);
    unsigned char *src=malloc(p.total_bytes),*dst=malloc(p.total_bytes),
                  *reference=malloc(p.total_bytes);
    assert(src && dst && reference);
    for(size_t i=0;i<p.total_bytes;i++) src[i]=(unsigned char)(i*31u+7u);
    memset(dst,0xa7,p.total_bytes); memset(reference,0xa7,p.total_bytes);
    for(unsigned i=0;i<p.count;i++){
        const struct TsfpBcMipLevel *l=&p.levels[i];
        assert(tsfp_bc_reorder_range(reference+l->offset,l->bytes,
            src+l->offset,l->bytes,l->width,l->height,b,0,
            l->bytes/b)==TSFP_BC_OK);
    }
    assert(tsfp_bc_reorder_chain(dst,p.total_bytes,src,p.total_bytes,&p,b)==TSFP_BC_OK);
    assert(!memcmp(dst,reference,p.total_bytes));
    if(p.total_bytes>1) {
        memset(dst,0xa7,p.total_bytes);
        assert(tsfp_bc_reorder_chain(dst,p.total_bytes-1,src,p.total_bytes,&p,b)==TSFP_BC_RANGE);
        for(size_t i=0;i<p.total_bytes;i++) assert(dst[i]==0xa7);
    }
    free(src);free(dst);free(reference);
}
int main(void){
    unsigned cases=0;
    for(unsigned w=1;w<=256;w*=2)
        for(unsigned h=1;h<=256;h*=2)
            for(unsigned b=8;b<=16;b*=2){positive(w,h,b);++cases;}
    struct TsfpBcMipPlan p;
    assert(tsfp_bc_plan_mips(16,8,8,0,&p)==TSFP_BC_OK);
    unsigned char src[160]={0},dst[160],before[160];
    memset(dst,0xa7,sizeof(dst));memcpy(before,dst,sizeof(dst));
    p.levels[2].offset++;
    assert(tsfp_bc_reorder_chain(dst,sizeof(dst),src,sizeof(src),&p,8)==TSFP_BC_INVALID);
    assert(!memcmp(dst,before,sizeof(dst)));
    p.levels[2].offset--;
    assert(tsfp_bc_reorder_chain(src,sizeof(src),src,sizeof(src),&p,8)==TSFP_BC_OVERLAP);
    assert(tsfp_bc_reorder_chain(dst,sizeof(dst),src,sizeof(src),&p,16)==TSFP_BC_INVALID);
    assert(tsfp_bc_reorder_chain(dst,sizeof(dst),src,sizeof(src),NULL,8)==TSFP_BC_INVALID);
    printf("bc-chain: %u dimension-format cases; invalid/overlap rejected\n",cases);
    assert(cases==162);
    return 0;
}
