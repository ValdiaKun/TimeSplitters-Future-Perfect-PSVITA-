/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "kernel.h"
#include "execution.h"
#include "memory.h"
#include <assert.h>
#include <stdio.h>
void fixture_title_entry(void); void fixture_kernel_call(void);
static void invoke(void *p) { (*(recomp_func_t *)p)(); }
void test_kernel(void) {
    const uint32_t table=0x88000,tib=0x89000,object=0x8a000,source=0x8b000;
    for (unsigned i=0;i<0x200;i+=4) tsfp_guest_store_u32(source+i,0x12345678);
    tsfp_guest_store_u32(0x8c004,99);
    assert(tsfp_guest_tls_prepare(tib,0x8c000,16,16));
    assert(!tsfp_thread_object_prepare(tib,source,tib,41));
    assert(!tsfp_thread_object_prepare(tib+0x04000000,source,tib,41));
    assert(tsfp_thread_object_prepare(object,source,tib,41));
    assert(tsfp_guest_load_u32(tib+0x28)==object && tsfp_guest_load_u32(object+0x12c)==41);
    assert(tsfp_guest_load_u32(object+0x28)==0x12345678);
    assert(tsfp_guest_thread_enter(41,0x8d000,0x1000,tib,96));
    tsfp_guest_store_u32(table,0x80000068); tsfp_guest_store_u32(table+4,0x80000140);
    tsfp_guest_store_u32(table+8,0x80000102);
    assert(tsfp_kernel_imports_prepare(table,3));
    assert(tsfp_guest_load_u32(table)==0xfe000000 && !tsfp_kernel_lookup(0xfe000001));
    for (unsigned i=0;i<16;++i) tsfp_guest_store_u8(0x90000+i,0xab);
    g_esi=table; PUSH32(g_esp,0);
    recomp_func_t fn=fixture_title_entry; TsfpFault f;
    assert(tsfp_guest_run(invoke,&fn,&f)==1);
    assert(f.kind==TSFP_THREAD_EXIT && f.detail==37 && f.thread_id==41);
    assert(g_edi==object && g_ebx==object && g_ecx==0 && tsfp_current_thread_object()==object && g_esp==0x8dff4);
    for (unsigned i=0;i<16;++i) assert(tsfp_guest_load_u8(0x90000+i)==0);
    assert(tsfp_guest_load_u32(g_fs_base+68)==99);
    /* Unknown import is diagnosed with its ordinal, never successful zero. */
    tsfp_guest_store_u32(table,0x800001ff); assert(tsfp_kernel_imports_prepare(table,1));
    g_esi=table; g_esp=0x8dff0; g_eax=77; fn=fixture_kernel_call;
    assert(tsfp_guest_run(invoke,&fn,&f)==1 && f.kind==TSFP_FAULT_IMPORT && f.detail==511);
    assert(g_eax==77 && g_esp==0x8dfec);
    tsfp_guest_store_u32(table,0); assert(!tsfp_kernel_imports_prepare(table,1));
    assert(tsfp_guest_load_u32(table)==0);
    /* Prior dispatch table remains installed after rejected rebinding. */
    tsfp_guest_store_u32(table,0xfe000000); g_esp=0x8dff0;
    assert(tsfp_guest_run(invoke,&fn,&f)==1 && f.detail==511);
    tsfp_guest_store_u32(table,0x80000140); assert(tsfp_kernel_imports_prepare(table,1));
    tsfp_guest_store_u32(0x73fffffc,0x12345678);
    g_esp=0x8dff0; tsfp_guest_store_u32(g_esp,0x73fffffc); tsfp_guest_store_u32(g_esp+4,8);
    g_eax=77;
    assert(tsfp_guest_run(invoke,&fn,&f)==1 && f.kind==TSFP_FAULT_MEMORY && f.access==TSFP_ACCESS_WRITE);
    assert(f.address==0x73fffffc && f.width==8 && g_eax==77 && g_esp==0x8dfec);
    assert(tsfp_guest_load_u32(0x73fffffc)==0x12345678);
    g_esp=0x73fffffc; g_eax=77;
    assert(tsfp_guest_run(invoke,&fn,&f)==1 && f.kind==TSFP_FAULT_MEMORY && f.access==TSFP_ACCESS_READ);
    assert(f.address==0x73fffff8 && f.width==12 && g_esp==0x73fffff8 && g_eax==77);
    /* Zero length ignores even invalid destination, with exact stdcall cleanup. */
    g_esp=0x8dfe0; tsfp_guest_store_u32(g_esp,0xfd000000); tsfp_guest_store_u32(g_esp+4,0);
    tsfp_guest_store_u32(g_esp+8,0);
    assert(tsfp_guest_run(invoke,&fn,&f)==0 && g_esp==0x8dfec);
    tsfp_guest_store_u32(table,0x80000140); tsfp_guest_store_u32(table+4,0x80000068);
    assert(tsfp_kernel_imports_prepare(table,2));
    recomp_func_t cached=tsfp_kernel_lookup(0xfe000000);
    assert(tsfp_kernel_lookup(0xfe000004)); /* Must not reroute cached handler. */
    g_esp=0x8dff0; tsfp_guest_store_u32(g_esp,0); tsfp_guest_store_u32(g_esp+4,0x90000);
    tsfp_guest_store_u32(g_esp+8,4); tsfp_guest_store_u32(0x90000,0xabcdef01);
    assert(tsfp_guest_run(invoke,&cached,&f)==0 && g_eax==0 && g_esp==0x8dffc);
    assert(tsfp_guest_load_u32(0x90000)==0);
    assert(tsfp_guest_thread_leave());
    tsfp_kernel_imports_reset(); assert(!tsfp_kernel_lookup(0xfe000000));
    puts("kernel-entry-v1: object=pass imports=3 stdcall=pass zero=pass exit=37 unsupported=pass");
}
