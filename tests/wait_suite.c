/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "handles.h"
#include "memory.h"
#include "image.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
/* Link-time fault injection only in the test executable, not runtime hooks. */
static _Thread_local int fail_clock,fail_condition;
/* Debian ARM uses redirected 64-bit time entry points. Match the actual ABI,
 * including timespec layout, rather than wrapping the legacy time32 symbol. */
#if defined(__GLIBC__) && __TIMESIZE == 32 && defined(__USE_TIME_BITS64)
#define __real_clock_gettime __real___clock_gettime64
#define __wrap_clock_gettime __wrap___clock_gettime64
#define __real_pthread_cond_timedwait __real___pthread_cond_timedwait64
#define __wrap_pthread_cond_timedwait __wrap___pthread_cond_timedwait64
#endif
int __real_clock_gettime(clockid_t clock,struct timespec *out);
int __wrap_clock_gettime(clockid_t clock,struct timespec *out) {
    if (fail_clock) { fail_clock=0; errno=EIO; return -1; }
    return __real_clock_gettime(clock,out);
}
int __real_pthread_cond_timedwait(pthread_cond_t *,pthread_mutex_t *,const struct timespec *);
int __wrap_pthread_cond_timedwait(pthread_cond_t *c,pthread_mutex_t *m,const struct timespec *t) {
    if (fail_condition) { fail_condition=0; return EINVAL; }
    return __real_pthread_cond_timedwait(c,m,t);
}
typedef struct Waiter { uint32_t owner,result; TsfpWaitTimeout timeout; } Waiter;
static void *waiter(void *arg) {
    Waiter *w=arg; w->result=tsfp_thread_handle_wait(w->owner,&w->timeout); pthread_testcancel(); return NULL;
}
static uint64_t now_ns(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}
static void await_references(uint32_t owner,unsigned count) {
    uint64_t deadline=now_ns()+UINT64_C(5000000000);
    for (;;) {
        TsfpThreadInfo info; assert(tsfp_thread_owner_query(owner,&info));
        if (info.waiters==count) return;
        assert(now_ns()<deadline);
        struct timespec pause={0,100000}; assert(!nanosleep(&pause,NULL));
    }
}
static uint32_t create_owner(void) {
    uint32_t owner;
    tsfp_guest_store_u32(0x8b200+0x12c,71);
    assert(tsfp_thread_owner_create(0x8b200,71,&owner));
    assert(tsfp_thread_owner_start(owner)); return owner;
}
static void retire(uint32_t owner) {
    assert(tsfp_thread_handle_close(owner)==0 && tsfp_thread_owner_finish(owner,19));
    assert(tsfp_thread_owner_release(owner));
}
void test_waits(void) {
    TsfpWaitTimeout timeout={42,1},before=timeout;
    assert(!tsfp_thread_timeout_decode(1,1,&timeout) && timeout.ticks==before.ticks && timeout.infinite==before.infinite);
    assert(tsfp_thread_timeout_decode(0,1,&timeout) && timeout.infinite);
    assert(tsfp_thread_timeout_decode(1,UINT64_C(0x8000000000000000),&timeout) && !timeout.infinite && timeout.ticks==UINT64_C(0x8000000000000000));
    assert(tsfp_thread_timeout_decode(1,UINT64_MAX,&timeout) && timeout.ticks==1);
    TsfpWaitTimeout poll={0,0},finite={10000,0},infinite={0,1};
    uint32_t owner=create_owner();
    assert(tsfp_thread_handle_wait(owner,&poll)==0x102);
    uint64_t start=now_ns();
    assert(tsfp_thread_handle_wait(owner,&finite)==0x102 && now_ns()-start>=1000000);
    TsfpThreadInfo info; assert(tsfp_thread_owner_query(owner,&info) && !info.waiters);
    fail_clock=1; uint32_t clock_result=tsfp_thread_handle_wait(owner,&finite);
    if (clock_result!=0xc0000001u || fail_clock) fprintf(stderr,"wait clock injection: result=%08x pending=%d\n",clock_result,fail_clock);
    assert(clock_result==0xc0000001u && !fail_clock);
    TsfpWaitTimeout long_finite={50000000,0};
    fail_condition=1; assert(tsfp_thread_handle_wait(owner,&long_finite)==0xc0000001u && !fail_condition);
    assert(tsfp_thread_owner_query(owner,&info) && !info.waiters);
    retire(owner);
    assert(tsfp_thread_handle_wait(owner,&infinite)==TSFP_STATUS_INVALID_HANDLE);
    /* Deterministic acquisition-before-close schedule; completion wakes all. */
    for (unsigned repeat=0;repeat<32;++repeat) {
        owner=create_owner(); Waiter w[4]; pthread_t threads[4];
        for (unsigned i=0;i<4;++i) {
            w[i]=(Waiter){owner,99,i&1 ? (TsfpWaitTimeout){50000000,0} : infinite};
            assert(!pthread_create(&threads[i],NULL,waiter,&w[i]));
        }
        await_references(owner,4);
        assert(tsfp_thread_handle_close(owner)==0 && !tsfp_thread_owner_release(owner));
        assert(!tsfp_image_reset());
        assert(tsfp_thread_handle_wait(owner,&poll)==TSFP_STATUS_INVALID_HANDLE);
        assert(tsfp_thread_owner_finish(owner,23));
        for (unsigned i=0;i<4;++i) { assert(!pthread_join(threads[i],NULL)); assert(w[i].result==0); }
        assert(tsfp_thread_owner_query(owner,&info) && !info.waiters && info.status==23);
        assert(tsfp_thread_owner_release(owner));
        assert(tsfp_thread_handle_wait(owner,&poll)==TSFP_STATUS_INVALID_HANDLE);
    }
    owner=create_owner();
    Waiter cancelled={owner,99,infinite}; pthread_t native;
    assert(!pthread_create(&native,NULL,waiter,&cancelled)); await_references(owner,1);
    assert(!pthread_cancel(native));
    assert(tsfp_thread_handle_close(owner)==0 && tsfp_thread_owner_finish(owner,31));
    void *exit_value; assert(!pthread_join(native,&exit_value) && exit_value==PTHREAD_CANCELED);
    assert(tsfp_thread_owner_query(owner,&info) && !info.waiters && tsfp_thread_owner_release(owner));
    /* Deliberately race a finite deadline with completion; the serialized
     * observer may report either, but completion remains sticky afterward. */
    for (unsigned repeat=0;repeat<16;++repeat) {
        owner=create_owner(); Waiter race={owner,99,finite}; pthread_t thread;
        assert(!pthread_create(&thread,NULL,waiter,&race));
        struct timespec pause={0,1000000}; assert(!nanosleep(&pause,NULL));
        assert(tsfp_thread_owner_finish(owner,27));
        assert(!pthread_join(thread,NULL) && (race.result==0 || race.result==0x102));
        assert(tsfp_thread_handle_wait(owner,&poll)==0);
        assert(tsfp_thread_handle_close(owner)==0 && tsfp_thread_owner_release(owner));
    }
    owner=create_owner(); assert(tsfp_thread_owner_finish(owner,29));
    assert(tsfp_thread_handle_wait(owner,&infinite)==0 && tsfp_thread_handle_wait(owner,&finite)==0);
    assert(tsfp_thread_timeout_decode(1,UINT64_C(0x8000000000000000),&timeout));
    assert(tsfp_thread_handle_wait(owner,&timeout)==0);
    assert(tsfp_thread_handle_close(owner)==0 && tsfp_thread_owner_release(owner));
    assert(!tsfp_thread_handles_busy() && tsfp_image_reset());
    puts("thread-wait-v1: ticks=pass timeout=pass close-reference=pass waiters=4 repeats=32 failures=pass teardown=pass");
}
