/* SPDX-License-Identifier: GPL-3.0-only */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "kernel.h"
#include "execution.h"
#include "memory.h"
#include <string.h>
#define THREAD_SIZE 0x200u
#define THREAD_ID 0x12cu
#define THUNK_BASE 0xfe000000u
static uint32_t ordinals[16];
static size_t slot_count;
int tsfp_thread_object_prepare(uint32_t object, uint32_t source, uint32_t tib, uint32_t id) {
    if (!id || !tsfp_guest_range_valid(object,THREAD_SIZE) || !tsfp_guest_range_valid(source,THREAD_SIZE) ||
        !tsfp_guest_range_valid(tib,0x40)) return 0;
    /* Require single physical spans so aliases can be checked for overlap. */
    TsfpGuestSpan a=tsfp_guest_classify(object), b=tsfp_guest_classify(tib);
    if (a.available<THREAD_SIZE || b.available<0x40) return 0;
    uintptr_t x=(uintptr_t)tsfp_guest_pointer(object,THREAD_SIZE), y=(uintptr_t)tsfp_guest_pointer(tib,0x40);
    if (x<=y ? y-x<THREAD_SIZE : x-y<0x40) return 0;
    unsigned char image[THREAD_SIZE];
    tsfp_guest_read(source,image,sizeof(image)); memcpy(image+THREAD_ID,&id,4);
    tsfp_guest_write(object,image,sizeof(image)); tsfp_guest_store_u32(tib+0x28,object);
    return 1;
}
uint32_t tsfp_current_thread_object(void) {
    if (!tsfp_guest_thread_id() || g_fs_base>UINT32_MAX-0x28u)
        tsfp_guest_raise(TSFP_FAULT_DISPATCH,g_fs_base,0,104);
    uint32_t object=tsfp_guest_load_u32(g_fs_base+0x28);
    if (!tsfp_guest_range_valid(object,THREAD_SIZE))
        tsfp_guest_raise(TSFP_FAULT_DISPATCH,object,0,104);
    if (tsfp_guest_load_u32(object+THREAD_ID)!=tsfp_guest_thread_id())
        tsfp_guest_raise(TSFP_FAULT_DISPATCH,object,0,104);
    return object;
}
int tsfp_kernel_imports_validate(const uint32_t *markers, size_t count) {
    if (!markers || !count || count>16) return 0;
    for (size_t i=0;i<count;++i)
        if (!(markers[i]&0x80000000u) || !(markers[i]&0x7fffffffu) || (markers[i]&0x7fffffffu)>511) return 0;
    return 1;
}
int tsfp_kernel_imports_prepare(uint32_t table, size_t count) {
    if (!count || count>16 || !tsfp_guest_range_valid(table,count*4)) return 0;
    uint32_t input[16], output[16], next[16];
    tsfp_guest_read(table,input,count*4);
    if (!tsfp_kernel_imports_validate(input,count)) return 0;
    for (size_t i=0;i<count;++i) {
        next[i]=input[i]&0x7fffffffu; output[i]=THUNK_BASE+(uint32_t)i*4;
    }
    tsfp_guest_write(table,output,count*4);
    memcpy(ordinals,next,count*4); slot_count=count;
    return 1;
}
static void dispatch(uint32_t ordinal) {
    unsigned arguments;
    switch (ordinal) {
    case 104: arguments=0; break; /* KeGetCurrentThread */
    case 320: arguments=2; break; /* RtlZeroMemory */
    case 258: arguments=1; break; /* PsTerminateSystemThread */
    default: tsfp_guest_raise(TSFP_FAULT_IMPORT,THUNK_BASE,0,ordinal);
    }
    uint32_t stack=g_esp, frame[3];
    tsfp_guest_read(stack,frame,(arguments+1)*4); /* all args before side effects */
    if (ordinal==258) tsfp_guest_thread_exit(frame[1]);
    uint32_t result=0;
    if (ordinal==104) result=tsfp_current_thread_object();
    else if (frame[2]) {
        if (!tsfp_guest_range_valid(frame[1],frame[2])) {
            g_tsfp_access=TSFP_ACCESS_WRITE;
            tsfp_guest_memory_fault(frame[1],frame[2]);
        }
        static const unsigned char zero[256]={0};
        size_t left=frame[2]; uint32_t address=frame[1];
        while (left) {
            size_t n=left<sizeof(zero) ? left : sizeof(zero);
            tsfp_guest_write(address,zero,n); left-=n; if (left) address+=(uint32_t)n;
        }
    }
    g_eax=result; g_esp=stack+(arguments+1)*4;
}
static void current_thread(void) { dispatch(104); }
static void zero_memory(void) { dispatch(320); }
static void terminate_thread(void) { dispatch(258); }
#define BAD_SLOT(n) static void unsupported_##n(void) { tsfp_guest_raise(TSFP_FAULT_IMPORT,THUNK_BASE+4u*n,0,ordinals[n]); }
BAD_SLOT(0) BAD_SLOT(1) BAD_SLOT(2) BAD_SLOT(3)
BAD_SLOT(4) BAD_SLOT(5) BAD_SLOT(6) BAD_SLOT(7)
BAD_SLOT(8) BAD_SLOT(9) BAD_SLOT(10) BAD_SLOT(11)
BAD_SLOT(12) BAD_SLOT(13) BAD_SLOT(14) BAD_SLOT(15)
#undef BAD_SLOT
static recomp_func_t const unsupported_slots[16]={
    unsupported_0,unsupported_1,unsupported_2,unsupported_3,
    unsupported_4,unsupported_5,unsupported_6,unsupported_7,
    unsupported_8,unsupported_9,unsupported_10,unsupported_11,
    unsupported_12,unsupported_13,unsupported_14,unsupported_15
};
void tsfp_kernel_imports_reset(void) { slot_count=0; memset(ordinals,0,sizeof(ordinals)); }
void (*tsfp_kernel_lookup(uint32_t address))(void) {
    if (address<THUNK_BASE || (address&3u) || (address-THUNK_BASE)/4>=slot_count) return NULL;
    uint32_t slot=(address-THUNK_BASE)/4;
    switch (ordinals[slot]) {
    case 104: return current_thread;
    case 320: return zero_memory;
    case 258: return terminate_thread;
    default: return unsupported_slots[slot];
    }
}
