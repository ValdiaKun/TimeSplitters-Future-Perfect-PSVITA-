/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "handles.h"
#include "memory.h"
#include "tls.h"
#include "storage.h"
#include "execution.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#define TAG UINT32_C(0x48000000) /* Pinned PC's opaque 32-bit handle namespace. */
#define MAX_GENERATION UINT32_C(0x3ffff)
enum { KIND_THREAD=1,KIND_EVENT };
typedef struct Waiter { struct Waiter *next; int granted,expired,finite; uint64_t end_sec; long end_ns; } Waiter;
typedef struct Record {
    uint32_t generation,storage_lease; unsigned kind,event_type,signaled;
    TsfpThreadInfo info; Waiter *first,*last;
} Record;
static void retire(Record *r) {
    uint32_t generation=r->generation; *r=(Record){.generation=generation};
}
static Record records[TSFP_THREAD_HANDLES];
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t completion;
static pthread_once_t condition_once=PTHREAD_ONCE_INIT;
static int condition_error,condition_ready;
static void init_condition(void) {
    pthread_condattr_t attr;
    condition_error=pthread_condattr_init(&attr);
    if (condition_error) return;
    condition_error=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
    if (!condition_error) condition_error=pthread_cond_init(&completion,&attr);
    if (pthread_condattr_destroy(&attr)) abort();
}
static void lock(void) { if (pthread_mutex_lock(&mutex)) abort(); }
static void unlock(void) { if (pthread_mutex_unlock(&mutex)) abort(); }
static Record *find(uint32_t token) {
    unsigned slot=(token&63u);
    if ((token&0xff000000u)!=TAG || !slot || slot>TSFP_THREAD_HANDLES) return NULL;
    Record *r=&records[slot-1];
    return r->kind && r->generation==((token&0xffffffu)>>6) ? r : NULL;
}
int tsfp_thread_owner_create(uint32_t object,uint32_t id,uint32_t *token) {
    return tsfp_thread_owner_create_storage(object,id,0,token);
}
int tsfp_thread_owner_create_storage(uint32_t object,uint32_t id,uint32_t lease,uint32_t *token) {
    if (!token || !id || tsfp_guest_recovery_active() || !tsfp_guest_range_valid(object,0x200) ||
        !tsfp_guest_access_allowed(object,0x200,1)) return 0;
    tsfp_storage_lock();
    if (!tsfp_storage_object_allowed_locked(object,id,lease) || tsfp_guest_load_u32(object+0x12c)!=id) {
        tsfp_storage_unlock(); return 0;
    }
    int ok=0; uint32_t value=0;
    lock();
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i)
        if (records[i].kind==KIND_THREAD && (tsfp_tls_ranges_overlap(records[i].info.object,0x200,object,0x200) || records[i].info.id==id)) goto done;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) {
        Record *r=&records[i];
        if (!r->kind && r->generation<MAX_GENERATION) {
            ++r->generation; r->storage_lease=lease; r->kind=KIND_THREAD;
            r->info=(TsfpThreadInfo){object,id,0,TSFP_THREAD_CREATED,1,0,0};
            value=TAG|(r->generation<<6)|(i+1); ok=1; break;
        }
    }
done:
    unlock(); tsfp_storage_unlock(); if (ok) *token=value; return ok;
}

