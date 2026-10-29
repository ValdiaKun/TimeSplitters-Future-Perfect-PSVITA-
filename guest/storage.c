/* SPDX-License-Identifier: GPL-3.0-only */
#include "storage.h"
#include "heap.h"
#include "handles.h"
#include "kernel.h"
#include "memory.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define LEASE_TAG UINT32_C(0x53000000)
#define MAX_GENERATION UINT32_C(0x3ffff)
typedef struct Reservation {
    uint32_t generation,metadata,owner; size_t metadata_size;
    int platform_error;
    enum TsfpStorageState state;
    TsfpWorkerSpec spec; TsfpWorker worker;
    unsigned char *backup;
} Reservation;
static Reservation records[TSFP_THREAD_HANDLES];
static pthread_mutex_t ownership=PTHREAD_MUTEX_INITIALIZER;
static TsfpStorageConfig config;
static TsfpImageThreadTemplate source;
static int configured,service_pin;
static void *(*allocate)(size_t)=malloc;
static void (*release)(void *)=free;
void tsfp_storage_lock(void) { if (pthread_mutex_lock(&ownership)) abort(); }
void tsfp_storage_unlock(void) { if (pthread_mutex_unlock(&ownership)) abort(); }
static Reservation *find(uint32_t token) {
    unsigned slot=token&63u;
    if ((token&0xff000000u)!=LEASE_TAG || !slot || slot>config.slots || !configured) return NULL;
    Reservation *r=&records[slot-1];
    return r->state && r->generation==((token&0xffffffu)>>6) ? r : NULL;
}
static int any(void) {
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (records[i].state) return 1;
    return 0;
}
static void clear(Reservation *r) {
    uint32_t generation=r->generation;
    memset(r,0,sizeof(*r)); r->generation=generation;
}
int tsfp_storage_configured(void) {
    tsfp_storage_lock(); int result=configured; tsfp_storage_unlock(); return result;
}
int tsfp_storage_set_allocator(void *(*a)(size_t),void (*f)(void *)) {
    if (tsfp_guest_recovery_active() || (!!a!=!!f)) return 0;
    tsfp_storage_lock(); int ok=!configured && !any();
    if (ok) { allocate=a ? a : malloc; release=f ? f : free; }
    tsfp_storage_unlock(); return ok;
}
enum TsfpStorageError tsfp_storage_configure(const TsfpStorageConfig *input) {
    if (!input || tsfp_guest_recovery_active()) return TSFP_STORAGE_INVALID;
    TsfpStorageConfig c=*input;
    if (!c.resolve || !c.slots || c.slots>TSFP_THREAD_HANDLES || c.stride<768 ||
        (c.base&63u) || (c.stride&63u) || c.stride>TSFP_IMAGE_MAX_BYTES ||
        (uint64_t)c.stride*c.slots>TSFP_IMAGE_MAX_BYTES || c.native_stack_size<65536 || c.native_stack_size>1024*1024)
        return TSFP_STORAGE_INVALID;
    tsfp_storage_lock();
    enum TsfpStorageError result=TSFP_STORAGE_INVALID;
    if (configured || any() || tsfp_thread_handles_busy() || tsfp_worker_range_busy_locked(0,TSFP_XBOX_RAM_SIZE)) result=TSFP_STORAGE_BUSY;
    else {
        TsfpImageThreadTemplate t;
        if (!tsfp_heap_overlaps(c.base,(size_t)c.stride*c.slots) && tsfp_image_thread_arena(c.base,(size_t)c.stride*c.slots,&t)) {
            config=c; source=t; configured=1; result=TSFP_STORAGE_OK;
        }
    }
    tsfp_storage_unlock(); return result;
}
int tsfp_storage_disable(void) {
    if (tsfp_guest_recovery_active()) return 0;
    tsfp_storage_lock(); int ok=!any() && !service_pin;
    if (ok) { memset(&config,0,sizeof(config)); memset(&source,0,sizeof(source)); configured=0; }
    tsfp_storage_unlock(); return ok;
}
static int matches(const TsfpWorkerSpec *a,const TsfpWorkerSpec *b) {
    return a->storage_lease==b->storage_lease && a->object==b->object && a->id==b->id &&
        a->stack==b->stack && a->tib==b->tib && a->entry==b->entry && a->context1==b->context1 && a->context2==b->context2 &&
        a->stack_size==b->stack_size && a->tls_size==b->tls_size && a->native_stack_size==b->native_stack_size && a->resolve==b->resolve;
}
int tsfp_storage_worker_allowed_locked(const TsfpWorker *w,const TsfpWorkerSpec *s) {
    if (tsfp_heap_overlaps(s->stack,s->stack_size) || tsfp_heap_overlaps(s->tib,s->tls_size) || tsfp_heap_overlaps(s->object,512)) return 0;
    if (s->storage_lease) {
        Reservation *r=find(s->storage_lease);
        return r && r->state==TSFP_STORAGE_LAUNCHING && w==&r->worker && matches(s,&r->spec);
    }
    if (!configured) return 1;
    size_t size=(size_t)config.stride*config.slots;
    return !tsfp_tls_ranges_overlap(config.base,size,s->stack,s->stack_size) &&
        !tsfp_tls_ranges_overlap(config.base,size,s->tib,s->tls_size) && !tsfp_tls_ranges_overlap(config.base,size,s->object,512) &&
        !tsfp_tls_ranges_overlap(source.tls.raw_start,source.tls.raw_size,s->stack,s->stack_size) &&
        !tsfp_tls_ranges_overlap(source.tls.raw_start,source.tls.raw_size,s->tib,s->tls_size) &&
        !tsfp_tls_ranges_overlap(source.tls.raw_start,source.tls.raw_size,s->object,512) &&
        !tsfp_tls_ranges_overlap(source.object_template,512,s->stack,s->stack_size) &&
        !tsfp_tls_ranges_overlap(source.object_template,512,s->tib,s->tls_size) && !tsfp_tls_ranges_overlap(source.object_template,512,s->object,512);
}
int tsfp_storage_object_allowed_locked(uint32_t object,uint32_t id,uint32_t lease) {
    if (tsfp_heap_overlaps(object,512)) return 0;
    if (lease) {
        Reservation *r=find(lease);
        return r && r->state==TSFP_STORAGE_LAUNCHING && r->spec.object==object && r->spec.id==id;
    }
    if (!configured) return 1;
    if (tsfp_tls_ranges_overlap(config.base,(size_t)config.stride*config.slots,object,512) ||
        tsfp_tls_ranges_overlap(source.object_template,512,object,512) || tsfp_tls_ranges_overlap(source.tls.raw_start,source.tls.raw_size,object,512)) return 0;
    for (unsigned i=0;i<config.slots;++i) if (records[i].state && records[i].spec.id==id) return 0;
    return 1;
}
enum TsfpStorageError tsfp_storage_reserve(const TsfpStorageRequest *input,uint32_t *lease) {
    if (!input || !lease || tsfp_guest_recovery_active()) return TSFP_STORAGE_INVALID;
    TsfpStorageRequest request=*input;
    if (!request.id || request.stack_size<16 || (request.stack_size&15u)) return TSFP_STORAGE_INVALID;
    tsfp_storage_lock();
    if (!configured) { tsfp_storage_unlock(); return TSFP_STORAGE_INVALID; }
    if (tsfp_thread_id_busy(request.id)) { tsfp_storage_unlock(); return TSFP_STORAGE_BUSY; }
    if (request.id==source.initial_id) { tsfp_storage_unlock(); return TSFP_STORAGE_INVALID; }
    /* Trusted resolver is pure; it may not execute guest/native resource code. */
    if (!tsfp_guest_range_valid(request.entry,1) || !tsfp_guest_access_allowed(request.entry,1,4) || !config.resolve(request.entry)) {
        tsfp_storage_unlock(); return TSFP_STORAGE_INVALID;
    }
    unsigned slot=config.slots;
    for (unsigned i=0;i<config.slots;++i) {
        if (records[i].state && records[i].spec.id==request.id) { tsfp_storage_unlock(); return TSFP_STORAGE_BUSY; }
        if (slot==config.slots && !records[i].state && records[i].generation<MAX_GENERATION) slot=i;
    }
    if (slot==config.slots) { tsfp_storage_unlock(); return TSFP_STORAGE_EXHAUSTED; }
    uint32_t block,index;
    if (!tsfp_tls_layout(source.tls.raw_size,source.tls.zero_fill,&block,&index) || source.thread_data_size>65536u-64u-block) {
        tsfp_storage_unlock(); return TSFP_STORAGE_INVALID;
    }
    size_t tls_size=64+block+source.thread_data_size;
    uint64_t tib_offset=((uint64_t)request.stack_size+63u)&~UINT64_C(63);
    uint64_t object_offset=(tib_offset+tls_size+63u)&~UINT64_C(63);
    if (object_offset+512u>config.stride) { tsfp_storage_unlock(); return TSFP_STORAGE_INVALID; }
    uint32_t base=config.base+slot*config.stride;
    size_t metadata_size=(size_t)(object_offset+512u-tib_offset);
    uint32_t metadata=base+(uint32_t)tib_offset;
    /* Revalidate every source/destination before allocation/mutation. Pool pins
     * immutable bindings/policy; templates must stay stable while configured. */
    if (!tsfp_guest_range_valid(metadata,metadata_size) || !tsfp_guest_access_allowed(metadata,metadata_size,3) ||
        !tsfp_guest_range_valid(source.object_template,512) || !tsfp_guest_access_allowed(source.object_template,512,1) ||
        (source.tls.raw_size && (!tsfp_guest_range_valid(source.tls.raw_start,source.tls.raw_size) || !tsfp_guest_access_allowed(source.tls.raw_start,source.tls.raw_size,1)))) {
        tsfp_storage_unlock(); return TSFP_STORAGE_INVALID;
    }
    unsigned char *backup=allocate(metadata_size);
    if (!backup) { tsfp_storage_unlock(); return TSFP_STORAGE_ALLOCATION; }
    tsfp_guest_read(metadata,backup,metadata_size);
    Reservation *r=&records[slot]; ++r->generation;
    uint32_t token=LEASE_TAG|(r->generation<<6)|(slot+1);
    r->metadata=metadata; r->metadata_size=metadata_size; r->backup=backup;
    r->spec=(TsfpWorkerSpec){.object=base+(uint32_t)object_offset,.id=request.id,.stack=base,.tib=metadata,
        .entry=request.entry,.context1=request.context1,.context2=request.context2,.storage_lease=token,
        .stack_size=request.stack_size,.tls_size=tls_size,.native_stack_size=config.native_stack_size,.resolve=config.resolve};
    /* Bounded metadata only: the guest stack is reserved, not copied/cleared. */
    static const unsigned char zero[256]={0};
    for (size_t offset=0;offset<metadata_size;) {
        size_t n=metadata_size-offset<sizeof(zero) ? metadata_size-offset : sizeof(zero);
        tsfp_guest_write(metadata+(uint32_t)offset,zero,n); offset+=n;
    }
    if (!tsfp_guest_tls_prepare_xbe(metadata,source.tls.raw_start,source.tls.raw_size,source.tls.zero_fill,source.thread_data_size) ||
        !tsfp_thread_object_prepare_tls(r->spec.object,source.object_template,metadata,request.id,metadata+64)) {
        tsfp_guest_write(metadata,backup,metadata_size); release(backup); clear(r);
        tsfp_storage_unlock(); return TSFP_STORAGE_INITIALIZATION;
    }
    r->state=TSFP_STORAGE_PREPARED; *lease=token;
    tsfp_storage_unlock(); return TSFP_STORAGE_OK;
}
static void rollback(Reservation *r) {
    tsfp_guest_write(r->metadata,r->backup,r->metadata_size); release(r->backup); clear(r);
}
int tsfp_storage_cancel(uint32_t lease) {
    if (tsfp_guest_recovery_active()) return 0;
    tsfp_storage_lock(); Reservation *r=find(lease); int ok=r && r->state==TSFP_STORAGE_PREPARED;
    if (ok) rollback(r);
    tsfp_storage_unlock(); return ok;
}
enum TsfpStorageError tsfp_storage_launch(uint32_t lease,int *platform_error) {
    if (tsfp_guest_recovery_active()) return TSFP_STORAGE_INVALID;
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return TSFP_STORAGE_INVALID;
    tsfp_storage_lock(); Reservation *r=find(lease);
    if (!r || r->state!=TSFP_STORAGE_PREPARED) { tsfp_storage_unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return TSFP_STORAGE_INVALID; }
    r->state=TSFP_STORAGE_LAUNCHING; tsfp_storage_unlock();
    enum TsfpWorkerError error=tsfp_worker_launch(&r->worker,&r->spec);
    tsfp_storage_lock();
    if (platform_error) *platform_error=r->worker.platform_error;
    if (error!=TSFP_WORKER_OK) {
        rollback(r); tsfp_storage_unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return error==TSFP_WORKER_RESOURCE ? TSFP_STORAGE_EXHAUSTED : TSFP_STORAGE_LAUNCH_FAILED;
    }
    release(r->backup); r->backup=NULL; r->owner=r->worker.owner; r->platform_error=0; r->state=TSFP_STORAGE_RUNNING;
    tsfp_storage_unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return TSFP_STORAGE_OK;
}
int tsfp_storage_query(uint32_t lease,TsfpStorageInfo *out) {
    if (!out) return 0;
    tsfp_storage_lock(); Reservation *r=find(lease); int ok=r!=NULL;
    if (ok) {
        *out=(TsfpStorageInfo){r->spec,r->owner,r->state,r->platform_error};
    }
    tsfp_storage_unlock(); return ok;
}
enum TsfpWorkerError tsfp_storage_join(uint32_t lease,TsfpWorkerResult *out) {
    if (!out || tsfp_guest_recovery_active()) return TSFP_WORKER_INVALID;
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return TSFP_WORKER_PLATFORM;
    tsfp_storage_lock(); Reservation *r=find(lease);
    if (!r || r->state!=TSFP_STORAGE_RUNNING) { tsfp_storage_unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return TSFP_WORKER_BUSY; }
    r->state=TSFP_STORAGE_JOINING; tsfp_storage_unlock();
    enum TsfpWorkerError error=tsfp_worker_join(&r->worker,out);
    tsfp_storage_lock(); r->platform_error=r->worker.platform_error; r->state=error ? TSFP_STORAGE_RUNNING : TSFP_STORAGE_JOINED;
    tsfp_storage_unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return error;
}
int tsfp_storage_release(uint32_t lease) {
    if (tsfp_guest_recovery_active()) return 0;
    tsfp_storage_lock(); Reservation *r=find(lease);
    if (!r || r->state!=TSFP_STORAGE_JOINED) { tsfp_storage_unlock(); return 0; }
    r->state=TSFP_STORAGE_RELEASING; tsfp_storage_unlock();
    int ok=tsfp_worker_release(&r->worker);
    tsfp_storage_lock(); if (ok) clear(r); else r->state=TSFP_STORAGE_JOINED;
    tsfp_storage_unlock(); return ok;
}

