/* SPDX-License-Identifier: GPL-3.0-only */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "worker.h"
#include "handles.h"
#include "storage.h"
#include "memory.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static TsfpWorker *workers[TSFP_THREAD_HANDLES];
static void lock(void) { tsfp_storage_lock(); }
static void unlock(void) { tsfp_storage_unlock(); }
static int overlap(const TsfpWorkerSpec *a,const TsfpWorkerSpec *b) {
    uint32_t aa[]={a->stack,a->tib,a->object},bb[]={b->stack,b->tib,b->object};
    size_t as[]={a->stack_size,a->tls_size,512},bs[]={b->stack_size,b->tls_size,512};
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j)
        if (tsfp_tls_ranges_overlap(aa[i],as[i],bb[j],bs[j])) return 1;
    return 0;
}
static int valid(const TsfpWorkerSpec *s) {
    if (!s->resolve || !s->id || s->stack_size<16 || s->tls_size<68 ||
        s->tls_size>65536 || s->native_stack_size<65536 || s->native_stack_size>1024*1024 ||
        (s->stack&3u) || (s->stack_size&3u) || (s->tib&3u) || (s->object&3u)) return 0;
    uint32_t addresses[]={s->stack,s->tib,s->object}; size_t sizes[]={s->stack_size,s->tls_size,512};
    for (unsigned i=0;i<3;++i) {
        if (addresses[i]>=0x74000000u || sizes[i]>0x74000000u-addresses[i] || !tsfp_guest_range_valid(addresses[i],sizes[i]) ||
            !tsfp_guest_access_allowed(addresses[i],sizes[i],3)) return 0;
        for (unsigned j=0;j<i;++j)
            if (tsfp_tls_ranges_overlap(addresses[i],sizes[i],addresses[j],sizes[j])) return 0;
    }
    if (!tsfp_guest_range_valid(s->entry,1) || !tsfp_guest_access_allowed(s->entry,1,4)) return 0;
    return 1;
}
static int identity(const TsfpWorkerSpec *s) {
    uint32_t end=tsfp_guest_load_u32(s->tib+4);
    return end>=s->tib+68 && end<=(uint64_t)s->tib+s->tls_size &&
        tsfp_guest_load_u32(s->tib+0x28)==s->object && tsfp_guest_load_u32(s->object+0x28)==s->tib+64 &&
        tsfp_guest_load_u32(s->object+0x12c)==s->id;
}
static void invoke(void *context) {
    TsfpWorker *w=context;
    PUSH32(g_esp,w->spec.context2); PUSH32(g_esp,w->spec.context1); PUSH32(g_esp,0);
    g_seh_ebp=g_esp; w->function();
}
static void *run(void *context) {
    TsfpWorker *w=context;
    /* No native cancellation may abandon a guest owner. Guest exit is captured. */
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL)) abort();
    w->result.status=0xc0000001u; w->result.event=-1;
    if (tsfp_guest_thread_enter(w->spec.id,w->spec.stack,w->spec.stack_size,w->spec.tib,w->spec.tls_size)) {
        w->result.event=tsfp_guest_run(invoke,w,&w->result.fault);
        tsfp_cpu_capture(&w->result.cpu);
        if (!w->result.event) w->result.status=0; /* PC native thread returns zero. */
        else if (w->result.fault.kind==TSFP_THREAD_EXIT) w->result.status=w->result.fault.detail;
        if (!tsfp_guest_thread_leave()) abort();
    }
    if (!tsfp_thread_owner_finish(w->owner,w->result.status)) abort();
    return NULL;
}
enum TsfpWorkerError tsfp_worker_launch(TsfpWorker *w,const TsfpWorkerSpec *input) {
    if (!w || !input || tsfp_guest_recovery_active()) return TSFP_WORKER_INVALID;
    lock(); int busy=w->state!=0; unlock();
    if (busy) return TSFP_WORKER_BUSY;
    TsfpWorkerSpec spec=*input;
    if (!valid(&spec)) return TSFP_WORKER_INVALID;
    TsfpImageFunction function=spec.resolve(spec.entry);
    if (!function) return TSFP_WORKER_INVALID;
    lock();
    if (w->state || !tsfp_storage_worker_allowed_locked(w,&spec)) { unlock(); return TSFP_WORKER_BUSY; }
    unsigned slot=TSFP_THREAD_HANDLES;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) {
        if (!workers[i]) { if (slot==TSFP_THREAD_HANDLES) slot=i; }
        else if (overlap(&spec,&workers[i]->spec)) { unlock(); return TSFP_WORKER_BUSY; }
    }
    if (slot==TSFP_THREAD_HANDLES) { unlock(); return TSFP_WORKER_RESOURCE; }
    w->state=1; w->platform_error=0; w->spec=spec; workers[slot]=w; unlock();
    if (!identity(&spec)) {
        lock(); workers[slot]=NULL; memset(w,0,sizeof(*w)); unlock(); return TSFP_WORKER_INVALID;
    }
    uint32_t owner;
    if (!tsfp_thread_owner_create_storage(spec.object,spec.id,spec.storage_lease,&owner)) {
        lock(); workers[slot]=NULL; memset(w,0,sizeof(*w)); unlock(); return TSFP_WORKER_RESOURCE;
    }
    w->owner=owner; w->function=function; memset(&w->result,0,sizeof(w->result));
    if (!tsfp_thread_owner_native_pin(owner) || !tsfp_thread_owner_start(owner)) abort();
    int error=tsfp_native_launch(&w->native,spec.native_stack_size,run,w);
    if (error) {
        if (!tsfp_thread_handle_close(owner) && tsfp_thread_owner_finish(owner,0xc0000001u) &&
            tsfp_thread_owner_native_unpin(owner) && tsfp_thread_owner_release_storage(owner,spec.storage_lease)) {
            lock(); workers[slot]=NULL; memset(w,0,sizeof(*w)); w->platform_error=error; unlock(); return TSFP_WORKER_PLATFORM;
        }
        abort();
    }
    lock(); w->state=2; unlock(); return TSFP_WORKER_OK;
}
enum TsfpWorkerError tsfp_worker_join(TsfpWorker *w,TsfpWorkerResult *output) {
    if (!w || !output || tsfp_guest_recovery_active()) return TSFP_WORKER_INVALID;
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return TSFP_WORKER_PLATFORM;
    lock();
    if (w->state!=2) { unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return TSFP_WORKER_BUSY; }
    w->state=3; unlock();
    int error=tsfp_native_join(&w->native);
    if (!error && !tsfp_thread_owner_native_unpin(w->owner)) abort();
    lock();
    if (!error) *output=w->result;
    w->platform_error=error; w->state=error ? 2 : 4; unlock();
    if (pthread_setcancelstate(old_cancel,NULL)) abort();
    return error ? TSFP_WORKER_PLATFORM : TSFP_WORKER_OK;
}
int tsfp_worker_release(TsfpWorker *w) {
    if (!w || tsfp_guest_recovery_active()) return 0;
    lock();
    if (w->state!=4 || !tsfp_thread_owner_release_storage(w->owner,w->spec.storage_lease)) { unlock(); return 0; }
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (workers[i]==w) workers[i]=NULL;
    memset(w,0,sizeof(*w)); unlock(); return 1;
}

int tsfp_worker_range_busy_locked(uint32_t base,size_t size) {
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (workers[i]) {
        TsfpWorkerSpec *s=&workers[i]->spec;
        if (tsfp_tls_ranges_overlap(base,size,s->stack,s->stack_size) ||
            tsfp_tls_ranges_overlap(base,size,s->tib,s->tls_size) || tsfp_tls_ranges_overlap(base,size,s->object,512)) return 1;
    }
    return 0;
}