int tsfp_thread_owner_native_pin(uint32_t token) {
    lock(); Record *r=find(token); int ok=r && r->kind==KIND_THREAD && r->info.state==TSFP_THREAD_CREATED && !r->info.native_pin;
    if (ok) r->info.native_pin=1;
    unlock(); return ok;
}
int tsfp_thread_owner_native_unpin(uint32_t token) {
    lock(); Record *r=find(token); int ok=r && r->kind==KIND_THREAD && r->info.state==TSFP_THREAD_TERMINATED && r->info.native_pin;
    if (ok) r->info.native_pin=0;
    unlock(); return ok;
}
int tsfp_thread_owner_start(uint32_t token) {
    lock(); Record *r=find(token); int ok=r && r->kind==KIND_THREAD && r->info.state==TSFP_THREAD_CREATED;
    if (ok) r->info.state=TSFP_THREAD_RUNNING;
    unlock(); return ok;
}
int tsfp_thread_owner_finish(uint32_t token,uint32_t status) {
    lock(); Record *r=find(token); int ok=r && r->kind==KIND_THREAD && r->info.state!=TSFP_THREAD_TERMINATED;
    if (ok) { r->info.state=TSFP_THREAD_TERMINATED; r->info.status=status;
        if (condition_ready && pthread_cond_broadcast(&completion)) abort();
    }
    unlock(); return ok;
}
int tsfp_thread_owner_query(uint32_t token,TsfpThreadInfo *info) {
    if (!info) return 0;
    lock(); Record *r=find(token); int ok=r && r->kind==KIND_THREAD;
    TsfpThreadInfo value={0}; if (ok) value=r->info;
    unlock(); if (ok) *info=value; return ok;
}
int tsfp_thread_owner_release(uint32_t token) { return tsfp_thread_owner_release_storage(token,0); }
int tsfp_thread_owner_release_storage(uint32_t token,uint32_t lease) {
    lock(); Record *r=find(token);
    int ok=r && r->kind==KIND_THREAD && r->storage_lease==lease && !r->info.handle_open && r->info.state==TSFP_THREAD_TERMINATED && !r->info.waiters && !r->info.native_pin;
    if (ok) retire(r); /* generation is never rolled back */
    unlock(); return ok;
}
static uint32_t close_handle(uint32_t handle,unsigned required) {
    lock(); Record *r=find(handle);
    uint32_t status=TSFP_STATUS_INVALID_HANDLE;
    if (r && r->info.handle_open) {
        if (required && r->kind!=required) status=TSFP_STATUS_INVALID_HANDLE;
        else {
            r->info.handle_open=0; status=0;
            if (r->kind==KIND_EVENT && !r->info.waiters) retire(r);
        }
    }
    unlock(); return status;
}
uint32_t tsfp_thread_handle_close(uint32_t handle) { return close_handle(handle,KIND_THREAD); }
uint32_t tsfp_handle_close(uint32_t handle) { return close_handle(handle,0); }
uint32_t tsfp_thread_handle_poll(uint32_t handle) {
    lock(); Record *r=find(handle); uint32_t status=TSFP_STATUS_INVALID_HANDLE;
    if (r && r->info.handle_open) status=r->kind!=KIND_THREAD ? TSFP_STATUS_INVALID_HANDLE : r->info.state==TSFP_THREAD_TERMINATED ? 0 : 0x102u;
    unlock(); return status;
}
int tsfp_thread_handles_busy(void) {
    lock(); int busy=0;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (records[i].kind) { busy=1; break; }
    unlock(); return busy;
}

