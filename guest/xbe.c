/* SPDX-License-Identifier: GPL-3.0-only */
#include "xbe.h"
#include "kernel.h"
#include <string.h>
static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int guest_word(const TsfpXbeImage *x,uint32_t address,uint32_t *value) {
    for (size_t i=0;i<x->count;++i) {
        const TsfpImageSection *s=&x->sections[i];
        if (address>=s->address && (uint64_t)(address-s->address)+4<=s->file_size) {
            *value=word((const unsigned char *)s->data+(address-s->address)); return 1;
        }
    }
    return 0;
}
int tsfp_xbe_normalize(const void *file,size_t size,TsfpXbeImage *output) {
    if (!file || !output || size<0x178 || size-1>UINTPTR_MAX-(uintptr_t)file) return 0;
    uintptr_t input=(uintptr_t)file,out=(uintptr_t)output;
    if (sizeof(*output)-1>UINTPTR_MAX-out || (out<=input ? input-out<sizeof(*output) : out-input<size)) return 0;
    const unsigned char *bytes=file;
    if (memcmp(bytes,"XBEH",4)) return 0;
    uint32_t base=word(bytes+0x104),headers=word(bytes+0x108),image_size=word(bytes+0x10c),image_header=word(bytes+0x110);
    uint32_t count=word(bytes+0x11c),table=word(bytes+0x120);
    if (base<0x10000 || image_header<0x178 || image_header>headers || headers<0x178 || headers>size || image_size<headers || image_size>0x04000000 ||
        (uint64_t)base+image_size>0x74000000 || !count || count>TSFP_IMAGE_MAX_SECTIONS || table<base ||
        table-base<0x178 || (uint64_t)(table-base)+count*56u>headers) return 0;
    TsfpXbeImage x={0}; x.count=count;
    size_t total=0;
    for (size_t i=0;i<count;++i) {
        const unsigned char *h=bytes+(table-base)+i*56;
        uint32_t flags=word(h),address=word(h+4),virtual_size=word(h+8),raw=word(h+12),raw_size=word(h+16);
        if ((flags&~7u) || (flags&5u)==5u || !virtual_size || virtual_size>TSFP_IMAGE_MAX_BYTES-total ||
            address<(uint64_t)base+headers || (uint64_t)address+(virtual_size>raw_size ? virtual_size : raw_size)>(uint64_t)base+image_size ||
            (raw_size && ((uint64_t)raw+raw_size>size || raw<headers))) return 0;
        total+=virtual_size;
        for (size_t j=0;j<i;++j) {
            const unsigned char *old=bytes+(table-base)+j*56;
            uint32_t old_raw=word(old+12),old_size=word(old+16),old_virtual=word(old+8);
            if ((uint64_t)address+(virtual_size>raw_size ? virtual_size : raw_size)>x.sections[j].address &&
                (uint64_t)x.sections[j].address+(old_virtual>old_size ? old_virtual : old_size)>address) return 0;
            if (raw_size && old_size && (uint64_t)raw+raw_size>old_raw && (uint64_t)old_raw+old_size>raw) return 0;
        }
        size_t initialized=raw_size<virtual_size ? raw_size : virtual_size;
        x.sections[i]=(TsfpImageSection){address,virtual_size,TSFP_IMAGE_READ|((flags&1u) ? TSFP_IMAGE_WRITE : 0u)|(flags&4u),initialized ? bytes+raw : NULL,initialized};
    }
    /* Pinned PC xbe_image.py canonical retail/debug key pairs. Choose only a
     * pair with both executable entry and a bounded terminated import table. */
    static const uint32_t keys[2][2]={{0xa8fc57ab,0x5b6d40b6},{0x94859d4b,0xefb1f152}};
    unsigned matches=0;
    for (unsigned k=0;k<2;++k) {
        uint32_t entry=word(bytes+0x128)^keys[k][0],imports=word(bytes+0x158)^keys[k][1];
        int executable=0;
        for (size_t i=0;i<x.count;++i)
            if ((x.sections[i].flags&TSFP_IMAGE_EXECUTE) && entry>=x.sections[i].address && (uint64_t)(entry-x.sections[i].address)<x.sections[i].file_size) executable=1;
        if (!executable || (imports&3u)) continue;
        uint32_t markers[16],marker=1; size_t n=0;
        for (;n<=16;++n) {
            if ((uint64_t)imports+n*4>UINT32_MAX || !guest_word(&x,imports+(uint32_t)n*4,&marker)) break;
            if (!marker) break;
            if (n<16) markers[n]=marker;
        }
        if (marker || !n || n>16 || !tsfp_kernel_imports_validate(markers,n)) continue;
        x.entry=entry; x.import_table=imports; x.import_count=n; ++matches;
    }
    if (matches!=1) return 0;
    x.tls_directory=word(bytes+0x12c); x.stack_commit=word(bytes+0x130);
    if (!x.stack_commit || (x.stack_commit&3u) || x.stack_commit>TSFP_IMAGE_MAX_BYTES) return 0;
    if (x.tls_directory) {
        uint32_t ignored;
        for (unsigned i=0;i<6;++i)
            if ((uint64_t)x.tls_directory+i*4>UINT32_MAX || !guest_word(&x,x.tls_directory+i*4,&ignored)) return 0;
    }
    *output=x; return 1;
}
