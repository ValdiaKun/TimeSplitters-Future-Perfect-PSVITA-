/* SPDX-License-Identifier: GPL-3.0-only */
#include "image.h"
#include "tls.h"
#include <string.h>
static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int tsfp_tls_layout(uint32_t raw,uint32_t zero,uint32_t *total,uint32_t *index) {
    if (!total || !index) return 0;
    uint64_t n=(((uint64_t)raw+zero+15u)&~UINT64_C(15))+4u;
    if (n>TSFP_TLS_MAX_BLOCK) return 0;
    *total=(uint32_t)n; *index=0u-(uint32_t)(n/4u); return 1;
}
static const unsigned char *view(const TsfpImageSection *s,size_t count,uint32_t address,size_t size,int file,unsigned flags) {
    for (size_t i=0;i<count;++i)
        if (address>=s[i].address && (uint64_t)(address-s[i].address)+size<=(file ? s[i].file_size : s[i].size) &&
            (s[i].flags&flags)==flags && (!file || s[i].data)) return file ? (const unsigned char *)s[i].data+(address-s[i].address) : (const unsigned char *)&s[i];
    return NULL;
}
int tsfp_tls_ranges_overlap(uint32_t a,size_t na,uint32_t b,size_t nb) {
    /* Raw TLS is bounded below one RAM view; compare both possible mirror pieces. */
    if (!na || !nb) return 0;
    if (na>=0x04000000u || nb>=0x04000000u) return 1;
    uint32_t x=a%0x04000000u,y=b%0x04000000u;
    size_t ax=na<0x04000000u-x ? na : 0x04000000u-x,by=nb<0x04000000u-y ? nb : 0x04000000u-y;
    return (ax && by && (uint64_t)x+ax>y && (uint64_t)y+by>x) ||
        (na>ax && (y<na-ax)) || (nb>by && (x<nb-by)) || (na>ax && nb>by);
}
enum TsfpTlsError tsfp_tls_parse(const TsfpImageSection *s,size_t count,uint32_t directory,TsfpTlsSpec *output) {
    if (!s || !count || count>TSFP_IMAGE_MAX_SECTIONS || !output) return TSFP_TLS_DIRECTORY;
    for (size_t i=0;i<count;++i)
        if (!s[i].size || s[i].file_size>s[i].size || (s[i].file_size && (!s[i].data || s[i].file_size-1>UINTPTR_MAX-(uintptr_t)s[i].data)) ||
            (uint64_t)s[i].address+s[i].size>0x74000000u) return TSFP_TLS_DIRECTORY;
    TsfpTlsSpec t={0};
    if (!directory) { *output=t; return TSFP_TLS_OK; }
    const unsigned char *p=view(s,count,directory,24,1,TSFP_IMAGE_READ);
    if ((directory&3u) || !p) return TSFP_TLS_DIRECTORY;
    uint32_t end=word(p+4),characteristics=word(p+20);
    t.directory=directory; t.raw_start=word(p); t.index_address=word(p+8); t.callbacks=word(p+12); t.zero_fill=word(p+16);
    if (end<t.raw_start) return TSFP_TLS_RANGE;
    t.raw_size=end-t.raw_start;
    if (!tsfp_tls_layout(t.raw_size,t.zero_fill,&t.block_size,&t.index_value) ||
        (t.raw_size && !view(s,count,t.raw_start,t.raw_size,1,TSFP_IMAGE_READ))) return TSFP_TLS_RANGE;
    if (characteristics) return TSFP_TLS_CHARACTERISTICS;
    for (size_t i=0;i<count;++i)
        if ((s[i].flags&TSFP_IMAGE_EXECUTE) && t.index_address>=s[i].address && (uint64_t)(t.index_address-s[i].address)<s[i].size) return TSFP_TLS_INDEX;
    if ((t.index_address&3u) || !view(s,count,t.index_address,4,0,TSFP_IMAGE_WRITE)) return TSFP_TLS_INDEX;
    if (tsfp_tls_ranges_overlap(t.index_address,4,directory,24) || (t.raw_size && tsfp_tls_ranges_overlap(t.index_address,4,t.raw_start,t.raw_size))) return TSFP_TLS_OVERLAP;
    if (t.callbacks) {
        const unsigned char *callback=view(s,count,t.callbacks,4,1,TSFP_IMAGE_READ);
        if ((t.callbacks&3u) || !callback) return TSFP_TLS_CALLBACK_TABLE;
        if (word(callback)) return TSFP_TLS_CALLBACK_UNSUPPORTED;
        if (tsfp_tls_ranges_overlap(t.index_address,4,t.callbacks,4)) return TSFP_TLS_OVERLAP;
    }
    *output=t; return TSFP_TLS_OK;
}
