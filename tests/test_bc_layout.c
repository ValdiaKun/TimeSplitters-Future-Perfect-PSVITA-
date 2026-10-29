/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "xbox_bc_layout.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Independent coordinate-to-index oracle: deposit each Y/X coordinate bit.
 * Production conversion instead extracts coordinates from an output index. */
static size_t encode(unsigned x,unsigned y,unsigned width,unsigned height) {
    size_t index=0; unsigned bit=0;
    for (unsigned scale=1;scale<width || scale<height;scale*=2) {
        if (scale<height) { if (y&scale) index|=(size_t)1<<bit; ++bit; }
        if (scale<width) { if (x&scale) index|=(size_t)1<<bit; ++bit; }
    }
    return index;
}
static void positive(unsigned width,unsigned height,unsigned block) {
    size_t bytes=0; assert(tsfp_bc_level_size(width,height,block,&bytes)==TSFP_BC_OK);
    unsigned bw=(width+3)/4,bh=(height+3)/4;
    assert(bytes==(size_t)bw*bh*block);
    unsigned char *source=malloc(bytes+64),*output=malloc(bytes+64),*expected=malloc(bytes);
    assert(source && output && expected); memset(source,0x39,bytes+64); memset(output,0xa7,bytes+64);
    unsigned char *src=source+31,*dst=output+31; /* Deliberately unaligned. */
    for (size_t i=0;i<bytes;++i) src[i]=(unsigned char)(i*13u+i/256u);
    for (unsigned y=0;y<bh;++y) for (unsigned x=0;x<bw;++x)
        memcpy(expected+encode(x,y,bw,bh)*block,src+((size_t)y*bw+x)*block,block);
    size_t split=(size_t)bw*bh/2;
    assert(tsfp_bc_reorder_range(dst,bytes,src,bytes,width,height,block,0,split)==TSFP_BC_OK);
    assert(!memcmp(dst,expected,split*block));
    for (size_t i=split*block;i<bytes;++i) assert(dst[i]==0xa7);
    assert(tsfp_bc_reorder_range(dst,bytes,src,bytes,width,height,block,split,bytes/block)==TSFP_BC_OK);
    assert(!memcmp(dst,expected,bytes));
    for (size_t i=0;i<31;++i) assert(output[i]==0xa7 && source[i]==0x39);
    for (size_t i=bytes+31;i<bytes+64;++i) assert(output[i]==0xa7 && source[i]==0x39);
    free(expected); free(output); free(source);
}
int main(void) {
    unsigned cases=0;
    for (unsigned width=1;width<=256;width*=2) for (unsigned height=1;height<=256;height*=2)
        for (unsigned block=8;block<=16;block*=2) { positive(width,height,block); ++cases; }
    /* Fixed 4x2 block example is independent of both algorithms. */
    unsigned char src[256],dst[256],before[256];
    memset(src,0,sizeof(src)); memset(dst,0xa7,sizeof(dst)); memcpy(before,dst,sizeof(dst));
    for (unsigned i=0;i<8;++i) memset(src+i*8,(int)i,8);
    const unsigned order[]={0,4,1,5,2,6,3,7};
    assert(tsfp_bc_reorder_range(dst,64,src,64,16,8,8,0,8)==TSFP_BC_OK);
    for (unsigned i=0;i<8;++i) for (unsigned j=0;j<8;++j) assert(dst[i*8+j]==order[i]);
    memset(dst,0xa7,sizeof(dst));
    const unsigned invalid[][3]={{0,4,8},{4,0,8},{3,4,8},{4,7,8},{4097,4,8},
        {UINT_MAX,1,8},{4,UINT_MAX,8},{4,4,0},{4,4,7},{4,4,32}};
    size_t bytes=0x12345;
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        assert(tsfp_bc_level_size(invalid[i][0],invalid[i][1],invalid[i][2],&bytes)==TSFP_BC_INVALID && bytes==0x12345);
        assert(tsfp_bc_reorder_range(dst,sizeof(dst),src,sizeof(src),invalid[i][0],invalid[i][1],invalid[i][2],0,0)==TSFP_BC_INVALID);
        assert(!memcmp(dst,before,sizeof(dst)));
    }
    assert(tsfp_bc_level_size(4,4,8,NULL)==TSFP_BC_INVALID);
    assert(tsfp_bc_level_size(4096,4096,16,&bytes)==TSFP_BC_OK && bytes==16u*1024u*1024u);
    assert(tsfp_bc_reorder_range(NULL,8,src,8,4,4,8,0,1)==TSFP_BC_INVALID);
    assert(tsfp_bc_reorder_range(dst,8,NULL,8,4,4,8,0,1)==TSFP_BC_INVALID);
    assert(tsfp_bc_reorder_range(dst,7,src,8,4,4,8,0,1)==TSFP_BC_RANGE);
    assert(tsfp_bc_reorder_range(dst,8,src,7,4,4,8,0,1)==TSFP_BC_RANGE);
    assert(tsfp_bc_reorder_range(dst,8,src,8,4,4,8,1,0)==TSFP_BC_RANGE);
    assert(tsfp_bc_reorder_range(dst,8,src,8,4,4,8,0,2)==TSFP_BC_RANGE);
    assert(tsfp_bc_reorder_range(dst,8,src,8,4,4,8,SIZE_MAX,SIZE_MAX)==TSFP_BC_RANGE);
    assert(tsfp_bc_reorder_range(dst,8,src,8,4,4,8,1,1)==TSFP_BC_OK);
    assert(!memcmp(dst,before,sizeof(dst)));
    assert(tsfp_bc_reorder_range((void *)(UINTPTR_MAX-3),8,src,8,4,4,8,0,1)==TSFP_BC_RANGE);
    assert(tsfp_bc_reorder_range(dst,8,(void *)(UINTPTR_MAX-3),8,4,4,8,0,1)==TSFP_BC_RANGE);
    memset(src,0x39,sizeof(src)); memcpy(before,src,sizeof(src));
    assert(tsfp_bc_reorder_range(src,8,src,8,4,4,8,0,1)==TSFP_BC_OVERLAP);
    assert(tsfp_bc_reorder_range(src+1,8,src,8,4,4,8,0,1)==TSFP_BC_OVERLAP);
    assert(tsfp_bc_reorder_range(src,8,src+1,8,4,4,8,0,1)==TSFP_BC_OVERLAP);
    assert(!memcmp(src,before,sizeof(src)));
    puts("bc-layout-v1: cases=162 fixed-order=pass spans=pass overflow=pass overlap=pass transaction=pass");
    assert(cases==162); return 0;
}
