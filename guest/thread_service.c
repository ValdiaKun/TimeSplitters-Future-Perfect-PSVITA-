/* SPDX-License-Identifier: GPL-3.0-only */
#include "thread_service.h"
#include "storage.h"
#include "handles.h"
#include "execution.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define FAILED UINT32_C(0xc0000001)
enum { STOPPED,STARTING,RUNNING,STOPPING };
enum { CREATE=1,STOP };
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static TsfpNativeThread server;
static TsfpStorageProfile profile;
static uint32_t default_stack,next_id=1,leases[TSFP_THREAD_HANDLES];
static unsigned state,clients,active_calls;
static int busy,pending,done,operation,stop_control;
static TsfpThreadCreateRequest packet;
static TsfpThreadCreateResult reply;
typedef struct CallGuard { int active,old_cancel; } CallGuard;
static _Thread_local CallGuard call_guard;
static void lock(void) { if (pthread_mutex_lock(&mutex)) abort(); }
static void unlock(void) { if (pthread_mutex_unlock(&mutex)) abort(); }
static void signal_all(void) { if (pthread_cond_broadcast(&changed)) abort(); }
static void wait_changed(void) { if (pthread_cond_wait(&changed,&mutex)) abort(); }
static void cleanup_call(void *context) {
    CallGuard *g=context;
    if (!g->active) abort();
    lock(); if (!active_calls) abort(); --active_calls; g->active=0; signal_all(); unlock();
    if (pthread_setcancelstate(g->old_cancel,NULL)) abort();
}
int tsfp_thread_service_call_enter(void) {
    if (!tsfp_guest_recovery_active() || call_guard.active) return 0;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&call_guard.old_cancel)) return 0;
    lock(); int ok=state==RUNNING && active_calls<TSFP_THREAD_HANDLES;
    if (ok) { ++active_calls; call_guard.active=1; }
    unlock();
    if (!ok) { if (pthread_setcancelstate(call_guard.old_cancel,NULL)) abort(); return 0; }
    if (!tsfp_guest_scope_guard(cleanup_call,&call_guard)) { cleanup_call(&call_guard); return 0; }
    return 1;
}
void tsfp_thread_service_call_leave(void) {
    if (!tsfp_guest_scope_unguard(cleanup_call,&call_guard)) abort();
    cleanup_call(&call_guard);
}
/* Only the server thread touches its lease list. No guest frame owns these
 * records, including when a caller faults or terminates after creation. */
