/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "heap.h"
#include "image.h"
#include "memory.h"
#include "kernel.h"
#include "thread_service.h"
#include "storage.h"
#include "handles.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
void fixture_fault_store(void);
void fixture_thread_args(void);
static TsfpImageFunction resolve(uint32_t address) { return address==0x13200 ? fixture_thread_args : NULL; }
void fixture_service_call(void); void fixture_allocation_sequence(void);
static _Thread_local uint32_t stack_top,output;
static void invoke(void *context) { (*(TsfpImageFunction *)context)(); }
static uint32_t call(unsigned slot,unsigned count,const uint32_t *args,TsfpFault *fault) {
    g_esp=stack_top-64; g_esi=0x900e0+slot*4;
    tsfp_guest_write(g_esp,args,count*4); tsfp_guest_store_u32(g_esp+count*4,0);
    TsfpCpuState before,after; tsfp_cpu_capture(&before); TsfpImageFunction f=fixture_service_call;
    if (tsfp_guest_run(invoke,&f,fault)) return UINT32_MAX;
    tsfp_cpu_capture(&after);
    assert(g_esp==stack_top-64+(count+1)*4 && before.flags==after.flags && before.fs_base==after.fs_base);
    return g_eax;
}
static uint32_t operation(int free_op,uint32_t *base,uint32_t *size,uint32_t type) {
    tsfp_guest_store_u32(output,*base); tsfp_guest_store_u32(output+4,*size);
    uint32_t alloc[]={output,0,output+4,type,4},release[]={output,output+4,type}; TsfpFault fault;
    uint32_t status=free_op ? call(1,3,release,&fault) : call(0,5,alloc,&fault);
    assert(status!=UINT32_MAX); *base=tsfp_guest_load_u32(output); *size=tsfp_guest_load_u32(output+4); return status;
}
static void split_write(void *context) { tsfp_guest_store_u32(*(uint32_t *)context,0xdeadbeef); }
static void access(void *context) { (void)tsfp_guest_load_u32(*(uint32_t *)context); }
static void inaccessible(uint32_t address) {
    TsfpFault fault; assert(tsfp_guest_run(access,&address,&fault)==1 && fault.kind==TSFP_FAULT_MEMORY && fault.access==TSFP_ACCESS_READ && fault.address==address);
}
static void trace(void) {
    static unsigned traces; int report=!traces++;
    uint32_t b,s,code; unsigned row=0;
#define ROW(free_op,base,size,type) do { b=base;s=size;code=operation(free_op,&b,&s,type);if (report) printf("%s[%u,%u,%u]",row++?",":"",code,b,s); } while (0)
    if (report) printf("vmem-trace: [");
    ROW(0,0x100001,1,0x3000); assert(!code && b==0x100000 && s==0x1000);
    tsfp_guest_store_u32(b,0xa5);
    ROW(0,0x100000,0x1000,0x3000); assert(code==0xc0000018u);
    ROW(0,0x100000,0x1000,0x1000); assert(!code && tsfp_guest_load_u32(b)==0xa5);
    ROW(1,0x100001,1,0x4000); assert(!code); inaccessible(0x100000); inaccessible(0x04100000);
    ROW(0,0x100000,0x1000,0x1000); assert(!code && !tsfp_guest_load_u32(b));
    ROW(1,0x100001,0,0x8000); assert(code==0xc000000du);
    ROW(1,0x100000,0,0x8000); assert(!code && !b && !s); if (report) puts("]");
#undef ROW
}
static void *parallel(void *context) {
    unsigned id=(unsigned)(uintptr_t)context; uint32_t stack=0x91000+id*0x1000;
    assert(tsfp_guest_thread_enter(700+id,stack,0x1000,0x89100+id*0x100,164));
    stack_top=stack+0x1000; output=stack+0x40;
    for (unsigned i=0;i<32;++i) {
        uint32_t b=0,s=1; assert(!operation(0,&b,&s,0x3000) && s==4096);
        tsfp_guest_store_u32(b,700+id); assert(tsfp_guest_load_u32(b)==700+id);
        s=0; assert(!operation(1,&b,&s,0x8000));
    }
    assert(tsfp_guest_thread_leave()); return NULL;
}
static _Thread_local int fail_after_pin;
static struct { pthread_mutex_t mutex; pthread_cond_t condition; int armed,entered,release; } output_gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
uint32_t __real_tsfp_event_create(unsigned,int,uint32_t *);
uint32_t __wrap_tsfp_event_create(unsigned type,int initial,uint32_t *handle) {
    assert(!pthread_mutex_lock(&output_gate.mutex));
    if (output_gate.armed) {
        output_gate.armed=0; output_gate.entered=1; assert(!pthread_cond_broadcast(&output_gate.condition));
        while (!output_gate.release) assert(!pthread_cond_wait(&output_gate.condition,&output_gate.mutex));
    }
    assert(!pthread_mutex_unlock(&output_gate.mutex));
    if (fail_after_pin) { fail_after_pin=0; g_tsfp_access=TSFP_ACCESS_WRITE; tsfp_guest_memory_fault(0xfd000000,4); }
    return __real_tsfp_event_create(type,initial,handle);
}
static void *event_output_worker(void *context) {
    uint32_t b=(uint32_t)(uintptr_t)context;
    assert(tsfp_guest_thread_enter(700,0x91000,0x1000,0x89100,164)); stack_top=0x92000;
    uint32_t args[]={b+0x04000000,0,0,0}; TsfpFault fault;
    assert(!call(2,4,args,&fault)); uint32_t handle=tsfp_guest_load_u32(b);
    uint32_t set_args[]={handle,b+4}; assert(!call(3,2,set_args,&fault) && !tsfp_guest_load_u32(b+4));
    assert(!tsfp_handle_close(handle) && tsfp_guest_thread_leave()); return NULL;
}
static void test_output_pins(void) {
    uint32_t b=0,s=4096; assert(!operation(0,&b,&s,0x3000));
    uint32_t refs[64],extra=0xdeadbeef;
    for (unsigned i=0;i<64;++i) assert(!tsfp_heap_pin_output(b+0x04000000,4,&refs[i]) && refs[i]);
    assert(tsfp_heap_pin_output(b,4,&extra)==0xc000009au && extra==0xdeadbeef);
    uint32_t full_args[]={b,0,0,0};TsfpFault full_fault;
    assert(call(2,4,full_args,&full_fault)==0xc000009au && !tsfp_thread_handles_busy());
    s=0; uint32_t copy=b; assert(operation(1,&copy,&s,0x8000)==0xc0000001u && copy==b && !s);
    for (unsigned i=0;i<64;++i) assert(tsfp_heap_unpin_output(refs[i]));
    assert(!tsfp_heap_unpin_output(refs[0]));
    assert(!tsfp_heap_pin_output(b,4,&extra) && extra!=refs[0] && !tsfp_heap_unpin_output(refs[0]));
    assert(tsfp_heap_unpin_output(extra));
    assert(!pthread_mutex_lock(&output_gate.mutex)); output_gate.armed=1; output_gate.entered=output_gate.release=0;
    assert(!pthread_mutex_unlock(&output_gate.mutex));
    pthread_t thread; assert(!pthread_create(&thread,NULL,event_output_worker,(void *)(uintptr_t)b));
    assert(!pthread_mutex_lock(&output_gate.mutex));
    struct timespec deadline; assert(!clock_gettime(CLOCK_REALTIME,&deadline)); deadline.tv_sec+=5;
    while (!output_gate.entered) assert(!pthread_cond_timedwait(&output_gate.condition,&output_gate.mutex,&deadline));
    assert(!pthread_mutex_unlock(&output_gate.mutex));
    s=0;copy=b;assert(operation(1,&copy,&s,0x8000)==0xc0000001u && copy==b);
    s=1;copy=b;assert(operation(1,&copy,&s,0x4000)==0xc0000001u && s==1);
    assert(!pthread_mutex_lock(&output_gate.mutex)); output_gate.release=1;
    assert(!pthread_cond_broadcast(&output_gate.condition) && !pthread_mutex_unlock(&output_gate.mutex));
    assert(!pthread_join(thread,NULL));
    /* Unsupported attributes and faulting output probes clean the durable guard. */
    uint32_t args[]={b,0x13300,0,0};TsfpFault fault;
    assert(call(2,4,args,&fault)==0xc00000bbu);
    args[1]=0xfd000000; assert(call(2,4,args,&fault)==UINT32_MAX && fault.access==TSFP_ACCESS_READ);
    args[1]=0;args[0]=0x93ffe;assert(call(2,4,args,&fault)==UINT32_MAX && fault.access==TSFP_ACCESS_WRITE);
    args[0]=b;fail_after_pin=1;assert(call(2,4,args,&fault)==UINT32_MAX && fault.address==0xfd000000 && !fail_after_pin);
    uint32_t invalid_set[]={0xdeadbeef,b+4};assert(call(3,2,invalid_set,&fault)==TSFP_STATUS_INVALID_HANDLE);
    s=0;assert(!operation(1,&b,&s,0x8000)); /* all event references released */
    int old_cancel;assert(!pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old_cancel) && old_cancel==PTHREAD_CANCEL_ENABLE);
    assert(!pthread_setcancelstate(old_cancel,NULL));
    puts("heap-outputs-v1: event-create=pass set=pass mirror=pass free-race=pass decommit-race=pass stale=pass quota=64 cleanup=pass");
}
static struct { pthread_mutex_t mutex;pthread_cond_t changed;int armed,entered,release; } vm_gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER};
static _Thread_local int fail_vm_after_pins;
uint32_t __real_tsfp_heap_allocate(uint32_t *,uint32_t *,uint32_t,uint32_t,uint32_t);
uint32_t __wrap_tsfp_heap_allocate(uint32_t *base,uint32_t *size,uint32_t bits,uint32_t type,uint32_t protect) {
    assert(!pthread_mutex_lock(&vm_gate.mutex));
    if(vm_gate.armed) {
        vm_gate.armed=0;vm_gate.entered=1;assert(!pthread_cond_broadcast(&vm_gate.changed));
        while(!vm_gate.release) assert(!pthread_cond_wait(&vm_gate.changed,&vm_gate.mutex));
    }
    assert(!pthread_mutex_unlock(&vm_gate.mutex));
    if(fail_vm_after_pins) { fail_vm_after_pins=0;tsfp_guest_raise(TSFP_FAULT_DIVIDE,0x132a0,0,0); }
    return __real_tsfp_heap_allocate(base,size,bits,type,protect);
}
static uint32_t vm_words(int release,uint32_t bp,uint32_t sp,uint32_t base,uint32_t size,uint32_t type) {
    tsfp_guest_store_u32(bp,base);tsfp_guest_store_u32(sp,size);
    uint32_t alloc[]={bp,0,sp,type,4},free_args[]={bp,sp,type};TsfpFault fault;
    uint32_t status=release ? call(1,3,free_args,&fault) : call(0,5,alloc,&fault);
    assert(status!=UINT32_MAX);return status;
}
typedef struct VmCaller { uint32_t bp,sp,target; } VmCaller;
static void *vm_output_worker(void *context) {
    VmCaller *p=context;assert(tsfp_guest_thread_enter(700,0x91000,0x1000,0x89100,164));stack_top=0x92000;
    assert(!vm_words(0,p->bp,p->sp,0,1,0x3000));p->target=tsfp_guest_load_u32(p->bp);
    assert(tsfp_guest_thread_leave());return NULL;
}
static void vm_output_cases(void) {
    uint32_t container=0,size=4096,other=0;assert(!operation(0,&container,&size,0x3000));
    size=4096;assert(!operation(0,&other,&size,0x3000));
    uint32_t bp=container+0x04000000,sp=other;
    assert(!vm_words(0,bp,sp,0,1,0x2000));uint32_t target=tsfp_guest_load_u32(bp);inaccessible(target);
    assert(!vm_words(0,bp,sp,target,1,0x1000) && !tsfp_guest_load_u32(target));
    tsfp_guest_store_u32(target,0x12345678);assert(!vm_words(0,bp,sp,target,1,0x1000) && tsfp_guest_load_u32(target)==0x12345678);
    assert(!vm_words(1,bp,sp,target+1,1,0x4000));inaccessible(target);
    assert(!vm_words(0,bp,sp,target,1,0x1000) && !tsfp_guest_load_u32(target));
    assert(!vm_words(1,bp,sp,target,0,0x8000) && !tsfp_guest_load_u32(bp) && !tsfp_guest_load_u32(sp));
    /* Mixed heap/image words, then deliberate destructive self-reference. */
    assert(!vm_words(0,container,0x90104,0,1,0x3000));target=tsfp_guest_load_u32(container);
    assert(!vm_words(1,container,0x90104,target,0,0x8000));
    assert(!vm_words(0,0x90100,other,0,1,0x3000));target=tsfp_guest_load_u32(0x90100);
    assert(!vm_words(1,0x90100,other,target,0,0x8000));
    assert(vm_words(0,container,other,0,0x40000,0x3000)==0xc0000017u && !tsfp_guest_load_u32(container) && tsfp_guest_load_u32(other)==0x40000);
    assert(vm_words(0,container,other,0,1,0x3001)==0xc00000bbu && !tsfp_guest_load_u32(container) && tsfp_guest_load_u32(other)==1);
    assert(vm_words(1,container,container+4,container,0,0x8000)==0xc0000001u);
    assert(tsfp_guest_load_u32(container)==container && !tsfp_guest_load_u32(container+4));
    assert(vm_words(1,container,container+4,container,1,0x4000)==0xc0000001u);
    assert(tsfp_guest_load_u32(container)==container && tsfp_guest_load_u32(container+4)==1);
    tsfp_guest_store_u32(container+32,0xfeed1234);
    assert(!vm_words(0,container,container+4,container,1,0x1000) && tsfp_guest_load_u32(container+32)==0xfeed1234);
    TsfpFault fault;uint32_t args[]={container,0,0x93ffe,0x3000,4};
    assert(call(0,5,args,&fault)==UINT32_MAX && fault.access==TSFP_ACCESS_WRITE);
    args[2]=other;fail_vm_after_pins=1;
    assert(call(0,5,args,&fault)==UINT32_MAX && fault.kind==TSFP_FAULT_DIVIDE && !fail_vm_after_pins);
    const uint32_t aliases[]={container+0x04000000,container+2};
    for(unsigned i=0;i<2;++i) {
        unsigned char before[8],after[8];tsfp_guest_read(container,before,8);args[2]=aliases[i];
        assert(call(0,5,args,&fault)==TSFP_STATUS_INVALID_PARAMETER);tsfp_guest_read(container,after,8);assert(!memcmp(before,after,8));
    }
    uint32_t refs[64];for(unsigned i=0;i<63;++i) assert(!tsfp_heap_pin_output(container,4,&refs[i]));
    assert(vm_words(0,container,other,0,1,0x3000)==0xc000009au && !tsfp_guest_load_u32(container) && tsfp_guest_load_u32(other)==1);
    assert(!tsfp_heap_pin_output(container,4,&refs[63]));
    assert(vm_words(0,container,other,0,1,0x3000)==0xc000009au);
    for(unsigned i=0;i<64;++i) assert(tsfp_heap_unpin_output(refs[i]));
    /* Both references are acquired before the allocator can mutate anything. */
    assert(!pthread_mutex_lock(&vm_gate.mutex));vm_gate.armed=1;vm_gate.entered=vm_gate.release=0;
    assert(!pthread_mutex_unlock(&vm_gate.mutex));
    VmCaller caller={bp,sp,0};pthread_t thread;assert(!pthread_create(&thread,NULL,vm_output_worker,&caller));
    assert(!pthread_mutex_lock(&vm_gate.mutex));struct timespec end;assert(!clock_gettime(CLOCK_REALTIME,&end));end.tv_sec+=5;
    while(!vm_gate.entered) assert(!pthread_cond_timedwait(&vm_gate.changed,&vm_gate.mutex,&end));
    assert(!pthread_mutex_unlock(&vm_gate.mutex));
    uint32_t b=container,s=0;assert(operation(1,&b,&s,0x8000)==0xc0000001u && b==container && !s);
    b=other;s=1;assert(operation(1,&b,&s,0x4000)==0xc0000001u && b==other && s==1);
    assert(!pthread_mutex_lock(&vm_gate.mutex));vm_gate.release=1;
    assert(!pthread_cond_broadcast(&vm_gate.changed) && !pthread_mutex_unlock(&vm_gate.mutex));
    assert(!pthread_join(thread,NULL));b=caller.target;s=0;assert(!operation(1,&b,&s,0x8000));
    b=container;s=0;assert(!operation(1,&b,&s,0x8000));b=other;s=0;assert(!operation(1,&b,&s,0x8000));
    int old;assert(!pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&old) && old==PTHREAD_CANCEL_ENABLE);assert(!pthread_setcancelstate(old,NULL));
    puts("vm-outputs-v1: reserve=pass commit=pass free=pass separate=pass mirror=pass self-reject=pass races=pass faults=pass exhaustion=pass");
}
void test_heap(void) {
    assert(!tsfp_heap_configured());
    unsigned char *raw=tsfp_guest_pointer(0x100000,0x40000); memset(raw,0x5a,0x40000);
    assert(!tsfp_heap_configure(0x100001,0x10000) && !tsfp_heap_configure(0x100000,1));
    assert(!tsfp_heap_configure(0x88000,0x10000) && !tsfp_heap_configure(0xffff0000,0x10000));
    assert(tsfp_heap_configure(0x100000,0x40000) && raw[0]==0x5a && raw[0x3ffff]==0x5a);
    assert(!tsfp_heap_configure(0x100000,0x40000) && !tsfp_image_reset());
    TsfpStorageConfig overlap={0x04100000,0x1400,3,256*1024,resolve};
    assert(tsfp_storage_configure(&overlap)!=TSFP_STORAGE_OK);
    assert(!tsfp_guest_thread_enter(123,0x100000,0x1000,0x89000,164));
    const uint32_t markers[]={0x800000b8,0x800000c7,0x800000bd,0x800000e1};
    tsfp_guest_write(0x900e0,markers,sizeof(markers)); assert(tsfp_kernel_imports_prepare(0x900e0,4));
    assert(tsfp_guest_thread_enter(41,0x8d000,0x1000,0x89000,164)); stack_top=0x8e000; output=0x90100;
    inaccessible(0x100000); inaccessible(0x04100000);
    trace();
    vm_output_cases();
    uint32_t b=0,s=3; assert(!operation(0,&b,&s,0x2000) && b==0x100000 && s==4096);
    inaccessible(b); uint32_t reserved=b; s=1; assert(!operation(0,&b,&s,0x1000));
    assert(!tsfp_guest_load_u32(b)); tsfp_guest_store_u32(0x04100000,0x87654321); assert(tsfp_guest_load_u32(b)==0x87654321);
    assert(!tsfp_guest_access_allowed(b,4,4) && !tsfp_guest_access_allowed(b+0x04000000,4,4));
    /* Whole-span validation prevents partial writes into the committed page. */
    TsfpFault crossing; uint32_t split=b+4094; raw[4094]=0x12; raw[4095]=0x34;
    assert(tsfp_guest_run(split_write,&split,&crossing)==1 && crossing.access==TSFP_ACCESS_WRITE && crossing.width==4);
    assert(raw[4094]==0x12 && raw[4095]==0x34 && !tsfp_guest_access_allowed(b+4096,4,1));
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    /* The PC oracle's raw lvalues do not implement software page policy. The
     * actual ARM lowering must reject a translated cross-page store wholly. */
    g_edx=split; g_eax=0xdeadbeef; g_esp=stack_top; PUSH32(g_esp,0);
    TsfpImageFunction store=fixture_fault_store;
    assert(tsfp_guest_run(invoke,&store,&crossing)==1 && crossing.cpu.pc==0x13280 && crossing.access==TSFP_ACCESS_WRITE && crossing.width==4);
    assert(raw[4094]==0x12 && raw[4095]==0x34);
#endif
    assert(!tsfp_guest_thread_enter(124,b,0x1000,0x89000,164));
    /* Rejection preserves both output words and committed contents. */
    const uint32_t bad_types[]={0,0x3001,0x100000,0x4000};
    for (unsigned i=0;i<4;++i) { b=0x110000;s=1; assert(operation(0,&b,&s,bad_types[i])==0xc00000bbu && b==0x110000 && s==1); }
    uint32_t invalid[]={output,1,output+4,0x3000,4}; TsfpFault fault;
    assert(call(0,5,invalid,&fault)==0xc00000bbu); invalid[1]=0; invalid[4]=0x40;
    assert(call(0,5,invalid,&fault)==0xc00000bbu);
    invalid[4]=4; invalid[2]=0x04090100; assert(call(0,5,invalid,&fault)==0xc000000du);
    invalid[2]=output+4; invalid[0]=0x04100000; assert(call(0,5,invalid,&fault)==0xc0000018u); /* valid output, invalid requested base */
    invalid[0]=0x93ffe; assert(call(0,5,invalid,&fault)==UINT32_MAX && fault.kind==TSFP_FAULT_MEMORY && fault.access==TSFP_ACCESS_WRITE);
    assert(tsfp_guest_load_u32(reserved)==0x87654321);
    tsfp_guest_store_u32(output,0x110000); invalid[0]=output; invalid[2]=0x93ffe;
    assert(call(0,5,invalid,&fault)==UINT32_MAX && fault.access==TSFP_ACCESS_WRITE && tsfp_guest_load_u32(output)==0x110000);
    /* Failure of the second output did not reserve the requested range. */
    b=0x110000;s=1; assert(!operation(0,&b,&s,0x3000)); s=0; assert(!operation(1,&b,&s,0x8000));
    b=0x04100000;s=1; assert(operation(0,&b,&s,0x3000)==0xc0000018u); /* alias isn't fresh RAM */
    b=0xfffffff0;s=0x100; assert(operation(0,&b,&s,0x3000)==0xc0000018u);
    b=reserved;s=1; assert(operation(1,&b,&s,0x8000)==0xc000000du);
    b=reserved;s=0; assert(!operation(1,&b,&s,0x8000));
    b=reserved;s=0; assert(operation(1,&b,&s,0x8000)==0xc000000du); /* double free */
    b=0x100000;s=0x10000; assert(!operation(0,&b,&s,0x3000));
    b=0x110000;s=0x10000; assert(!operation(0,&b,&s,0x3000));
    tsfp_guest_store_u32(0x10fffc,0x1234); tsfp_guest_store_u32(0x110000,0x5678);
    b=0x10fffe;s=4; assert(operation(1,&b,&s,0x4000)==0xc000000du);
    assert(tsfp_guest_load_u32(0x10fffc)==0x1234 && tsfp_guest_load_u32(0x110000)==0x5678);
    b=0x100000;s=0; assert(!operation(1,&b,&s,0x8000)); b=0x110000;s=0; assert(!operation(1,&b,&s,0x8000));
    /* Fragmentation: four tiny reservations occupy four granules; free holes
     * cannot satisfy a two-granule contiguous request. No large-reserve clamp. */
    uint32_t blocks[4];
    for (unsigned i=0;i<4;++i) { b=0;s=1; assert(!operation(0,&b,&s,0x3000)); blocks[i]=b; }
    b=0;s=1; assert(operation(0,&b,&s,0x3000)==0xc0000017u && b==0 && s==1);
    b=blocks[0];s=0; assert(!operation(1,&b,&s,0x8000)); b=blocks[2];s=0; assert(!operation(1,&b,&s,0x8000));
    b=0;s=0x20000; assert(operation(0,&b,&s,0x3000)==0xc0000017u);
    b=blocks[1];s=0; assert(!operation(1,&b,&s,0x8000)); b=blocks[3];s=0; assert(!operation(1,&b,&s,0x8000));
    b=0;s=0x40001; assert(operation(0,&b,&s,0x3000)==0xc0000017u && s==0x40001);
    /* Authored x86 allocates, stores/loads guest RAM, frees and returns via IAT. */
    tsfp_guest_store_u32(output,0); tsfp_guest_store_u32(output+4,1); g_esp=stack_top; PUSH32(g_esp,0);
    TsfpImageFunction fn=fixture_allocation_sequence;
    assert(tsfp_guest_run(invoke,&fn,&fault)==0 && g_esp==stack_top);
    assert(!tsfp_guest_load_u32(0x90120) && tsfp_guest_load_u32(0x90124)==0x12345678 && !tsfp_guest_load_u32(0x90128));
    test_output_pins();
    assert(tsfp_guest_thread_leave());
    pthread_t threads[2]; for (unsigned i=0;i<2;++i) assert(!pthread_create(&threads[i],NULL,parallel,(void *)(uintptr_t)i));
    for (unsigned i=0;i<2;++i) assert(!pthread_join(threads[i],NULL));
    b=0;s=1; assert(!tsfp_heap_allocate(&b,&s,0,0x3000,4) && !tsfp_heap_disable());
    s=0; assert(!tsfp_heap_free(&b,&s,0x8000) && tsfp_heap_disable());
    assert(tsfp_guest_access_allowed(0x100000,0x40000,3));
    overlap.base=0x100000;
    assert(tsfp_storage_configure(&overlap)==TSFP_STORAGE_OK);
    unsigned char saved=raw[0]; assert(!tsfp_heap_configure(0x100000,0x40000) && raw[0]==saved);
    assert(tsfp_storage_disable());
    const uint32_t restore[]={0x80000068,0x80000140,0x80000102,0x800000bb};
    tsfp_guest_write(0x900e0,restore,sizeof(restore)); assert(tsfp_kernel_imports_prepare(0x900e0,4));
    puts("heap-v1: reserve=pass commit=pass decommit=pass release=pass zero=pass aliases=pass rollback=pass fragmentation=pass callers=2 repeats=32");
}
