/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "xbox_swizzle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent bit-column oracle. Do not rely on the bulk masked-increment
 * implementation to generate expected swizzled positions. */
static size_t oracle_index(unsigned w, unsigned h, unsigned x, unsigned y) {
    unsigned bit = 0; size_t result = 0;
    for (unsigned scale = 1; scale < w || scale < h; scale <<= 1) {
        if (scale < w) { if (x & scale) result |= (size_t)1 << bit; ++bit; }
        if (scale < h) { if (y & scale) result |= (size_t)1 << bit; ++bit; }
    }
    return result;
}
static void check_texture(unsigned w, unsigned h, unsigned bpp) {
    size_t src_size = (size_t)w * h * bpp;
    size_t pitch = (size_t)w * bpp + 3;
    size_t dst_size = (h - 1) * pitch + (size_t)w * bpp;
    unsigned char *source = malloc(src_size + 2);
    unsigned char *destination = malloc(dst_size + 2);
    assert(source && destination);
    memset(source, 0xee, src_size + 2);
    memset(destination, 0xa5, dst_size + 2);
    source[0] = 0x72;
    source[src_size + 1] = 0x73;
    destination[0] = 0x82;
    destination[dst_size + 1] = 0x83;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const size_t pos = oracle_index(w,h,x,y);
        size_t index = SIZE_MAX;
        assert(tsfp_swizzle_index(w,h,x,y,&index) == TSFP_SWIZZLE_OK);
        assert(index == pos && index < (size_t)w*h);
        for (unsigned i = 0; i < bpp; ++i)
            source[1 + pos*bpp + i] = (unsigned char)((x*31u + y*17u + i*73u + 9u) & 255u);
    }
    assert(tsfp_unswizzle_2d(destination+1,dst_size,pitch,source+1,src_size,w,h,bpp) == TSFP_SWIZZLE_OK);
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) for (unsigned i = 0; i < bpp; ++i)
            assert(destination[1 + (size_t)y*pitch + (size_t)x*bpp + i] ==
                (unsigned char)((x*31u + y*17u + i*73u + 9u) & 255u));
        if (y+1 < h) for (size_t i=(size_t)w*bpp; i<pitch; ++i)
            assert(destination[1 + (size_t)y*pitch + i] == 0xa5);
    }
    assert(destination[0] == 0x82 && destination[dst_size + 1] == 0x83);
    assert(source[0] == 0x72 && source[src_size + 1] == 0x73);
    free(source); free(destination);
}
static void check_rejected(void) {
    unsigned char buf[130];
    unsigned char before[sizeof(buf)];
    memset(buf,0x5a,sizeof(buf)); memcpy(before,buf,sizeof(buf));
    size_t index = SIZE_MAX;
    assert(tsfp_swizzle_index(4,2,2,1,&index) == TSFP_SWIZZLE_OK && index==6);
    assert(tsfp_swizzle_index(2,4,1,2,&index) == TSFP_SWIZZLE_OK && index==5);
    assert(tsfp_swizzle_index(2,3,1,1,&index) == TSFP_SWIZZLE_INVALID);
    assert(tsfp_swizzle_index(4096,8192,0,0,&index) == TSFP_SWIZZLE_INVALID);
    index = SIZE_MAX;
    assert(tsfp_swizzle_index(4,4,4,0,&index) == TSFP_SWIZZLE_RANGE);
    assert(tsfp_swizzle_index(4,4,0,4,&index) == TSFP_SWIZZLE_RANGE);
    assert(tsfp_swizzle_index(4,4,0,0,NULL) == TSFP_SWIZZLE_INVALID);
    assert(index == SIZE_MAX);
    assert(tsfp_unswizzle_2d(buf+64,27,8,buf,16,4,4,1) == TSFP_SWIZZLE_RANGE);
    assert(tsfp_unswizzle_2d(buf+64,32,8,buf,15,4,4,1) == TSFP_SWIZZLE_RANGE);
    assert(tsfp_unswizzle_2d(buf+64,32,3,buf,16,4,4,1) == TSFP_SWIZZLE_RANGE);
    assert(tsfp_unswizzle_2d(buf+64,32,SIZE_MAX,buf,16,4,4,1) == TSFP_SWIZZLE_RANGE);
    assert(tsfp_unswizzle_2d(buf,64,4,buf,64,4,4,1) == TSFP_SWIZZLE_OVERLAP);
    assert(tsfp_unswizzle_2d(buf+10,50,4,buf,40,4,4,1) == TSFP_SWIZZLE_OVERLAP);
    assert(tsfp_unswizzle_2d(buf+64,32,4,buf,16,3,4,1) == TSFP_SWIZZLE_INVALID);
    assert(tsfp_unswizzle_2d(buf+64,32,4,buf,16,4,4,3) == TSFP_SWIZZLE_INVALID);
    assert(tsfp_unswizzle_2d(NULL,32,4,buf,16,4,4,1) == TSFP_SWIZZLE_INVALID);
    assert(tsfp_unswizzle_2d(buf+64,32,4,NULL,16,4,4,1) == TSFP_SWIZZLE_INVALID);
    assert(!memcmp(before,buf,sizeof(buf)));
}
int main(void) {
    unsigned cases = 0;
    for (unsigned w = 1; w <= 128; w <<= 1)
        for (unsigned h = 1; h <= 128; h <<= 1)
            for (unsigned bpp = 1; bpp <= 4; bpp <<= 1) {
                check_texture(w,h,bpp);
                ++cases;
            }
    for (unsigned bpp = 1; bpp <= 4; bpp <<= 1) {
        check_texture(1,4096,bpp);
        check_texture(4096,1,bpp);
        check_texture(2,2048,bpp);
        check_texture(2048,2,bpp);
        cases+=4;
    }
    size_t last=0;
    assert(tsfp_swizzle_index(4096,4096,4095,4095,&last)==TSFP_SWIZZLE_OK);
    assert(last==4096u*4096u-1u);
    check_rejected();
    printf("xbox-swizzle: asset-free layouts=%u rectangular=pass noncolliding=pass rollback=pass\n",cases);
    return 0;
}
