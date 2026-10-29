/* SPDX-License-Identifier: GPL-3.0-only */
#include "handles.h"
#include "memory.h"
#include "tls.h"
#include <pthread.h>
#include <stdlib.h>
#define TAG UINT32_C(0x48000000) /* Pinned PC's opaque 32-bit handle namespace. */
#define MAX_GENERATION UINT32_C(0x3ffff)
typedef struct Record { uint32_t generation; TsfpThreadInfo info; } Record;
static Record records[TSFP_THREAD_HANDLES];
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
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
            r->info=(TsfpThreadInfo){object,id,0,TSFP_THREAD_CREATED,1};
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
    if (ok) { r->info.state=TSFP_THREAD_TERMINATED; r->info.status=status; }
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
    int ok=r && !r->info.handle_open && r->info.state==TSFP_THREAD_TERMINATED;
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
