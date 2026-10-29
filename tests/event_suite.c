/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "handles.h"
#include "kernel.h"
#include "image.h"
#include "memory.h"
#include "storage.h"
#include "thread_service.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
static _Thread_local uint32_t stack_top,close_before_wait;
static _Thread_local int pause_recheck,fail_infinite;
static struct { pthread_mutex_t mutex; pthread_cond_t changed; unsigned awake; int release; pthread_cond_t *object; } gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER};
void test_wait_advance_clock(unsigned);
void fixture_thread_args(void);
static TsfpImageFunction resolve(uint32_t address) { return address==0x13200 ? fixture_thread_args : NULL; }
void test_wait_fail_clock(void); void test_wait_fail_condition(void);
void fixture_service_call(void); void fixture_event_sequence(void);
static void invoke(void *context) { (*(TsfpImageFunction *)context)(); }
static uint32_t call(unsigned slot,unsigned count,const uint32_t *args,TsfpFault *fault) {
    uint32_t stack=stack_top-64; g_esp=stack; g_esi=0x900e0+slot*4; g_eax=77;
    tsfp_guest_write(stack,args,count*4); tsfp_guest_store_u32(stack+count*4,0);
    TsfpCpuState before,after; tsfp_cpu_capture(&before);
    TsfpImageFunction fn=fixture_service_call;
    int result=tsfp_guest_run(invoke,&fn,fault);
    if (result) return UINT32_MAX;
    tsfp_cpu_capture(&after);
    assert(g_esp==stack+(count+1)*4 && before.flags==after.flags && before.fs_base==after.fs_base);
    return g_eax;
}
static uint32_t create(unsigned type,unsigned initial) {
    const uint32_t args[]={0x90100,0,type,initial}; TsfpFault fault;
    assert(call(0,4,args,&fault)==0); return tsfp_guest_load_u32(0x90100);
}
static uint32_t set(uint32_t h) {
    const uint32_t args[]={h,0x90104}; TsfpFault fault; return call(1,2,args,&fault);
}
static uint32_t one(unsigned slot,uint32_t h) { TsfpFault f; return call(slot,1,&h,&f); }
static uint32_t poll(uint32_t h) {
    const uint32_t args[]={h,0,0x90108}; TsfpFault f; tsfp_guest_store_u64(0x90108,0); return call(3,3,args,&f);
}
void test_event_before_wait(pthread_cond_t *condition,pthread_mutex_t *mutex) {
    if (pause_recheck) { assert(!pthread_mutex_lock(&gate.mutex)); gate.object=condition; assert(!pthread_mutex_unlock(&gate.mutex)); }
    if (!close_before_wait) return;
    uint32_t h=close_before_wait; close_before_wait=0;
    assert(!pthread_mutex_unlock(mutex));
    assert(tsfp_handle_close(h)==0 && tsfp_thread_handles_busy());
    TsfpEventInfo info; assert(tsfp_event_query(h,&info) && !info.handle_open && info.waiters==1);
    assert(!pthread_mutex_lock(mutex));
}
void test_event_after_wait(pthread_mutex_t *mutex) {
    if (!pause_recheck) return;
    pause_recheck=0; assert(!pthread_mutex_unlock(mutex));
    assert(!pthread_mutex_lock(&gate.mutex)); ++gate.awake; assert(!pthread_cond_broadcast(&gate.changed));
    while (!gate.release) assert(!pthread_cond_wait(&gate.changed,&gate.mutex));
    assert(!pthread_mutex_unlock(&gate.mutex) && !pthread_mutex_lock(mutex));
}
int __real_pthread_cond_wait(pthread_cond_t *,pthread_mutex_t *);
int __wrap_pthread_cond_wait(pthread_cond_t *c,pthread_mutex_t *m) {
    if (fail_infinite) { fail_infinite=0; return EINVAL; }
    test_event_before_wait(c,m); int error=__real_pthread_cond_wait(c,m);
    if (!error) test_event_after_wait(m);
    return error;
}
static void reset_gate(void) {
    assert(!pthread_mutex_lock(&gate.mutex)); gate.awake=0; gate.release=0; gate.object=NULL; assert(!pthread_mutex_unlock(&gate.mutex));
}
static void await_gate(unsigned count) {
    struct timespec end; assert(!clock_gettime(CLOCK_REALTIME,&end)); end.tv_sec+=5;
    assert(!pthread_mutex_lock(&gate.mutex));
    while (gate.awake<count) assert(!pthread_cond_timedwait(&gate.changed,&gate.mutex,&end));
    assert(!pthread_mutex_unlock(&gate.mutex));
}
static void release_gate(void) {
    assert(!pthread_mutex_lock(&gate.mutex)); gate.release=1; assert(!pthread_cond_broadcast(&gate.changed) && !pthread_mutex_unlock(&gate.mutex));
}
static void spurious_wake(void) {
    assert(!pthread_mutex_lock(&gate.mutex)); pthread_cond_t *c=gate.object; assert(c); assert(!pthread_mutex_unlock(&gate.mutex));
    assert(!pthread_cond_broadcast(c));
}
static void await_refs(uint32_t h,unsigned count) {
    struct timespec start,now; assert(!clock_gettime(CLOCK_MONOTONIC,&start));
    for (;;) {
        TsfpEventInfo info; assert(tsfp_event_query(h,&info)); if (info.waiters==count) return;
        assert(!clock_gettime(CLOCK_MONOTONIC,&now) && now.tv_sec-start.tv_sec<5);
        struct timespec delay={0,100000}; assert(!nanosleep(&delay,NULL));
    }
}
typedef struct WaitContext { uint32_t handle,status; unsigned slot; uint64_t ticks; } WaitContext;
static void *waiter(void *context) {
    WaitContext *w=context; unsigned slot=w->slot;
    uint32_t tib=0x89100+slot*0x100,stack=0x91000+slot*0x1000;
    assert(tsfp_guest_thread_enter(700+slot,stack,0x1000,tib,164)); stack_top=stack+0x1000;
    uint32_t timeout=0;
    if (w->ticks) { timeout=tib+0xa8; tsfp_guest_store_u64(timeout,0u-w->ticks); }
    uint32_t args[]={w->handle,0,timeout}; TsfpFault fault; pause_recheck=1;
    w->status=call(3,3,args,&fault); assert(tsfp_guest_thread_leave()); return NULL;
}
static void group(unsigned type) {
    uint32_t h=create(type,0); pthread_t threads[3]; WaitContext contexts[3]; reset_gate();
    for (unsigned i=0;i<3;++i) { contexts[i]=(WaitContext){h,99,i,0}; assert(!pthread_create(&threads[i],NULL,waiter,&contexts[i])); }
    await_refs(h,3); assert(set(h)==0); await_gate(3);
    TsfpEventInfo info; assert(tsfp_event_query(h,&info) && info.granted==(type ? 1u : 3u));
    assert(one(2,h)==0);
    if (type) {
        assert(set(h)==0 && tsfp_event_query(h,&info) && info.granted==2);
        assert(set(h)==0 && tsfp_event_query(h,&info) && info.granted==3);
        assert(one(2,h)==0);
    }
    assert(one(4,h)==0 && tsfp_event_query(h,&info) && !info.handle_open && info.waiters==3);
    assert(tsfp_guest_thread_leave() && !tsfp_image_reset()); /* event refs are the only registry pins */
    release_gate();
    for (unsigned i=0;i<3;++i) assert(!pthread_join(threads[i],NULL) && contexts[i].status==0);
    assert(!tsfp_event_query(h,&info) && !tsfp_thread_handles_busy());
    assert(tsfp_guest_thread_enter(41,0x8d000,0x1000,0x89000,164)); stack_top=0x8e000;
    assert(set(h)==TSFP_STATUS_INVALID_HANDLE);
}
void test_events(void) {
    assert(!tsfp_thread_handles_busy());
    const uint32_t markers[]={0x800000bd,0x800000e1,0x800000ba,0x800000e9,0x800000bb,0x800000ff};
    tsfp_guest_write(0x900e0,markers,sizeof(markers)); assert(tsfp_kernel_imports_prepare(0x900e0,6));
    for (unsigned i=0;i<3;++i) {
        uint32_t tib=0x89100+i*0x100,object=0x8a200+i*0x200;
        assert(tsfp_guest_tls_prepare_xbe(tib,0x8c080,20,5,64));
        assert(tsfp_thread_object_prepare_tls(object,0x8b000,tib,700+i,tib+64));
    }
    assert(tsfp_guest_thread_enter(41,0x8d000,0x1000,0x89000,164)); stack_top=0x8e000;
    tsfp_guest_store_u64(0x90108,0); g_esp=stack_top; PUSH32(g_esp,0);
    TsfpImageFunction fn=fixture_event_sequence; TsfpFault fault;
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_esp==stack_top);
    for (unsigned i=0;i<5;++i) assert(tsfp_guest_load_u32(0x90120+4*i)==0);
    assert(!tsfp_thread_handles_busy());
    for (unsigned type=0;type<2;++type) {
        uint32_t h=create(type,1); assert(poll(h)==0 && poll(h)==(type ? 0x102u : 0));
        assert(one(2,h)==0 && poll(h)==0x102);
        assert(set(h)==0 && tsfp_guest_load_u32(0x90104)==0);
        assert(set(h)==0 && tsfp_guest_load_u32(0x90104)==1);
        assert(poll(h)==0 && poll(h)==(type ? 0x102u : 0)); assert(one(4,h)==0);
        group(type);
    }
    uint32_t boolean=create(1,0x100); assert(poll(boolean)==0x102 && one(4,boolean)==0);
    boolean=create(1,0x101); assert(poll(boolean)==0 && poll(boolean)==0x102 && one(4,boolean)==0);
    /* A late set cannot be consumed by an expired, not-yet-scheduled waiter. */
    uint32_t h=create(1,0); reset_gate(); WaitContext timed={h,99,0,50000000}; pthread_t native;
    assert(!pthread_create(&native,NULL,waiter,&timed)); await_refs(h,1); spurious_wake(); await_gate(1);
    /* Control only this setter clock observation; actual finite waits are
     * tested separately. Avoid a scheduler-dependent 10 ms interleaving. */
    test_wait_advance_clock(10);
    assert(set(h)==0); TsfpEventInfo info; assert(tsfp_event_query(h,&info) && !info.granted && info.signaled);
    release_gate(); assert(!pthread_join(native,NULL) && timed.status==0x102);
    assert(poll(h)==0 && poll(h)==0x102 && one(4,h)==0);
    /* Clock failure before set is transactional and preserves previous output. */
    h=create(1,0); reset_gate(); timed=(WaitContext){h,99,0,50000000};
    assert(!pthread_create(&native,NULL,waiter,&timed)); await_refs(h,1); spurious_wake(); await_gate(1);
    tsfp_guest_store_u32(0x90104,0x12345678); test_wait_fail_clock();
    assert(set(h)==0xc0000001u && tsfp_guest_load_u32(0x90104)==0x12345678);
    assert(tsfp_event_query(h,&info) && !info.granted && !info.signaled);
    assert(set(h)==0 && one(2,h)==0 && one(4,h)==0); release_gate();
    assert(!pthread_join(native,NULL) && timed.status==0);
    /* A shared native condition must not transfer a different object's grant. */
    uint32_t pair[2]={create(1,0),create(1,0)}; WaitContext distinct[2]; pthread_t peers[2]; reset_gate();
    for (unsigned i=0;i<2;++i) { distinct[i]=(WaitContext){pair[i],99,i,0}; assert(!pthread_create(&peers[i],NULL,waiter,&distinct[i])); }
    await_refs(pair[0],1); await_refs(pair[1],1); assert(set(pair[0])==0); await_gate(2);
    assert(tsfp_event_query(pair[0],&info) && info.granted==1);
    assert(tsfp_event_query(pair[1],&info) && !info.granted);
    assert(set(pair[1])==0 && one(4,pair[0])==0 && one(4,pair[1])==0); release_gate();
    for (unsigned i=0;i<2;++i) assert(!pthread_join(peers[i],NULL) && distinct[i].status==0);
    /* Referenced close, real finite timeout and native error cleanup. */
    h=create(1,0); close_before_wait=h; tsfp_guest_store_u64(0x90108,UINT64_C(0)-10000);
    const uint32_t wait_args[]={h,0,0x90108}; assert(call(3,3,wait_args,&fault)==0x102 && !tsfp_event_query(h,&info));
    h=create(0,0); uint32_t args[]={h,0,0x90108}; tsfp_guest_store_u64(0x90108,UINT64_C(0)-50000000);
    test_wait_fail_condition(); assert(call(3,3,args,&fault)==0xc0000001u);
    fail_infinite=1; args[2]=0; assert(call(3,3,args,&fault)==0xc0000001u && !fail_infinite);
    assert(tsfp_event_query(h,&info) && !info.waiters && !info.signaled);
    assert(set(h)==0 && poll(h)==0 && one(4,h)==0);
    /* Thread handles are waitable, but never event objects. */
    tsfp_guest_store_u32(0x8a600+0x12c,801); uint32_t owner;
    assert(tsfp_thread_owner_create(0x8a600,801,&owner));
    assert(set(owner)==TSFP_STATUS_INVALID_HANDLE && one(2,owner)==TSFP_STATUS_INVALID_HANDLE && poll(owner)==0x102);
    h=create(1,1); TsfpThreadInfo thread;
    TsfpWaitTimeout zero={0,0}; assert(!tsfp_thread_owner_query(h,&thread) && !tsfp_thread_owner_start(h) && !tsfp_thread_owner_finish(h,0));
    assert(tsfp_thread_handle_wait(h,&zero)==TSFP_STATUS_INVALID_HANDLE && tsfp_thread_handle_close(h)==TSFP_STATUS_INVALID_HANDLE && poll(h)==0);
    assert(one(4,h)==0 && tsfp_thread_owner_finish(owner,37) && poll(owner)==0 && one(4,owner)==0 && tsfp_thread_owner_release(owner));
    /* Validate all guest outputs before mutating signal state or allocating. */
    h=create(0,0); uint32_t set_bad[]={h,0x93ffe};
    assert(call(1,2,set_bad,&fault)==UINT32_MAX && fault.kind==TSFP_FAULT_MEMORY && fault.access==TSFP_ACCESS_WRITE && fault.width==4);
    assert(tsfp_event_query(h,&info) && !info.signaled && one(4,h)==0);
    uint32_t create_bad[]={0x93ffe,0,0,0}; assert(call(0,4,create_bad,&fault)==UINT32_MAX && !tsfp_thread_handles_busy());
    create_bad[0]=0x90100; create_bad[2]=2; tsfp_guest_store_u32(0x90100,0x12345678);
    assert(call(0,4,create_bad,&fault)==0xc000000du && tsfp_guest_load_u32(0x90100)==0x12345678);
    create_bad[2]=0;
    /* Guest layout is 12 bytes; unaligned and mirrored descriptors are safe.
     * Snapshot before writing an output that aliases the input descriptor. */
    const uint32_t empty[3]={0};
    const uint32_t locations[]={0x90140,0x90141,0x04090141};
    for (unsigned i=0;i<3;++i) {
        tsfp_guest_write(locations[i],empty,sizeof(empty)); create_bad[1]=locations[i];
        assert(call(0,4,create_bad,&fault)==0); h=tsfp_guest_load_u32(0x90100);
        assert(poll(h)==0x102 && one(4,h)==0);
    }
    create_bad[0]=create_bad[1]=0x90140; tsfp_guest_write(0x90140,empty,sizeof(empty));
    assert(call(0,4,create_bad,&fault)==0); h=tsfp_guest_load_u32(0x90140); assert(one(4,h)==0);
    create_bad[0]=0x90100; create_bad[1]=0x90140;
    for (unsigned field=0;field<3;++field) {
        tsfp_guest_write(0x90140,empty,sizeof(empty)); tsfp_guest_store_u32(0x90140+field*4,1);
        tsfp_guest_store_u32(0x90100,0x12345678);
        assert(call(0,4,create_bad,&fault)==TSFP_STATUS_NOT_SUPPORTED && !tsfp_thread_handles_busy());
        assert(tsfp_guest_load_u32(0x90100)==0x12345678);
    }
    const uint32_t invalid_attrs[]={0xfd000000,0xfffffffc,0x93ff8,0x03fffffc};
    for (unsigned i=0;i<4;++i) {
        create_bad[1]=invalid_attrs[i];
        assert(call(0,4,create_bad,&fault)==UINT32_MAX && fault.kind==TSFP_FAULT_MEMORY &&
            fault.access==TSFP_ACCESS_READ && fault.address==invalid_attrs[i] && fault.width==12);
        assert(!tsfp_thread_handles_busy() && tsfp_guest_load_u32(0x90100)==0x12345678);
    }
    /* Shared quota, cross-kind reuse and stale generation rejection. */
    TsfpStorageConfig config={0x90400,0x1400,3,256*1024,resolve};
    assert(tsfp_storage_configure(&config)==TSFP_STORAGE_OK && tsfp_thread_service_start(0x1000,NULL));
    uint32_t all[TSFP_THREAD_HANDLES]; for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) all[i]=create(i&1,0);
    create_bad[1]=0; assert(call(0,4,create_bad,&fault)==0xc000009au);
    static unsigned char before[0x3c00],after[0x3c00]; tsfp_guest_read(0x90400,before,sizeof(before));
    const uint32_t thread_args[]={0x90100,0,0,0,0,11,22,0,0,0x13200};
    assert(call(5,10,thread_args,&fault)==TSFP_STATUS_INSUFFICIENT_RESOURCES);
    tsfp_guest_read(0x90400,after,sizeof(after));
    for (unsigned i=0;i<sizeof(before);++i) assert(before[i]==after[i]);
    assert(tsfp_thread_service_stop(NULL) && tsfp_storage_disable());
    uint32_t canary=0x12345678; assert(!tsfp_thread_owner_create(0x8a600,801,&canary) && canary==0x12345678);
    assert(one(4,all[0])==0 && tsfp_thread_owner_create(0x8a600,801,&owner) && owner!=all[0]);
    assert(set(all[0])==TSFP_STATUS_INVALID_HANDLE);
    for (unsigned i=1;i<TSFP_THREAD_HANDLES;++i) assert(one(4,all[i])==0);
    assert(tsfp_thread_owner_finish(owner,0) && one(4,owner)==0 && tsfp_thread_owner_release(owner));
    for (unsigned repeat=0;repeat<32;++repeat) { h=create(repeat&1,0); assert(set(h)==0 && poll(h)==0 && one(4,h)==0); }
    assert(tsfp_guest_thread_leave() && !tsfp_thread_handles_busy());
    const uint32_t restore[]={0x80000068,0x80000140,0x80000102,0x800000bb};
    tsfp_guest_write(0x900e0,restore,sizeof(restore)); assert(tsfp_kernel_imports_prepare(0x900e0,4));
    puts("event-v1: manual=pass auto=pass grants=pass close-refs=pass deadlines=pass types=pass quota=32 repeats=32 attributes=pass");
}
