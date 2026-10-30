/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "worker.h"
#include "handles.h"
#include "memory.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
static _Thread_local int fail_create,fail_join;
static atomic_int fail_any_create,gate_next;
static struct {
    pthread_mutex_t mutex; pthread_cond_t changed;
    int started,released,finished; void *(*entry)(void *); void *context;
} gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER};
void test_fail_any_native_create(void) { atomic_store(&fail_any_create,1); }
void test_gate_next_native_create(void) {
    assert(!pthread_mutex_lock(&gate.mutex));
    gate.started=gate.released=gate.finished=0; atomic_store(&gate_next,1);
    assert(!pthread_mutex_unlock(&gate.mutex));
}
static void *gated_entry(void *unused) {
    (void)unused; assert(!pthread_mutex_lock(&gate.mutex));
    gate.started=1; assert(!pthread_cond_broadcast(&gate.changed));
    while (!gate.released) assert(!pthread_cond_wait(&gate.changed,&gate.mutex));
    void *(*entry)(void *)=gate.entry; void *context=gate.context;
    assert(!pthread_mutex_unlock(&gate.mutex));
    void *result=entry(context);
    assert(!pthread_mutex_lock(&gate.mutex)); gate.finished=1;
    assert(!pthread_cond_broadcast(&gate.changed) && !pthread_mutex_unlock(&gate.mutex)); return result;
}
void test_gate_native_wait(int finished) {
    struct timespec deadline; assert(!clock_gettime(CLOCK_REALTIME,&deadline)); deadline.tv_sec+=5;
    assert(!pthread_mutex_lock(&gate.mutex));
    while (!(finished ? gate.finished : gate.started)) assert(!pthread_cond_timedwait(&gate.changed,&gate.mutex,&deadline));
    assert(!pthread_mutex_unlock(&gate.mutex));
}
void test_gate_native_release(void) {
    assert(!pthread_mutex_lock(&gate.mutex)); gate.released=1;
    assert(!pthread_cond_broadcast(&gate.changed) && !pthread_mutex_unlock(&gate.mutex));
}
void test_fail_native_create(void) { fail_create=1; }
void test_fail_native_join(void) { fail_join=1; }
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*entry)(void *),void *context) {
    if (fail_create || atomic_exchange(&fail_any_create,0)) { fail_create=0; return EAGAIN; }
    if (atomic_exchange(&gate_next,0)) {
        assert(!pthread_mutex_lock(&gate.mutex)); gate.entry=entry; gate.context=context;
        assert(!pthread_mutex_unlock(&gate.mutex));
        return __real_pthread_create(t,a,gated_entry,NULL);
    }
    return __real_pthread_create(t,a,entry,context);
}
int __real_pthread_join(pthread_t,void **);
int __wrap_pthread_join(pthread_t thread,void **out) {
    if (fail_join) { fail_join=0; return EINVAL; }
    return __real_pthread_join(thread,out);
}
void fixture_fault_divide(void); void fixture_thread_args(void); void fixture_image_entry(void); void fixture_tls_worker(void);
static TsfpImageFunction resolve(uint32_t address) {
    return address==0x132a0 ? fixture_fault_divide : address==0x13200 ? fixture_thread_args : address==0x13400 ? fixture_image_entry : address==0x13600 ? fixture_tls_worker : NULL;
}
static TsfpWorkerSpec spec(unsigned slot,uint32_t entry) {
    TsfpWorkerSpec s={0}; s.object=0x8a200+slot*0x200; s.id=101+slot;
    s.stack=0x91000+slot*0x1000; s.tib=0x89100+slot*0x100; s.entry=entry;
    s.context1=11; s.context2=22; s.stack_size=0x1000; s.tls_size=164;
    s.native_stack_size=256*1024; s.resolve=resolve; return s;
}
typedef struct Joiner { TsfpWorker *worker; TsfpWorkerResult output; enum TsfpWorkerError error; } Joiner;
static void *joiner(void *context) {
    Joiner *j=context; j->error=tsfp_worker_join(j->worker,&j->output); return NULL;
}
static void forbidden_join(void *context) {
    TsfpWorker *w=context; TsfpWorkerResult result;
    assert(tsfp_worker_join(w,&result)==TSFP_WORKER_INVALID && !tsfp_worker_release(w));
}
static unsigned char before[8192],after[8192];
static void snapshot(const TsfpWorkerSpec *s,unsigned char *bytes) {
    assert(s->stack_size+s->tls_size+512<=sizeof(before));
    tsfp_guest_read(s->stack,bytes,s->stack_size);
    tsfp_guest_read(s->tib,bytes+s->stack_size,s->tls_size);
    tsfp_guest_read(s->object,bytes+s->stack_size+s->tls_size,512);
}
void test_worker_adapter(void) {
    TsfpWorker a={0},b={0},conflict={0}; TsfpWorkerResult ar,br;
    TsfpWorkerSpec sa=spec(0,0x13200),sb=spec(1,0x13400),bad=sa;
    bad.entry=0x13340; assert(tsfp_worker_launch(&a,&bad)==TSFP_WORKER_INVALID && !a.state);
    bad=sa; bad.native_stack_size=1; assert(tsfp_worker_launch(&a,&bad)==TSFP_WORKER_INVALID);
    bad=sa; bad.stack_size=SIZE_MAX-3; assert(tsfp_worker_launch(&a,&bad)==TSFP_WORKER_INVALID);
    bad=sa; bad.stack=0xfd000000; assert(tsfp_worker_launch(&a,&bad)==TSFP_WORKER_INVALID);
    bad=sa; ++bad.id; assert(tsfp_worker_launch(&a,&bad)==TSFP_WORKER_INVALID);
    uint32_t quota[TSFP_THREAD_HANDLES];
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i) {
        uint32_t object=0x90000+i*512,id=1000+i;
        tsfp_guest_store_u32(object+0x12c,id); assert(tsfp_thread_owner_create(object,id,&quota[i]));
    }
    assert(tsfp_worker_launch(&a,&sa)==TSFP_WORKER_RESOURCE && !a.state);
    for (unsigned i=0;i<TSFP_THREAD_HANDLES;++i)
        assert(tsfp_thread_handle_close(quota[i])==0 && tsfp_thread_owner_finish(quota[i],0) && tsfp_thread_owner_release(quota[i]));
    snapshot(&sa,before); fail_create=1;
    assert(tsfp_worker_launch(&a,&sa)==TSFP_WORKER_PLATFORM && !fail_create && a.platform_error==EAGAIN && !a.state && !tsfp_thread_handles_busy());
    snapshot(&sa,after); assert(!memcmp(before,after,sa.stack_size+sa.tls_size+512));
    assert(tsfp_worker_launch(&a,&sa)==TSFP_WORKER_OK);
    assert(tsfp_worker_launch(&conflict,&sa)==TSFP_WORKER_BUSY && !conflict.state);
    bad=sa; bad.stack+=0x04000000;
    assert(tsfp_worker_launch(&conflict,&bad)==TSFP_WORKER_BUSY && !conflict.state);
    TsfpFault forbidden; assert(tsfp_guest_run(forbidden_join,&a,&forbidden)==0);
    assert(tsfp_worker_launch(&b,&sb)==TSFP_WORKER_OK);
    TsfpWaitTimeout infinite={0,1};
    assert(tsfp_thread_handle_wait(a.owner,&infinite)==0 && tsfp_thread_handle_wait(b.owner,&infinite)==0);
    uint32_t stale=a.owner;
    assert(tsfp_thread_handle_close(a.owner)==0 && tsfp_thread_handle_close(b.owner)==0);
    assert(!tsfp_thread_owner_release(a.owner) && !tsfp_thread_owner_release(b.owner));
    assert(!tsfp_worker_release(&a) && !tsfp_image_reset());
    fail_join=1; assert(tsfp_worker_join(&a,&ar)==TSFP_WORKER_PLATFORM && !fail_join && a.platform_error==EINVAL);
    assert(!tsfp_thread_owner_release(a.owner) && !tsfp_image_reset());
    assert(tsfp_worker_join(&a,&ar)==TSFP_WORKER_OK && tsfp_worker_join(&a,&ar)==TSFP_WORKER_BUSY);
    assert(a.platform_error==0);
    assert(ar.event==0 && ar.status==0 && ar.cpu.registers[0]==11 && ar.cpu.registers[2]==22);
    assert(ar.cpu.registers[4]==sa.stack+sa.stack_size && ar.cpu.fs_base==sa.tib);
    assert(tsfp_worker_join(&b,&br)==TSFP_WORKER_OK && br.event==1 && br.status==37);
    assert(br.fault.kind==TSFP_THREAD_EXIT && br.fault.thread_id==102 && br.cpu.fs_base==sb.tib);
    assert(tsfp_worker_release(&a) && tsfp_worker_release(&b) && !tsfp_thread_handles_busy());
    assert(tsfp_thread_handle_close(stale)==TSFP_STATUS_INVALID_HANDLE);
    /* Normal return and close-before-join repeatedly reclaim the backend stack. */
    for (unsigned repeat=0;repeat<16;++repeat) {
        assert(tsfp_worker_launch(&a,&sa)==TSFP_WORKER_OK);
        assert(tsfp_thread_handle_close(a.owner)==0 && !tsfp_worker_release(&a));
        assert(tsfp_worker_join(&a,&ar)==TSFP_WORKER_OK && ar.status==0 && !ar.event);
        assert(tsfp_worker_release(&a));
    }
    assert(tsfp_worker_launch(&a,&sa)==TSFP_WORKER_OK);
    assert(tsfp_thread_handle_close(a.owner)==0);
    Joiner j[2]={{.worker=&a},{.worker=&a}}; pthread_t controllers[2];
    for (unsigned i=0;i<2;++i) assert(!pthread_create(&controllers[i],NULL,joiner,&j[i]));
    for (unsigned i=0;i<2;++i) assert(!pthread_join(controllers[i],NULL));
    assert((j[0].error==TSFP_WORKER_OK && j[1].error==TSFP_WORKER_BUSY) ||
           (j[1].error==TSFP_WORKER_OK && j[0].error==TSFP_WORKER_BUSY));
    assert(tsfp_worker_release(&a) && !tsfp_worker_release(&a));
    /* Guest failure remains observable; it does not become successful exit. */
    sa.entry=0x132a0;
    assert(tsfp_worker_launch(&a,&sa)==TSFP_WORKER_OK);
    assert(tsfp_worker_join(&a,&ar)==TSFP_WORKER_OK && ar.event==1 && ar.status==0xc0000001u);
    assert(ar.fault.kind==TSFP_FAULT_DIVIDE && ar.fault.thread_id==101);
    assert(tsfp_thread_handle_close(a.owner)==0 && tsfp_worker_release(&a));
    assert(!tsfp_thread_handles_busy());
    puts("worker-adapter-v1: args=pass exit=37 tls=isolated native-pin=pass rollback=pass join-retry=pass repeats=16 fault=pass");
}
