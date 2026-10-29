/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "thread_service.h"
#include "storage.h"
#include "handles.h"
#include "kernel.h"
#include "memory.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
void fixture_service_parent(void); void fixture_service_child(void); void fixture_service_call(void); void fixture_thread_args(void);
void test_fail_native_create(void); void test_fail_any_native_create(void); void test_fail_native_join(void);
void test_gate_next_native_create(void); void test_gate_native_wait(int); void test_gate_native_release(void);
static TsfpImageFunction resolve(uint32_t address) {
    return address==0x139a0 ? fixture_service_child : address==0x13200 ? fixture_thread_args : NULL;
}
static void invoke(void *context) { (*(TsfpImageFunction *)context)(); }
static _Thread_local unsigned cleaned;
static void count_cleanup(void *context) { ++*(unsigned *)context; }
static void guarded_fault(void *unused) {
    (void)unused; assert(tsfp_guest_scope_guard(count_cleanup,&cleaned));
    tsfp_guest_raise(TSFP_FAULT_DIVIDE,0x132a0,0,0);
}
static void guarded_return(void *unused) {
    (void)unused; assert(tsfp_guest_scope_guard(count_cleanup,&cleaned));
    assert(!tsfp_guest_scope_guard(count_cleanup,&cleaned));
}
static void guarded_unregister(void *unused) {
    (void)unused; assert(tsfp_guest_scope_guard(count_cleanup,&cleaned));
    assert(tsfp_guest_scope_unguard(count_cleanup,&cleaned));
}
static void inner_fault(void *unused) {
    (void)unused; assert(!tsfp_guest_scope_guard(count_cleanup,&cleaned));
    tsfp_guest_raise(TSFP_FAULT_DIVIDE,0x132a0,0,0);
}
static void nested_guard(void *unused) {
    (void)unused; unsigned before=cleaned; TsfpFault inner;
    assert(tsfp_guest_scope_guard(count_cleanup,&cleaned));
    assert(tsfp_guest_run(inner_fault,NULL,&inner)==1 && cleaned==before);
}
static void cancellation_restored(void) {
    int previous; assert(!pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&previous));
    assert(previous==PTHREAD_CANCEL_ENABLE && !pthread_setcancelstate(previous,NULL));
}
static uint32_t frame(uint32_t out,uint32_t tid,uint32_t entry) {
    uint32_t words[]={out,0,0,0,tid,11,22,0,0,entry,0};
    g_esi=0x900a0; g_esp=0x8dfc0; g_eax=77;
    tsfp_guest_write(g_esp,words,sizeof(words)); return g_esp;
}
static void canaries(void) {
    tsfp_guest_store_u32(0x90084,0x12345678); tsfp_guest_store_u32(0x90088,0xabcdef01);
}
static void preserved(void) {
    assert(tsfp_guest_load_u32(0x90084)==0x12345678 && tsfp_guest_load_u32(0x90088)==0xabcdef01);
}
static unsigned char before[0x3c00],after[0x3c00];
typedef struct Parent { uint32_t id,tib,stack,object,out,owner,child; int leave_open; } Parent;
static void *parent(void *context) {
    Parent *p=context;
    assert(tsfp_guest_thread_enter(p->id,p->stack,0x800,p->tib,164));
    uint32_t words[]={p->out,0,0,0,p->out+4,p->id,2,0,0,0x13200,0};
    g_esi=0x900a0; g_esp=p->stack+0x800-64;
    tsfp_guest_write(g_esp,words,sizeof(words));
    TsfpImageFunction fn=fixture_service_call; TsfpFault fault;
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==0 && g_esp==p->stack+0x800-20);
    TsfpCpuState cpu; tsfp_cpu_capture(&cpu);
    assert(cpu.flags==2 && cpu.fs_base==p->tib);
    p->child=tsfp_guest_load_u32(p->out);
    uint32_t id=tsfp_guest_load_u32(p->out+4); assert(id && id!=p->id);
    TsfpWaitTimeout infinite={0,1}; assert(tsfp_thread_handle_wait(p->child,&infinite)==0);
    if (!p->leave_open) assert(tsfp_thread_handle_close(p->child)==0);
    assert(tsfp_guest_thread_leave()); return NULL;
}
void test_thread_service(void) {
    TsfpStorageConfig config={0x90400,0x1400,3,256*1024,resolve};
    assert(tsfp_storage_configure(&config)==TSFP_STORAGE_OK);
    int native_error=0; test_fail_native_create();
    assert(!tsfp_thread_service_start(0x1000,&native_error) && native_error==EAGAIN);
    assert(tsfp_storage_disable() && tsfp_storage_configure(&config)==TSFP_STORAGE_OK);
    assert(!tsfp_thread_service_start(0xfffffff0u,NULL));
    assert(tsfp_thread_service_start(0x1000,&native_error) && !native_error && !tsfp_storage_disable());
    tsfp_guest_store_u32(0x900a0,0x800000ff); tsfp_guest_store_u32(0x900a4,0x800000e9); tsfp_guest_store_u32(0x900a8,0x800000bb);
    assert(tsfp_kernel_imports_prepare(0x900a0,3));
    uint32_t owner; assert(tsfp_thread_owner_create(0x8a000,41,&owner) && tsfp_thread_owner_start(owner));
    assert(tsfp_guest_thread_enter(41,0x8d800,0x800,0x89000,164));
    TsfpImageFunction fn=fixture_service_parent; TsfpFault fault;
    g_esp=0x8e000; PUSH32(g_esp,0);
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_esp==0x8e000 && g_eax==0);
    assert(tsfp_guest_load_u32(0x9008c)==0 && tsfp_guest_load_u32(0x90094)==0 && tsfp_guest_load_u32(0x90098)==0);
    assert(tsfp_guest_load_u32(0x900b0)==11 && tsfp_guest_load_u32(0x900b4)==22);
    assert(tsfp_guest_load_u32(0x900b8)==tsfp_guest_load_u32(0x90088) && g_fs_base==0x89000);
    assert(tsfp_thread_handle_poll(tsfp_guest_load_u32(0x90084))==TSFP_STATUS_INVALID_HANDLE);
    fn=fixture_service_call;
    /* Full frame before service acquisition, then guarded output-probe faults. */
    g_esi=0x900a0; g_esp=0x8dffc; g_eax=77;
    assert(tsfp_guest_run(invoke,&fn,&fault)==1 && fault.kind==TSFP_FAULT_MEMORY);
    assert(fault.address==0x8dff8 && fault.width==44 && fault.access==TSFP_ACCESS_READ && g_eax==77);
    const uint32_t invalid[]={0,0xfd000000,0x73fffffe};
    for (unsigned i=0;i<3;++i) {
        canaries(); uint32_t stack=frame(0x90084,invalid[i] ? invalid[i] : 0x13200,0x139a0);
        assert(tsfp_guest_run(invoke,&fn,&fault)==1 && fault.kind==TSFP_FAULT_MEMORY && fault.access==TSFP_ACCESS_WRITE);
        assert(fault.width==4 && g_eax==77 && g_esp==stack-4); preserved();
    }
    cancellation_restored();
    canaries(); frame(0,0x90088,0x139a0);
    assert(tsfp_guest_run(invoke,&fn,&fault)==1 && fault.address==0); preserved();
    canaries(); frame(0xfd000000,0x90088,0x139a0);
    assert(tsfp_guest_run(invoke,&fn,&fault)==1 && fault.address==0xfd000000); preserved();
    for (unsigned variant=0;variant<7;++variant) {
        canaries(); uint32_t stack=frame(0x90084,0x90088,variant==0 ? 0xfd000000 : 0x139a0);
        uint32_t expected=TSFP_STATUS_NOT_SUPPORTED;
        if (variant==0) expected=TSFP_STATUS_INVALID_PARAMETER;
        if (variant==1) tsfp_guest_store_u32(stack+4,4); /* ThreadExtraSize */
        if (variant==2) tsfp_guest_store_u32(stack+28,1); /* suspended */
        if (variant==3) tsfp_guest_store_u32(stack+32,1); /* debug */
        if (variant==4) tsfp_guest_store_u32(stack+12,7); /* TLS size */
        if (variant==5) { tsfp_guest_store_u32(stack+8,0xfffffff0u); expected=TSFP_STATUS_INVALID_PARAMETER; }
        if (variant==6) { tsfp_guest_store_u32(stack+16,0x04090084); expected=TSFP_STATUS_INVALID_PARAMETER; }
        assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==expected && g_esp==stack+44); preserved();
    }
    canaries(); frame(0x90400,0x90088,0x139a0); uint32_t word=tsfp_guest_load_u32(0x90400);
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==TSFP_STATUS_INVALID_PARAMETER && tsfp_guest_load_u32(0x90400)==word);
    tsfp_guest_read(0x90400,before,sizeof(before)); canaries(); frame(0x90084,0x90088,0x139a0);
    test_fail_any_native_create();
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==TSFP_STATUS_INSUFFICIENT_RESOURCES); preserved();
    tsfp_guest_read(0x90400,after,sizeof(after)); assert(!memcmp(before,after,sizeof(before)));
    /* A genuinely live native child cannot be silently killed by shutdown. */
    test_gate_next_native_create(); frame(0x90084,0,0x13200);
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==0);
    uint32_t live=tsfp_guest_load_u32(0x90084); test_gate_native_wait(0);
    assert(!tsfp_thread_service_stop(NULL) && tsfp_thread_handle_poll(live)==0x102 && !tsfp_storage_disable());
    test_gate_native_release(); test_gate_native_wait(1);
    assert(tsfp_thread_handle_close(live)==0);
    /* Open completed guest handles retain storage until closed or shutdown. */
    uint32_t handles[3];
    for (unsigned i=0;i<3;++i) {
        frame(0x90084,0,0x13200); assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==0);
        handles[i]=tsfp_guest_load_u32(0x90084);
    }
    canaries(); frame(0x90084,0x90088,0x13200);
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_eax==TSFP_STATUS_INSUFFICIENT_RESOURCES); preserved();
    TsfpWaitTimeout infinite={0,1};
    for (unsigned i=0;i<3;++i) assert(tsfp_thread_handle_wait(handles[i],&infinite)==0 && tsfp_thread_handle_close(handles[i])==0);
    unsigned before_cleanup=cleaned;
    assert(tsfp_guest_run(guarded_fault,NULL,&fault)==1 && cleaned==before_cleanup+1);
    assert(tsfp_guest_run(guarded_return,NULL,&fault)==0 && cleaned==before_cleanup+2);
    assert(tsfp_guest_run(guarded_unregister,NULL,&fault)==0 && cleaned==before_cleanup+2);
    assert(tsfp_guest_run(nested_guard,NULL,&fault)==0 && cleaned==before_cleanup+3);
    cancellation_restored();
    assert(tsfp_guest_thread_leave() && tsfp_thread_owner_finish(owner,0) && tsfp_thread_handle_close(owner)==0 && tsfp_thread_owner_release(owner));
    /* Two native guest callers queue independent creation requests concurrently. */
    assert(tsfp_guest_tls_prepare_xbe(0x89100,0x8c080,20,5,64));
    assert(tsfp_thread_object_prepare_tls(0x8a200,0x8b000,0x89100,50000,0x89140));
    uint32_t shutdown_handle=0;
    for (unsigned repeat=0;repeat<8;++repeat) {
        Parent parents[2]={{41,0x89000,0x8d800,0x8a000,0x900c0,0,0,repeat==7},{50000,0x89100,0x8d000,0x8a200,0x900d0,0,0,0}};
        pthread_t threads[2];
        for (unsigned i=0;i<2;++i) assert(tsfp_thread_owner_create(parents[i].object,parents[i].id,&parents[i].owner) && tsfp_thread_owner_start(parents[i].owner));
        for (unsigned i=0;i<2;++i) assert(!pthread_create(&threads[i],NULL,parent,&parents[i]));
        for (unsigned i=0;i<2;++i) {
            assert(!pthread_join(threads[i],NULL));
            assert(tsfp_thread_owner_finish(parents[i].owner,0) && tsfp_thread_handle_close(parents[i].owner)==0 && tsfp_thread_owner_release(parents[i].owner));
        }
        assert(parents[0].child!=parents[1].child); shutdown_handle=parents[0].child;
    }
    assert(tsfp_thread_handle_poll(shutdown_handle)==0);
    test_fail_native_join(); assert(!tsfp_thread_service_stop(&native_error) && native_error==EINVAL && !tsfp_storage_disable());
    assert(tsfp_thread_service_stop(&native_error) && !native_error && tsfp_thread_handle_poll(shutdown_handle)==TSFP_STATUS_INVALID_HANDLE);
    assert(tsfp_storage_disable() && !tsfp_thread_handles_busy());
    /* Restore original import ordinal meanings, without patching read-only IAT. */
    const uint32_t imports[]={0x80000068,0x80000140,0x80000102,0x800000bb};
    tsfp_guest_write(0x900a0,imports,sizeof(imports)); assert(tsfp_kernel_imports_prepare(0x900a0,4));
    puts("thread-service-v1: create=pass args=11,22 wait-close=pass faults=pass rollback=pass clients=2 repeats=8 shutdown=pass");
}
