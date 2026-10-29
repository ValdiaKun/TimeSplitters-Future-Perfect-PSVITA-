/* SPDX-License-Identifier: GPL-3.0-only */
#include "image.h"
#include "memory.h"
#include "kernel.h"
#include <stdlib.h>
#include <string.h>
/* One image per guest address space. Setup/teardown require exclusive ownership. */
typedef struct Part { uint32_t offset,size,flags; } Part;
typedef struct Plan {
    TsfpImageDescription d;
    TsfpImageSection s[TSFP_IMAGE_MAX_SECTIONS];
    size_t offsets[TSFP_IMAGE_MAX_SECTIONS];
    Part parts[TSFP_IMAGE_MAX_SECTIONS*3];
    size_t part_count;
    TsfpImageFunction function;
} Plan;
static Plan image;
static int ready;
static void *(*allocate_image)(size_t)=malloc;
static void (*release_image)(void *)=free;
int tsfp_image_set_allocator(void *(*allocate)(size_t),void (*release)(void *)) {
    if (ready || tsfp_guest_thread_id() || tsfp_guest_recovery_active() || (!!allocate!=!!release)) return 0;
    allocate_image=allocate ? allocate : malloc; release_image=release ? release : free;
    return 1;
}
static int policy(uint32_t address, size_t width, unsigned mode) {
    if (!width || (uint64_t)width>UINT64_C(0x100000000)-address) return 0;
    while (width) {
        TsfpGuestSpan span=tsfp_guest_classify(address);
        if (span.region!=TSFP_REGION_RAM && span.region!=TSFP_REGION_RAM_ALIAS) return 0;
        size_t n=0;
        for (size_t i=0;i<image.part_count;++i) {
            Part p=image.parts[i];
            if (span.offset>=p.offset && span.offset-p.offset<p.size && (p.flags&mode)==mode) {
                n=p.size-(span.offset-p.offset); break;
            }
        }
        if (!n) return 0;
        if (n>span.available) n=span.available;
        if (n>width) n=width;
        width-=n; if (width) address+=(uint32_t)n;
    }
    return 1;
}
static unsigned char *at(Plan *p,unsigned char *stage,uint32_t address,size_t width,unsigned flags) {
    if (!width) return NULL;
    for (size_t i=0;i<p->d.count;++i) {
        TsfpImageSection s=p->s[i];
        if (address>=s.address && (uint64_t)(address-s.address)+width<=s.size && (s.flags&flags)==flags)
            return stage+p->offsets[i]+(address-s.address);
    }
    return NULL;
}
static int overlap(uint32_t a,size_t na,uint32_t b,size_t nb) {
    uint32_t x[2]={a%TSFP_XBOX_RAM_SIZE,0}, y[2]={b%TSFP_XBOX_RAM_SIZE,0};
    size_t ax=na<TSFP_XBOX_RAM_SIZE-x[0] ? na : TSFP_XBOX_RAM_SIZE-x[0];
    size_t by=nb<TSFP_XBOX_RAM_SIZE-y[0] ? nb : TSFP_XBOX_RAM_SIZE-y[0];
    size_t nx[2]={ax,na-ax},ny[2]={by,nb-by};
    for (unsigned i=0;i<2;++i) for (unsigned j=0;j<2;++j)
        if (nx[i] && ny[j] && (uint64_t)x[i]+nx[i]>y[j] && (uint64_t)y[j]+ny[j]>x[i]) return 1;
    return 0;
}

