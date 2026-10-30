/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "storage.h"
#include "handles.h"
#include "memory.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
static _Thread_local volatile int fail_calloc;
static atomic_int calloc_injection_armed;
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t size) {
    /* Static libc may allocate TLS itself: never read test TLS before main. */
    if (atomic_load(&calloc_injection_armed) && fail_calloc) { fail_calloc=0; return NULL; }
    return __real_calloc(n,size);
}
void test_fail_native_create(void); void test_fail_native_join(void);
void fixture_thread_args(void); void fixture_tls_worker(void); void fixture_image_entry(void);
static TsfpImageFunction resolve(uint32_t address) {
    return address==0x13200 ? fixture_thread_args : address==0x13600 ? fixture_tls_worker : address==0x13400 ? fixture_image_entry : NULL;
}
static int fail_allocate; static unsigned allocations,releases;
static void *allocate(size_t size) {
    if (fail_allocate) { fail_allocate=0; return NULL; }
    void *p=malloc(size); if (p) ++allocations; return p;
}
static void release(void *p) { assert(p); ++releases; free(p); }
#define BASE 0x90400u
#define SIZE 0x3c00u
static unsigned char before[SIZE],after[SIZE];
static void snapshot(unsigned char *p) { tsfp_guest_read(BASE,p,SIZE); }
static void unchanged(void) { snapshot(after); assert(!memcmp(before,after,SIZE)); }
static void *teardown(void *unused) {
    (void)unused;
    for (unsigned i=0;i<128;++i) {
        assert(!tsfp_image_reset());
        if (!(i&7u)) { struct timespec pause={0,100000}; assert(!nanosleep(&pause,NULL)); }
    }
    return NULL;
}
typedef struct Reserver { TsfpStorageRequest request; uint32_t lease; enum TsfpStorageError error; } Reserver;
static void *reserve_same_id(void *context) {
    Reserver *r=context; r->error=tsfp_storage_reserve(&r->request,&r->lease); return NULL;
}
void test_storage(void) {
    TsfpStorageConfig config={BASE,0x1400,3,256*1024,resolve},bad;
    TsfpStorageRequest request={201,0x1000,0x13200,11,22};
    uint32_t lease=0x12345678; TsfpStorageInfo info;
    assert(tsfp_storage_set_allocator(allocate,release));
    const uint32_t bad_bases[]={0x8d000,0x0408d000,0x8c000,0x13200,0x73ffffc0,0x80090400};
    for (unsigned i=0;i<sizeof(bad_bases)/sizeof(bad_bases[0]);++i) {
        bad=config; bad.base=bad_bases[i]; snapshot(before);
        assert(tsfp_storage_configure(&bad)==TSFP_STORAGE_INVALID && !tsfp_storage_configured()); unchanged();
    }
    bad=config; bad.slots=33; assert(tsfp_storage_configure(&bad)==TSFP_STORAGE_INVALID);
    bad=config; bad.stride=UINT32_MAX; assert(tsfp_storage_configure(&bad)==TSFP_STORAGE_INVALID);
    assert(tsfp_storage_configure(&config)==TSFP_STORAGE_OK && !tsfp_image_reset());
    bad=config; bad.base+=0x04000000; assert(tsfp_storage_configure(&bad)==TSFP_STORAGE_BUSY);
    snapshot(before); fail_allocate=1;
    assert(tsfp_storage_reserve(&request,&lease)==TSFP_STORAGE_ALLOCATION && lease==0x12345678); unchanged();
    fail_calloc=1; atomic_store(&calloc_injection_armed,1);
    assert(tsfp_storage_reserve(&request,&lease)==TSFP_STORAGE_INITIALIZATION && !fail_calloc && lease==0x12345678);
    atomic_store(&calloc_injection_armed,0); unchanged();
    TsfpStorageRequest malformed=request;
    malformed.stack_size=0xfffffff0u; assert(tsfp_storage_reserve(&malformed,&lease)==TSFP_STORAGE_INVALID); unchanged();
    malformed=request; malformed.entry=0x13340; assert(tsfp_storage_reserve(&malformed,&lease)==TSFP_STORAGE_INVALID); unchanged();
    malformed=request; malformed.id=41; assert(tsfp_storage_reserve(&malformed,&lease)==TSFP_STORAGE_INVALID); unchanged();
    Reserver concurrent[2]={{request,0x12345678,TSFP_STORAGE_INVALID},{request,0x12345678,TSFP_STORAGE_INVALID}};
    pthread_t contenders[2];
    for (unsigned i=0;i<2;++i) assert(!pthread_create(&contenders[i],NULL,reserve_same_id,&concurrent[i]));
    for (unsigned i=0;i<2;++i) assert(!pthread_join(contenders[i],NULL));
    unsigned winner=concurrent[0].error==TSFP_STORAGE_OK ? 0 : 1,loser=1-winner;
    assert(concurrent[winner].error==TSFP_STORAGE_OK && concurrent[loser].error==TSFP_STORAGE_BUSY && concurrent[loser].lease==0x12345678);
    assert(tsfp_storage_cancel(concurrent[winner].lease)); unchanged();
    assert(tsfp_storage_reserve(&request,&lease)==TSFP_STORAGE_OK);
    assert(tsfp_storage_query(lease,&info) && info.state==TSFP_STORAGE_PREPARED && !info.owner);
    assert(!(info.spec.tib&63u) && !(info.spec.object&63u) && info.spec.stack==BASE);
    assert(tsfp_guest_load_u32(info.spec.tib+4)==info.spec.tib+100);
    assert(tsfp_guest_load_u32(info.spec.tib+0x28)==info.spec.object && tsfp_guest_load_u32(info.spec.object+0x12c)==201);
    assert(tsfp_guest_load_u32(info.spec.object+0x28)==info.spec.tib+64);
    for (unsigned i=20;i<36;++i) assert(tsfp_guest_load_u8(info.spec.tib+64+i)==0);
    assert(!tsfp_storage_disable() && !tsfp_storage_set_allocator(NULL,NULL));
    assert(tsfp_storage_cancel(lease) && !tsfp_storage_cancel(lease)); unchanged();
    uint32_t stale=lease;
    assert(tsfp_storage_reserve(&request,&lease)==TSFP_STORAGE_OK && lease!=stale);
    assert(!tsfp_storage_query(stale,&info));
    test_fail_native_create(); int platform=0;
    assert(tsfp_storage_launch(lease,&platform)==TSFP_STORAGE_LAUNCH_FAILED && platform==EAGAIN);
    assert(!tsfp_storage_query(lease,&info) && !tsfp_thread_handles_busy()); unchanged();
    uint32_t leases[3]; TsfpStorageInfo active[3];
    for (unsigned i=0;i<3;++i) {
        request.id=201+i; request.entry=i==0 ? 0x13200 : i==1 ? 0x13600 : 0x13400;
        assert(tsfp_storage_reserve(&request,&leases[i])==TSFP_STORAGE_OK && tsfp_storage_query(leases[i],&active[i]));
    }
    snapshot(before); request.id=204;
    uint32_t canary=0x11223344;
    assert(tsfp_storage_reserve(&request,&canary)==TSFP_STORAGE_EXHAUSTED && canary==0x11223344); unchanged();
    assert(!tsfp_thread_owner_create(active[0].spec.object,201,&canary) && canary==0x11223344);
    TsfpWorker raw={0}; TsfpWorkerSpec raw_spec=active[0].spec; raw_spec.storage_lease=0; raw_spec.stack+=0x04000000;
    assert(tsfp_worker_launch(&raw,&raw_spec)==TSFP_WORKER_BUSY && !raw.state); unchanged();
    for (unsigned i=0;i<3;++i) {
        assert(tsfp_storage_launch(leases[i],NULL)==TSFP_STORAGE_OK && tsfp_storage_query(leases[i],&active[i]));
        assert(!tsfp_storage_cancel(leases[i]) && !tsfp_storage_release(leases[i]));
    }
    TsfpWaitTimeout infinite={0,1};
    for (unsigned i=0;i<3;++i) {
        assert(tsfp_thread_handle_wait(active[i].owner,&infinite)==0 && tsfp_thread_handle_close(active[i].owner)==0);
        assert(!tsfp_thread_owner_release(active[i].owner));
    }
    pthread_t observer; assert(!pthread_create(&observer,NULL,teardown,NULL));
    TsfpWorkerResult result;
    test_fail_native_join(); assert(tsfp_storage_join(leases[0],&result)==TSFP_WORKER_PLATFORM);
    assert(tsfp_storage_query(leases[0],&info) && info.platform_error==EINVAL && !tsfp_storage_release(leases[0]));
    for (unsigned i=0;i<3;++i) {
        assert(tsfp_storage_join(leases[i],&result)==TSFP_WORKER_OK);
        if (i==0) assert(!result.event && result.cpu.registers[0]==11 && result.cpu.registers[2]==22);
        if (i==1) assert(!result.event && result.cpu.registers[0]==99 && result.cpu.fs_base==active[i].spec.tib);
        if (i==2) assert(result.event==1 && result.status==37 && result.fault.thread_id==203);
        assert(!tsfp_thread_owner_release(active[i].owner));
        assert(tsfp_storage_release(leases[i]) && !tsfp_storage_query(leases[i],&info));
    }
    assert(!pthread_join(observer,NULL));
    request=(TsfpStorageRequest){201,0x1000,0x13200,11,22};
    for (unsigned repeat=0;repeat<16;++repeat) {
        assert(tsfp_storage_reserve(&request,&lease)==TSFP_STORAGE_OK && tsfp_storage_launch(lease,NULL)==TSFP_STORAGE_OK);
        assert(tsfp_storage_query(lease,&info) && tsfp_thread_handle_close(info.owner)==0);
        assert(tsfp_storage_join(lease,&result)==TSFP_WORKER_OK && !result.event && tsfp_storage_release(lease));
    }
    assert(tsfp_storage_disable() && !tsfp_storage_configured() && !tsfp_thread_handles_busy());
    config.base+=0x04000000;
    assert(tsfp_storage_configure(&config)==TSFP_STORAGE_OK && !tsfp_storage_query(lease,&info));
    assert(tsfp_storage_reserve(&request,&lease)==TSFP_STORAGE_OK && tsfp_storage_query(lease,&info));
    assert(info.spec.stack==BASE+0x04000000 && tsfp_storage_launch(lease,NULL)==TSFP_STORAGE_OK);
    assert(tsfp_storage_query(lease,&info) && tsfp_thread_handle_close(info.owner)==0);
    assert(tsfp_storage_join(lease,&result)==TSFP_WORKER_OK && result.cpu.fs_base==info.spec.tib);
    assert(tsfp_storage_release(lease) && tsfp_storage_disable());
    assert(allocations==releases && tsfp_storage_set_allocator(NULL,NULL));
    puts("storage-v1: slots=3 exhaustion=pass alias=pass rollback=pass workers=3 stale=pass teardown=pass repeats=16");
}
