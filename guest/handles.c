/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "handles.h"
#include "memory.h"
#include "tls.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#define TAG UINT32_C(0x48000000) /* Pinned PC's opaque 32-bit handle namespace. */
#define MAX_GENERATION UINT32_C(0x3ffff)
typedef struct Record { uint32_t generation; TsfpThreadInfo info; } Record;
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
    return r->info.state && r->generation==((token&0xffffffu)>>6) ? r : NULL;
}
int tsfp_thread_owner_create(uint32_t object,uint32_t id,uint32_t *token) {
    /* Guest checks precede locking: recovery must never unwind a held mutex. */
    if (!token || !id || !tsfp_guest_range_valid(object,0x200) || !tsfp_guest_access_allowed(object,0x200,1)) return 0;
    if (tsfp_guest_load_u32(object+0x12c)!=id) return 0;
    lock();
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i)
        if (records[i].info.state && (tsfp_tls_ranges_overlap(records[i].info.object,0x200,object,0x200) || records[i].info.id==id)) { unlock(); return 0; }
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) {
        Record *r=&records[i];
        if (!r->info.state && r->generation<MAX_GENERATION) {
            ++r->generation;
            r->info=(TsfpThreadInfo){object,id,0,TSFP_THREAD_CREATED,1,0};
            uint32_t value=TAG|(r->generation<<6)|(i+1); unlock(); *token=value; return 1;
        }
    }
    unlock(); return 0;
}
int tsfp_thread_owner_start(uint32_t token) {
    lock(); Record *r=find(token); int ok=r && r->info.state==TSFP_THREAD_CREATED;
    if (ok) r->info.state=TSFP_THREAD_RUNNING;
    unlock(); return ok;
}
int tsfp_thread_owner_finish(uint32_t token,uint32_t status) {
    lock(); Record *r=find(token); int ok=r && r->info.state!=TSFP_THREAD_TERMINATED;
    if (ok) { r->info.state=TSFP_THREAD_TERMINATED; r->info.status=status;
        if (condition_ready && pthread_cond_broadcast(&completion)) abort();
    }
    unlock(); return ok;
}
int tsfp_thread_owner_query(uint32_t token,TsfpThreadInfo *info) {
    if (!info) return 0;
    lock(); Record *r=find(token); int ok=r!=NULL;
    TsfpThreadInfo value={0}; if (ok) value=r->info;
    unlock(); if (ok) *info=value; return ok;
}
int tsfp_thread_owner_release(uint32_t token) {
    lock(); Record *r=find(token);
    int ok=r && !r->info.handle_open && r->info.state==TSFP_THREAD_TERMINATED && !r->info.waiters;
    if (ok) r->info=(TsfpThreadInfo){0}; /* generation is never rolled back */
    unlock(); return ok;
}
uint32_t tsfp_thread_handle_close(uint32_t handle) {
    lock(); Record *r=find(handle); int ok=r && r->info.handle_open;
    if (ok) r->info.handle_open=0;
    unlock(); return ok ? 0 : TSFP_STATUS_INVALID_HANDLE;
}
uint32_t tsfp_thread_handle_poll(uint32_t handle) {
    lock(); Record *r=find(handle);
    uint32_t status=TSFP_STATUS_INVALID_HANDLE;
    if (r && r->info.handle_open) status=r->info.state==TSFP_THREAD_TERMINATED ? 0 : 0x102u;
    unlock(); return status;
}
int tsfp_thread_handles_busy(void) {
    lock(); int busy=0;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (records[i].info.state) { busy=1; break; }
    unlock(); return busy;
}

int tsfp_thread_timeout_decode(int present,uint64_t bits,TsfpWaitTimeout *out) {
    if (!out || (present && bits && !(bits>>63))) return 0;
    *out=(TsfpWaitTimeout){present ? 0u-bits : 0,!present}; return 1;
}
uint32_t tsfp_thread_handle_wait(uint32_t handle,const TsfpWaitTimeout *timeout) {
    const uint32_t failed=UINT32_C(0xc0000001),quota=UINT32_C(0xc000009a);
    if (!timeout) return failed;
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return failed;
    uint32_t status=failed;
    struct timespec start={0}; uint64_t end_sec=0; long end_ns=0;
    int blocking=timeout->infinite || timeout->ticks;
    if (blocking) {
        if (pthread_once(&condition_once,init_condition) || condition_error) goto done;
        if (!timeout->infinite) {
            if (clock_gettime(CLOCK_MONOTONIC,&start) || start.tv_sec<0 || start.tv_nsec<0 || start.tv_nsec>=1000000000L) goto done;
            end_sec=(uint64_t)start.tv_sec+timeout->ticks/10000000u;
            end_ns=start.tv_nsec+(long)(timeout->ticks%10000000u)*100L;
            if (end_ns>=1000000000L) { ++end_sec; end_ns-=1000000000L; }
        }
    }
    lock(); Record *r=find(handle);
    if (!r || !r->info.handle_open) { status=TSFP_STATUS_INVALID_HANDLE; unlock(); goto done; }
    if (r->info.waiters==64) { status=quota; unlock(); goto done; }
    ++r->info.waiters; if (blocking) condition_ready=1;
    for (;;) {
        if (r->info.state==TSFP_THREAD_TERMINATED) { status=0; break; }
        if (!blocking) { status=0x102u; break; }
        int error;
        if (timeout->infinite) error=pthread_cond_wait(&completion,&mutex);
        else {
            struct timespec now,deadline;
            if (clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 || now.tv_nsec<0 || now.tv_nsec>=1000000000L) break;
            uint64_t seconds=(uint64_t)now.tv_sec;
            if (seconds>end_sec || (seconds==end_sec && now.tv_nsec>=end_ns)) { status=0x102u; break; }
            /* Bounded sleeps avoid overflowing a 32-bit time_t on long waits. */
            uint64_t next=seconds+1; long ns=now.tv_nsec;
            if (next>end_sec || (next==end_sec && ns>end_ns)) { next=end_sec; ns=end_ns; }
            deadline.tv_sec=(time_t)next; deadline.tv_nsec=ns;
            if (deadline.tv_sec<0 || (uint64_t)deadline.tv_sec!=next) break;
            error=pthread_cond_timedwait(&completion,&mutex,&deadline);
        }
        if (error && error!=ETIMEDOUT) break;
    }
    --r->info.waiters; unlock();
done:
    /* All native resources are safe before a pending cancellation can run. */
    if (pthread_setcancelstate(old_cancel,NULL)) abort();
    return status;
}