enum TsfpImageError tsfp_image_initialize(const TsfpImageDescription *description) {
    if (ready || tsfp_guest_policy_active() || tsfp_guest_thread_id() || tsfp_guest_recovery_active()) return TSFP_IMAGE_BUSY;
    if (!description || !description->sections || !description->count || description->count>TSFP_IMAGE_MAX_SECTIONS || !description->resolve)
        return TSFP_IMAGE_METADATA;
    Plan p={0}; p.d=*description;
    memcpy(p.s,description->sections,p.d.count*sizeof(p.s[0]));
    size_t total=0;
    for (size_t i=0;i<p.d.count;++i) {
        TsfpImageSection s=p.s[i];
        if (!s.size || s.file_size>s.size || (s.file_size && (!s.data || s.file_size-1>UINTPTR_MAX-(uintptr_t)s.data)) ||
            !(s.flags&TSFP_IMAGE_READ) || (s.flags&~7u) || (s.flags&6u)==6u || s.size>TSFP_IMAGE_MAX_BYTES-total)
            return TSFP_IMAGE_METADATA;
        if ((uint64_t)s.address+s.size>UINT64_C(0x74000000) || !tsfp_guest_range_valid(s.address,s.size)) return TSFP_IMAGE_RANGE;
        p.offsets[i]=total; total+=s.size;
        uint32_t address=s.address,left=s.size;
        while (left) {
            TsfpGuestSpan span=tsfp_guest_classify(address);
            uint32_t n=left<span.available ? left : span.available;
            if (!n || (span.region!=TSFP_REGION_RAM && span.region!=TSFP_REGION_RAM_ALIAS)) return TSFP_IMAGE_RANGE;
            for (size_t j=0;j<p.part_count;++j)
                if ((uint64_t)span.offset+n>p.parts[j].offset && (uint64_t)p.parts[j].offset+p.parts[j].size>span.offset)
                    return TSFP_IMAGE_OVERLAP;
            uint32_t consumed=s.size-left, flags=s.flags;
            if ((flags&TSFP_IMAGE_EXECUTE) && consumed<s.file_size && n>s.file_size-consumed) n=(uint32_t)(s.file_size-consumed);
            if (consumed>=s.file_size) flags&=~TSFP_IMAGE_EXECUTE;
            p.parts[p.part_count++]=(Part){span.offset,n,flags};
            left-=n; if (left) address+=n;
        }
    }
    int executable=0;
    for (size_t i=0;i<p.d.count;++i)
        if ((p.s[i].flags&TSFP_IMAGE_EXECUTE) && p.d.entry>=p.s[i].address && (uint64_t)(p.d.entry-p.s[i].address)<p.s[i].file_size) executable=1;
    if (!executable || !(p.function=p.d.resolve(p.d.entry))) return TSFP_IMAGE_ENTRY;
    unsigned char *stage=allocate_image(total);
    if (!stage) return TSFP_IMAGE_ALLOCATION;
    memset(stage,0,total);
    for (size_t i=0;i<p.d.count;++i) if (p.s[i].file_size) memcpy(stage+p.offsets[i],p.s[i].data,p.s[i].file_size);
    enum TsfpImageError result=TSFP_IMAGE_THREAD;
    if (!p.d.thread_id || p.d.stack_size<16 || (p.d.tib&3u) || (p.d.object&3u) || (p.d.import_table&3u) || (p.d.stack_base&3u) || (p.d.stack_size&3u) ||
        p.d.tls_size<4 || p.d.tls_size>65536-64 || p.d.thread_data_size>65536-64-p.d.tls_size) goto fail;
    size_t tls_total=64+p.d.tls_size+p.d.thread_data_size;
    uint32_t reserved[]={p.d.tib,p.d.object,p.d.stack_base,p.d.import_table};
    if (!p.d.import_count || p.d.import_count>16) { result=TSFP_IMAGE_IMPORT; goto fail; }
    size_t sizes[]={tls_total,0x200,p.d.stack_size,p.d.import_count*4};
    for (unsigned i=0;i<4;++i) {
        if (!at(&p,stage,reserved[i],sizes[i],TSFP_IMAGE_WRITE) || (uint64_t)(reserved[i]%TSFP_XBOX_RAM_SIZE)+sizes[i]>TSFP_XBOX_RAM_SIZE) goto fail;
        for (unsigned j=0;j<i;++j) if (overlap(reserved[i],sizes[i],reserved[j],sizes[j])) goto fail;
    }
    unsigned char *tls=at(&p,stage,p.d.tls_template,p.d.tls_size,TSFP_IMAGE_READ);
    unsigned char *object=at(&p,stage,p.d.object_template,0x200,TSFP_IMAGE_READ);
    if (!tls || !object) goto fail;
    for (unsigned i=0;i<4;++i)
        if (overlap(p.d.tls_template,p.d.tls_size,reserved[i],sizes[i]) || overlap(p.d.object_template,0x200,reserved[i],sizes[i])) goto fail;
    unsigned char *imports=at(&p,stage,p.d.import_table,sizes[3],TSFP_IMAGE_WRITE);
    uint32_t markers[16]; memcpy(markers,imports,p.d.import_count*4);
    if (!tsfp_kernel_imports_validate(markers,p.d.import_count)) { result=TSFP_IMAGE_IMPORT; goto fail; }
    unsigned char *tib=at(&p,stage,p.d.tib,tls_total,TSFP_IMAGE_WRITE);
    memset(tib,0,tls_total); memcpy(tib+64,tls,p.d.tls_size);
    uint32_t end=p.d.tib+64+p.d.tls_size,seh=UINT32_MAX;
    memcpy(tib,&seh,4); memcpy(tib+4,&end,4); memcpy(tib+64,&end,4); memcpy(tib+0x28,&p.d.object,4);
    unsigned char *thread=at(&p,stage,p.d.object,0x200,TSFP_IMAGE_WRITE);
    memcpy(thread,object,0x200); memcpy(thread+0x12c,&p.d.thread_id,4);
    memset(at(&p,stage,p.d.stack_base,p.d.stack_size,TSFP_IMAGE_WRITE),0,p.d.stack_size);
    /* No fallible input validation/allocation remains. Bindings and input
     * ownership must stay quiescent. Unexpected internal inconsistency aborts,
     * rather than reporting successful recovery from a half-published image. */
    for (size_t i=0;i<p.d.count;++i) tsfp_guest_write(p.s[i].address,stage+p.offsets[i],p.s[i].size);
    if (!tsfp_kernel_imports_prepare(p.d.import_table,p.d.import_count)) abort();
    release_image(stage);
    p.d.sections=NULL; p.d.resolve=NULL;
    for (size_t i=0;i<p.d.count;++i) p.s[i].data=NULL;
    image=p; ready=1; tsfp_guest_set_policy(policy);
    return TSFP_IMAGE_OK;
fail:
    release_image(stage); return result;
}
static void enter(void *unused) { (void)unused; image.function(); }
int tsfp_image_run(TsfpFault *fault) {
    if (ready!=1 || !fault || tsfp_guest_recovery_active() || tsfp_guest_thread_id()) return -1;
    size_t tls_total=64+image.d.tls_size+image.d.thread_data_size;
    if (!tsfp_guest_thread_enter(image.d.thread_id,image.d.stack_base,image.d.stack_size,image.d.tib,tls_total)) return -1;
    /* Store the root return marker before invoking resource-free C frames. */
    tsfp_guest_store_u32(image.d.stack_base+image.d.stack_size-4,0);
    extern _Thread_local uint32_t g_esp;
    g_esp-=4;
    ready=2;
    int result=tsfp_guest_run(enter,NULL,fault);
    if (!tsfp_guest_thread_leave()) abort();
    ready=3;
    return result;
}
int tsfp_image_active(void) { return ready; }
int tsfp_image_reset(void) {
    if (tsfp_guest_thread_id() || tsfp_guest_recovery_active()) return 0;
    if (ready) { tsfp_guest_set_policy(NULL); tsfp_kernel_imports_reset(); memset(&image,0,sizeof(image)); ready=0; }
    return 1;
}