static int reap(int shutdown) {
    if (shutdown) {
        for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (leases[i]) {
            TsfpStorageInfo s; TsfpThreadInfo t;
            if (!tsfp_storage_query(leases[i],&s) || !tsfp_thread_owner_query(s.owner,&t)) abort();
            if (t.state!=TSFP_THREAD_TERMINATED || t.waiters) return 0;
        }
    }
    int all=1;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (leases[i]) {
        TsfpStorageInfo s; TsfpThreadInfo t;
        if (!tsfp_storage_query(leases[i],&s) || !tsfp_thread_owner_query(s.owner,&t)) abort();
        if (t.state!=TSFP_THREAD_TERMINATED || (!shutdown && t.handle_open)) { all=0; continue; }
        if (shutdown && t.handle_open && tsfp_thread_handle_close(s.owner)) abort();
        TsfpWorkerResult result;
        if (s.state==TSFP_STORAGE_RUNNING && tsfp_storage_join(leases[i],&result)!=TSFP_WORKER_OK) { all=0; continue; }
        if (tsfp_storage_release(leases[i])) leases[i]=0;
        else all=0;
    }
    return all;
}
static TsfpThreadCreateResult create(TsfpThreadCreateRequest request) {
    TsfpThreadCreateResult result={TSFP_STATUS_INVALID_PARAMETER,0,0};
    if (request.tls_size && request.tls_size!=profile.thread_data_size) { result.status=TSFP_STATUS_NOT_SUPPORTED; return result; }
    if (!request.stack_size) request.stack_size=default_stack;
    if (request.stack_size>profile.max_stack || request.stack_size<16 || (request.stack_size&15u)) return result;
    (void)reap(0);
    unsigned slot=TSFP_THREAD_HANDLES;
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) if (!leases[i]) { slot=i; break; }
    if (slot==TSFP_THREAD_HANDLES) { result.status=TSFP_STATUS_INSUFFICIENT_RESOURCES; return result; }
    uint32_t lease=0,id=0; enum TsfpStorageError error=TSFP_STORAGE_EXHAUSTED;
    for (unsigned attempt=0;attempt<2*TSFP_THREAD_HANDLES+2 && next_id;++attempt) {
        id=next_id++; if (id==profile.initial_id) continue;
        TsfpStorageRequest r={id,request.stack_size,request.entry,request.context1,request.context2};
        error=tsfp_storage_reserve(&r,&lease);
        if (error!=TSFP_STORAGE_BUSY) break;
    }
    if (error!=TSFP_STORAGE_OK) {
        result.status=error==TSFP_STORAGE_INVALID ? TSFP_STATUS_INVALID_PARAMETER : TSFP_STATUS_INSUFFICIENT_RESOURCES;
        return result;
    }
    int platform_error;
    enum TsfpStorageError launch_error=tsfp_storage_launch(lease,&platform_error);
    if (launch_error!=TSFP_STORAGE_OK) {
        result.status=(launch_error==TSFP_STORAGE_EXHAUSTED || platform_error==EAGAIN || platform_error==ENOMEM) ? TSFP_STATUS_INSUFFICIENT_RESOURCES : FAILED;
        return result;
    }
    leases[slot]=lease; /* ownership is retained BEFORE client publication */
    TsfpStorageInfo info; if (!tsfp_storage_query(lease,&info)) abort();
    return (TsfpThreadCreateResult){0,info.owner,id};
}
static void *serve(void *unused) {
    (void)unused; if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL)) abort();
    for (;;) {
        lock(); while (!pending) wait_changed();
        int op=operation; TsfpThreadCreateRequest request=packet; pending=0;
        int can_stop=op==STOP && !active_calls;
        if (can_stop) state=STOPPING;
        unlock();
        TsfpThreadCreateResult result={FAILED,0,0};
        int stopped=0;
        if (op==CREATE) result=create(request);
        else if (can_stop) { stopped=reap(1); result.status=stopped ? 0 : FAILED; }
        lock();
        if (op==STOP && !stopped) state=RUNNING;
        reply=result; done=1; signal_all(); unlock();
        if (stopped) return NULL;
    }
}
static TsfpThreadCreateResult submit(int op,const TsfpThreadCreateRequest *request) {
    TsfpThreadCreateResult result={TSFP_STATUS_NOT_SUPPORTED,0,0};
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return result;
    lock();
    if (state!=RUNNING || clients==TSFP_THREAD_HANDLES) {
        if (state==RUNNING) result.status=TSFP_STATUS_INSUFFICIENT_RESOURCES;
        unlock(); goto out;
    }
    ++clients;
    while (busy && state==RUNNING) wait_changed();
    if (state!=RUNNING) { --clients; signal_all(); unlock(); goto out; }
    busy=1; done=0; operation=op; packet=request ? *request : (TsfpThreadCreateRequest){0}; pending=1; signal_all();
    while (!done) wait_changed();
    result=reply; busy=0; --clients; signal_all(); unlock();
out:
    if (pthread_setcancelstate(old_cancel,NULL)) abort();
    return result;
}
TsfpThreadCreateResult tsfp_thread_service_create(const TsfpThreadCreateRequest *request) {
    if (!request) return (TsfpThreadCreateResult){TSFP_STATUS_INVALID_PARAMETER,0,0};
    if (tsfp_guest_recovery_active() && !call_guard.active) return (TsfpThreadCreateResult){TSFP_STATUS_NOT_SUPPORTED,0,0};
    return submit(CREATE,request);
}
int tsfp_thread_service_start(uint32_t stack,int *platform_error) {
    if (tsfp_guest_recovery_active()) return 0;
    lock(); if (state!=STOPPED) { unlock(); return 0; } state=STARTING; unlock();
    TsfpStorageProfile p;
    if (!tsfp_storage_service_pin(&p)) { lock(); state=STOPPED; unlock(); return 0; }
    if (stack<16 || (stack&15u) || stack>p.max_stack) {
        if (!tsfp_storage_service_unpin()) abort();
        lock(); state=STOPPED; unlock(); return 0;
    }
    profile=p; default_stack=stack;
    int error=tsfp_native_launch(&server,p.native_stack_size,serve,NULL);
    if (platform_error) *platform_error=error;
    if (error && !tsfp_storage_service_unpin()) abort();
    lock(); state=error ? STOPPED : RUNNING; signal_all(); unlock(); return !error;
}
int tsfp_thread_service_stop(int *platform_error) {
    if (tsfp_guest_recovery_active()) return 0;
    int old_cancel;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel)) return 0;
    lock();
    if ((state!=RUNNING && state!=STOPPING) || stop_control) { unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return 0; }
    int retry=state==STOPPING; stop_control=1; unlock();
    TsfpThreadCreateResult result=retry ? (TsfpThreadCreateResult){0,0,0} : submit(STOP,NULL);
    if (result.status) {
        lock(); stop_control=0; unlock(); if (pthread_setcancelstate(old_cancel,NULL)) abort(); return 0;
    }
    lock(); while (clients) wait_changed(); unlock();
    int error=tsfp_native_join(&server);
    if (platform_error) *platform_error=error;
    /* Failed join retains the pin and STOPPING state; stop can retry join. */
    if (!error && !tsfp_storage_service_unpin()) abort();
    lock(); state=error ? STOPPING : STOPPED; stop_control=0; unlock();
    if (pthread_setcancelstate(old_cancel,NULL)) abort();
    return !error;
}