int tsfp_thread_timeout_decode(int present,uint64_t bits,TsfpWaitTimeout *out) {
    if (!out || (present && bits && !(bits>>63))) return 0;
    *out=(TsfpWaitTimeout){present ? 0u-bits : 0,!present}; return 1;
}
static int ready(Record *r,Waiter *w) {
    if (r->kind==KIND_THREAD) return r->info.state==TSFP_THREAD_TERMINATED;
    if (w->granted) return 1;
    if (w->expired) return 0;
    if (!r->signaled) return 0;
    if (r->event_type==1) r->signaled=0;
    return 1;
}
static void unlink_waiter(Record *r,Waiter *w) {
    Waiter **link=&r->first,*previous=NULL;
    while (*link && *link!=w) { previous=*link; link=&(*link)->next; }
    if (!*link) abort();
    *link=w->next; if (r->last==w) r->last=previous;
}
static uint32_t wait_handle(uint32_t handle,const TsfpWaitTimeout *timeout,unsigned required) {
    const uint32_t failed=UINT32_C(0xc0000001),quota=UINT32_C(0xc000009a);
    if (!timeout) return failed;
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return failed;
    uint32_t status=failed; Waiter waiter={0}; int linked=0;
    lock(); Record *r=find(handle);
    if (!r || !r->info.handle_open) { status=TSFP_STATUS_INVALID_HANDLE; goto unlock; }
    if (required && r->kind!=required) { status=TSFP_STATUS_INVALID_HANDLE; goto unlock; }
    if (r->info.waiters==64) { status=quota; goto unlock; }
    ++r->info.waiters;
    if (ready(r,&waiter)) { status=0; goto release; }
    if (!timeout->infinite && !timeout->ticks) { status=0x102u; goto release; }
    if (r->kind==KIND_EVENT) {
        if (r->last) r->last->next=&waiter; else r->first=&waiter;
        r->last=&waiter; linked=1;
    }
    if (pthread_once(&condition_once,init_condition) || condition_error) goto release;
    condition_ready=1;
    uint64_t end_sec=0; long end_ns=0;
    if (!timeout->infinite) {
        struct timespec start;
        if (clock_gettime(CLOCK_MONOTONIC,&start) || start.tv_sec<0 || start.tv_nsec<0 || start.tv_nsec>=1000000000L) goto release;
        end_sec=(uint64_t)start.tv_sec+timeout->ticks/10000000u;
        end_ns=start.tv_nsec+(long)(timeout->ticks%10000000u)*100L;
        if (end_ns>=1000000000L) { ++end_sec; end_ns-=1000000000L; }
    }
    waiter.finite=!timeout->infinite; waiter.end_sec=end_sec; waiter.end_ns=end_ns;
    int native_error=0;
    for (;;) {
        if (ready(r,&waiter)) { status=0; break; }
        if (waiter.expired) { status=0x102u; break; }
        if (native_error) break;
        int error;
        if (timeout->infinite) error=pthread_cond_wait(&completion,&mutex);
        else {
            struct timespec now,deadline;
            if (clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 || now.tv_nsec<0 || now.tv_nsec>=1000000000L) break;
            uint64_t seconds=(uint64_t)now.tv_sec;
            if (seconds>end_sec || (seconds==end_sec && now.tv_nsec>=end_ns)) { status=0x102u; break; }
            uint64_t next=seconds+1; long ns=now.tv_nsec;
            if (next>end_sec || (next==end_sec && ns>end_ns)) { next=end_sec; ns=end_ns; }
            deadline.tv_sec=(time_t)next; deadline.tv_nsec=ns;
            if (deadline.tv_sec<0 || (uint64_t)deadline.tv_sec!=next) break;
            error=pthread_cond_timedwait(&completion,&mutex,&deadline);
        }
        native_error=error && error!=ETIMEDOUT;
    }
release:
    if (linked) unlink_waiter(r,&waiter);
    --r->info.waiters;
    if (r->kind==KIND_EVENT && !r->info.handle_open && !r->info.waiters) retire(r);
unlock:
    unlock();
    if (pthread_setcancelstate(old_cancel,NULL)) abort();
    return status;
}
uint32_t tsfp_thread_handle_wait(uint32_t handle,const TsfpWaitTimeout *t) { return wait_handle(handle,t,KIND_THREAD); }
uint32_t tsfp_handle_wait(uint32_t handle,const TsfpWaitTimeout *t) { return wait_handle(handle,t,0); }
uint32_t tsfp_event_create(unsigned type,int initial,uint32_t *handle) {
    if (!handle || type>1) return UINT32_C(0xc000000d);
    uint32_t value=0,status=UINT32_C(0xc000009a);
    lock();
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) {
        Record *r=&records[i];
        if (!r->kind && r->generation<MAX_GENERATION) {
            ++r->generation; r->kind=KIND_EVENT; r->event_type=type; r->signaled=!!initial;
            r->info.handle_open=1; value=TAG|(r->generation<<6)|(i+1); status=0; break;
        }
    }
    unlock(); if (!status) *handle=value; return status;
}
uint32_t tsfp_event_set(uint32_t handle,uint32_t *previous) {
    uint32_t old=0,status=TSFP_STATUS_INVALID_HANDLE;
    lock(); Record *r=find(handle);
    if (r && r->info.handle_open) {
        if (r->kind!=KIND_EVENT) status=TSFP_STATUS_INVALID_HANDLE;
        else {
            struct timespec now={0}; int needs_clock=0,wake=0;
            for (Waiter *w=r->first;w;w=w->next) if (w->finite && !w->granted && !w->expired) needs_clock=1;
            if (needs_clock && (clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 || now.tv_nsec<0 || now.tv_nsec>=1000000000L)) {
                status=UINT32_C(0xc0000001); goto finish_set;
            }
            /* A late signal must not be assigned to a timed-out waiter whose
             * native condition return has not yet reacquired this mutex. */
            for (Waiter *w=r->first;w;w=w->next) if (w->finite && !w->granted && !w->expired &&
                ((uint64_t)now.tv_sec>w->end_sec || ((uint64_t)now.tv_sec==w->end_sec && now.tv_nsec>=w->end_ns))) {
                w->expired=1; wake=1;
            }
            status=0; old=r->signaled;
            if (!r->event_type) {
                r->signaled=1;
                for (Waiter *w=r->first;w;w=w->next) if (!w->granted && !w->expired) { w->granted=1; wake=1; }
            } else {
                Waiter *w=r->first; while (w && (w->granted || w->expired)) w=w->next;
                if (w) { w->granted=1; wake=1; } else r->signaled=1;
            }
            if (wake && condition_ready && pthread_cond_broadcast(&completion)) abort();
        }
    }
finish_set:
    unlock(); if (!status && previous) *previous=old; return status;
}
uint32_t tsfp_event_clear(uint32_t handle) {
    uint32_t status=TSFP_STATUS_INVALID_HANDLE;
    lock(); Record *r=find(handle);
    if (r && r->info.handle_open) {
        if (r->kind!=KIND_EVENT) status=TSFP_STATUS_INVALID_HANDLE;
        else { r->signaled=0; status=0; }
    }
    unlock(); return status;
}
int tsfp_event_query(uint32_t handle,TsfpEventInfo *info) {
    if (!info) return 0;
    lock(); Record *r=find(handle); int ok=r && r->kind==KIND_EVENT; TsfpEventInfo out={0};
    if (ok) {
        out=(TsfpEventInfo){r->event_type,r->signaled,r->info.waiters,0,r->info.handle_open};
        for (Waiter *w=r->first;w;w=w->next) out.granted+=!!w->granted;
    }
    unlock(); if (ok) *info=out; return ok;
}

int tsfp_thread_id_busy(uint32_t id) {
    lock(); int busy=0;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (records[i].kind==KIND_THREAD && records[i].info.id==id) { busy=1; break; }
    unlock(); return busy;
}
