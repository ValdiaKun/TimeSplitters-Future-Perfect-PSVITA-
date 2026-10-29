/* SPDX-License-Identifier: GPL-3.0-only
 * Page ownership/rounding adapted from sp00nznet/xboxrecomp guest_vmem.c,
 * f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b, Copyright (c) 2026 sp00nz.
 * Original MIT permission notice: LICENSES/xboxrecomp-MIT.txt.
 * Vita adaptation replaces fixed Windows VMA with explicitly ceded image BSS,
 * bounded metadata, checked RAM policy and shared storage ownership locking.
 */
#include "heap.h"
#include "memory.h"
#include "image.h"
#include "storage.h"
#include "handles.h"
#include <stdatomic.h>
#include <string.h>
#define PAGE 4096u
#define PAGES 256u
#define INVALID UINT32_C(0xc000000d)
#define CONFLICT UINT32_C(0xc0000018)
#define NO_MEMORY UINT32_C(0xc0000017)
#define UNSUPPORTED UINT32_C(0xc00000bb)
static uint32_t arena,extent;
static unsigned char *backing;
static uint32_t owner[PAGES];
static _Atomic unsigned char committed[PAGES];
static atomic_int enabled;
int tsfp_heap_configured(void) { return atomic_load(&enabled); }
int tsfp_heap_overlaps(uint32_t address,size_t size) {
    return tsfp_heap_configured() && tsfp_tls_ranges_overlap(arena,extent,address,size);
}
int tsfp_heap_access_allowed(uint32_t address,size_t size,unsigned mode) {
    if (!tsfp_heap_configured()) return 1;
    while (size) {
        TsfpGuestSpan span=tsfp_guest_classify(address);
        size_t n=size<span.available ? size : span.available;
        if (!n) return 0;
        uint32_t offset=span.offset;
        /* Image policy already rejects all non-RAM regions. */
        if (offset>=arena && offset-arena<extent) {
            unsigned page=(offset-arena)/PAGE;
            if ((mode&4u) || !atomic_load(&committed[page])) return 0;
            if (n>PAGE-(offset%PAGE)) n=PAGE-(offset%PAGE);
        } else if (offset<arena && n>arena-offset) n=arena-offset;
        size-=n; if (size) address+=(uint32_t)n;
    }
    return 1;
}
int tsfp_heap_configure(uint32_t base,uint32_t size) {
    if (tsfp_guest_recovery_active() || tsfp_guest_thread_id() || (base&65535u) || !base ||
        base>=TSFP_XBOX_RAM_SIZE || !size || (size&65535u) || size>PAGES*PAGE || size>TSFP_XBOX_RAM_SIZE-base) return 0;
    tsfp_storage_lock(); int ok=0; TsfpImageThreadTemplate t;
    if (!tsfp_heap_configured() && !tsfp_thread_handles_busy() &&
        !tsfp_worker_range_busy_locked(0,TSFP_XBOX_RAM_SIZE) &&
        tsfp_storage_range_allowed_locked(base,size) && tsfp_image_thread_arena(base,size,&t)) {
        backing=tsfp_guest_pointer(base,size); arena=base; extent=size;
        memset(owner,0,sizeof(owner));
        for (unsigned i=0;i<PAGES;++i) atomic_store(&committed[i],0);
        atomic_store(&enabled,1); ok=1;
    }
    tsfp_storage_unlock(); return ok;
}
int tsfp_heap_disable(void) {
    if (tsfp_guest_recovery_active() || tsfp_guest_thread_id()) return 0;
    tsfp_storage_lock(); int ok=!tsfp_thread_handles_busy() && !tsfp_worker_range_busy_locked(0,TSFP_XBOX_RAM_SIZE);
    for (unsigned i=0;i<PAGES;++i) if (owner[i]) ok=0;
    if (ok) { atomic_store(&enabled,0); backing=NULL; arena=extent=0; }
    tsfp_storage_unlock(); return ok;
}
static int owned(unsigned first,unsigned end,uint32_t value) {
    if (!value) return 0;
    for (unsigned p=first;p<end;++p) if (owner[p]!=value) return 0;
    return 1;
}
uint32_t tsfp_heap_allocate(uint32_t *base,uint32_t *size,uint32_t zero_bits,uint32_t type,uint32_t protect) {
    if (!base || !size || base==size || !*size) return INVALID;
    if (zero_bits || (type!=0x1000u && type!=0x2000u && type!=0x3000u) || protect!=4u) return UNSUPPORTED;
    tsfp_storage_lock(); uint32_t status=UNSUPPORTED,start=*base; uint64_t end;
    if (!tsfp_heap_configured()) goto done;
    if (!start) {
        if (!(type&0x2000u)) { status=INVALID; goto done; }
        uint64_t rounded=((uint64_t)*size+PAGE-1)&~(uint64_t)(PAGE-1);
        status=NO_MEMORY;
        for (uint32_t candidate=arena;candidate<arena+extent;candidate+=65536u) {
            if (rounded>arena+extent-candidate) break;
            unsigned a=(candidate-arena)/PAGE,b=a+(unsigned)(rounded/PAGE),p=a;
            while (p<b && !owner[p]) ++p;
            if (p==b) { start=candidate; break; }
        }
        if (!start) goto done;
    }
    end=((uint64_t)start+*size+PAGE-1)&~(uint64_t)(PAGE-1);
    start &= (type&0x2000u) ? ~65535u : ~(PAGE-1);
    status=CONFLICT;
    if (start<arena || end>arena+extent || end<=start) goto done;
    unsigned first=(start-arena)/PAGE,last=(unsigned)((end-arena)/PAGE);
    uint32_t allocation=owner[first];
    if (type&0x2000u) {
        for (unsigned p=first;p<last;++p) if (owner[p]) goto done;
        allocation=start;
    } else if (!owned(first,last,allocation)) goto done;
    /* All validation precedes mutation. Native backing was checked and pinned
     * at configuration; no guest-faulting calls or allocations under the lock. */
    for (unsigned p=first;p<last;++p) {
        if ((type&0x1000u) && !atomic_load(&committed[p])) memset(backing+p*PAGE,0,PAGE);
        owner[p]=allocation;
        if (type&0x1000u) atomic_store(&committed[p],1);
    }
    *base=start; *size=(uint32_t)(end-start); status=0;
done:
    tsfp_storage_unlock(); return status;
}
uint32_t tsfp_heap_free(uint32_t *base,uint32_t *size,uint32_t type) {
    if (!base || !size || base==size) return INVALID;
    if (type!=0x4000u && type!=0x8000u) return UNSUPPORTED;
    tsfp_storage_lock(); uint32_t status=INVALID;
    if (!tsfp_heap_configured()) { status=UNSUPPORTED; goto done; }
    if (*base<arena || *base>=arena+extent) goto done;
    uint32_t start=*base&~(PAGE-1); unsigned first=(start-arena)/PAGE,last;
    uint32_t allocation=owner[first]; if (!allocation) goto done;
    if (type==0x8000u) {
        if (*size || *base!=allocation) goto done;
        last=first; while (last<extent/PAGE && owner[last]==allocation) ++last;
    } else {
        if (!*size) goto done;
        uint64_t end=((uint64_t)*base+*size+PAGE-1)&~(uint64_t)(PAGE-1);
        if (end>arena+extent) goto done;
        last=(unsigned)((end-arena)/PAGE);
        if (!owned(first,last,allocation)) goto done;
    }
    for (unsigned p=first;p<last;++p) {
        atomic_store(&committed[p],0);
        if (type==0x8000u) owner[p]=0;
    }
    if (type==0x8000u) { *base=0; *size=0; }
    else { *base=start; *size=(last-first)*PAGE; }
    status=0;
done:
    tsfp_storage_unlock(); return status;
}
