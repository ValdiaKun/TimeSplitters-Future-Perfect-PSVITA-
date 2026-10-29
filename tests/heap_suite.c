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
#include "storage.h"
#include "handles.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
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
    const uint32_t markers[]={0x800000b8,0x800000c7};
    tsfp_guest_write(0x900e0,markers,sizeof(markers)); assert(tsfp_kernel_imports_prepare(0x900e0,2));
    assert(tsfp_guest_thread_enter(41,0x8d000,0x1000,0x89000,164)); stack_top=0x8e000; output=0x90100;
    inaccessible(0x100000); inaccessible(0x04100000);
    trace();
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
    invalid[2]=output+4; invalid[0]=0x04100000; assert(call(0,5,invalid,&fault)==0xc000000du);
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