int tsfp_storage_service_pin(TsfpStorageProfile *out) {
    if (!out || tsfp_guest_recovery_active()) return 0;
    tsfp_storage_lock(); int ok=configured && !service_pin;
    uint32_t block,index;
    if (ok && tsfp_tls_layout(source.tls.raw_size,source.tls.zero_fill,&block,&index)) {
        size_t metadata=(64u+block+source.thread_data_size+63u)&~(size_t)63u;
        if (metadata+512u>=config.stride) ok=0;
        else {
            *out=(TsfpStorageProfile){source.initial_id,(uint32_t)(config.stride-metadata-512u)&~63u,source.thread_data_size,config.native_stack_size};
            service_pin=1;
        }
    } else ok=0;
    tsfp_storage_unlock(); return ok;
}
int tsfp_storage_service_unpin(void) {
    if (tsfp_guest_recovery_active()) return 0;
    tsfp_storage_lock(); int ok=service_pin;
    if (ok) service_pin=0;
    tsfp_storage_unlock(); return ok;
}
int tsfp_storage_output_allowed(uint32_t address,size_t size) {
    tsfp_storage_lock(); int ok=!tsfp_heap_overlaps(address,size) && configured && service_pin &&
        !tsfp_tls_ranges_overlap(config.base,(size_t)config.stride*config.slots,address,size) &&
        !tsfp_tls_ranges_overlap(source.tls.raw_start,source.tls.raw_size,address,size) &&
        !tsfp_tls_ranges_overlap(source.object_template,512,address,size);
    tsfp_storage_unlock(); return ok;
}

int tsfp_storage_kernel_output_allowed(uint32_t address,size_t size) {
    tsfp_storage_lock(); int ok=!tsfp_heap_overlaps(address,size) && tsfp_storage_range_allowed_locked(address,size);
    tsfp_storage_unlock(); return ok;
}
int tsfp_storage_range_allowed_locked(uint32_t address,size_t size) {
    return !configured ||
        (!tsfp_tls_ranges_overlap(config.base,(size_t)config.stride*config.slots,address,size) &&
         !tsfp_tls_ranges_overlap(source.tls.raw_start,source.tls.raw_size,address,size) &&
         !tsfp_tls_ranges_overlap(source.object_template,512,address,size));
}

int tsfp_storage_event_output_allowed(uint32_t address,size_t size) {
    tsfp_storage_lock(); int ok=tsfp_storage_range_allowed_locked(address,size);
    tsfp_storage_unlock(); return ok;
}
