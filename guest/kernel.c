/* SPDX-License-Identifier: GPL-3.0-only */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "kernel.h"
#include "heap.h"
#include <pthread.h>
#include <stdlib.h>
#include "handles.h"
#include "storage.h"
#include "thread_service.h"
#include "execution.h"
#include "memory.h"
#include <string.h>
#define THREAD_SIZE 0x200u
#define THREAD_ID 0x12cu
#define THUNK_BASE 0xfe000000u
static uint32_t ordinals[16];
static size_t slot_count;
int tsfp_thread_object_prepare_tls(uint32_t object,uint32_t source,uint32_t tib,uint32_t id,uint32_t tls_block) {
    if (!id || !tsfp_guest_range_valid(object,THREAD_SIZE) || !tsfp_guest_range_valid(source,THREAD_SIZE) ||
        !tsfp_guest_range_valid(tib,0x40)) return 0;
    if (!tsfp_guest_access_allowed(object,THREAD_SIZE,2) || !tsfp_guest_access_allowed(tib,0x40,2) ||
        !tsfp_guest_access_allowed(source,THREAD_SIZE,1) || (tls_block && ((uint64_t)tib+64>UINT32_MAX || tls_block!=tib+64 || !tsfp_guest_range_valid(tls_block,4) || !tsfp_guest_access_allowed(tls_block,4,2)))) return 0;
    /* Require single physical spans so aliases can be checked for overlap. */
    TsfpGuestSpan a=tsfp_guest_classify(object), b=tsfp_guest_classify(tib);
    if (a.available<THREAD_SIZE || b.available<0x40) return 0;
    uintptr_t x=(uintptr_t)tsfp_guest_pointer(object,THREAD_SIZE), y=(uintptr_t)tsfp_guest_pointer(tib,0x40);
    if (x<=y ? y-x<THREAD_SIZE : x-y<0x40) return 0;
    unsigned char image[THREAD_SIZE];
    tsfp_guest_read(source,image,sizeof(image)); memcpy(image+THREAD_ID,&id,4);
    if (tls_block) memcpy(image+0x28,&tls_block,4);
    tsfp_guest_write(object,image,sizeof(image)); tsfp_guest_store_u32(tib+0x28,object);
    return 1;
}
int tsfp_thread_object_prepare(uint32_t object,uint32_t source,uint32_t tib,uint32_t id) {
    return tsfp_thread_object_prepare_tls(object,source,tib,id,0);
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
static uint32_t create_thread(const uint32_t *frame) {
    /* Complete frame was prefetched before acquiring the durable service guard. */
    uint32_t outputs[]={frame[1],frame[5]};
    for (unsigned i=0;i<2;++i) if (!i || outputs[i]) {
        if (!tsfp_guest_range_valid(outputs[i],4) || !tsfp_guest_access_allowed(outputs[i],4,2)) {
            g_tsfp_access=TSFP_ACCESS_WRITE; tsfp_guest_memory_fault(outputs[i],4);
        }
        if (!tsfp_storage_output_allowed(outputs[i],4)) return TSFP_STATUS_INVALID_PARAMETER;
    }
    if (outputs[1] && tsfp_tls_ranges_overlap(outputs[0],4,outputs[1],4)) return TSFP_STATUS_INVALID_PARAMETER;
    TsfpThreadCreateRequest request={frame[3],frame[4],frame[10],frame[6],frame[7]};
    TsfpThreadCreateResult result=tsfp_thread_service_create(&request);
    if (!result.status) {
        /* Service/image pins keep both validated spans and permissions stable.
         * No queue/native-create frame is on this stack during these stores. */
        tsfp_guest_store_u32(outputs[0],result.handle);
        if (outputs[1]) tsfp_guest_store_u32(outputs[1],result.id);
    }
    return result.status;
}
/* Durable bookkeeping outlives a guest callback-frame fault. No native object
 * creation/join is performed here, and no lock crosses guest access. */
typedef struct EventOutputGuard { int active,old_cancel; uint32_t pin; } EventOutputGuard;
static _Thread_local EventOutputGuard event_guard;
static void event_cleanup(void *context) {
    EventOutputGuard *g=context;
    if (!g->active || !tsfp_heap_unpin_output(g->pin)) abort();
    g->active=0; g->pin=0;
    if (pthread_setcancelstate(g->old_cancel,NULL)) abort();
}
static int event_enter(void) {
    if (event_guard.active || !tsfp_guest_recovery_active() || pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&event_guard.old_cancel)) return 0;
    event_guard.active=1; event_guard.pin=0;
    if (!tsfp_guest_scope_guard(event_cleanup,&event_guard)) { event_cleanup(&event_guard); return 0; }
    return 1;
}
static void event_leave(void) {
    if (!tsfp_guest_scope_unguard(event_cleanup,&event_guard)) abort();
    event_cleanup(&event_guard);
}
static uint32_t pin_event_output(uint32_t address) {
    if (!tsfp_guest_range_valid(address,4) || !tsfp_guest_access_allowed(address,4,2)) {
        g_tsfp_access=TSFP_ACCESS_WRITE; tsfp_guest_memory_fault(address,4);
    }
    if (!tsfp_storage_event_output_allowed(address,4)) return TSFP_STATUS_INVALID_PARAMETER;
    return tsfp_heap_pin_output(address,4,&event_guard.pin);
}
static uint32_t event_attributes(uint32_t address) {
    if (!address) return 0;
    /* Pinned bridge: Xbox OBJECT_ATTRIBUTES is three 32-bit fields, not the
     * native host struct. Snapshot the whole readable descriptor before any
     * allocation/output mutation; names and directory/flag semantics are absent. */
    uint32_t fields[3]; tsfp_guest_read(address,fields,sizeof(fields));
    return (fields[0] || fields[1] || fields[2]) ? TSFP_STATUS_NOT_SUPPORTED : 0;
}
static int event_output(uint32_t address) {
    if (!tsfp_guest_range_valid(address,4) || !tsfp_guest_access_allowed(address,4,2)) {
        g_tsfp_access=TSFP_ACCESS_WRITE; tsfp_guest_memory_fault(address,4);
    }
    return tsfp_storage_kernel_output_allowed(address,4);
}
static void dispatch(uint32_t ordinal) {
    unsigned arguments;
    switch (ordinal) {
    case 184: arguments=5; break; /* NtAllocateVirtualMemory */
    case 199: arguments=3; break; /* NtFreeVirtualMemory */
    case 189: arguments=4; break; /* NtCreateEvent */
    case 225: arguments=2; break; /* NtSetEvent */
    case 186: arguments=1; break; /* NtClearEvent */
    case 255: arguments=10; break; /* PsCreateSystemThreadEx */
    case 233: arguments=3; break; /* NtWaitForSingleObject, non-alertable relative/infinite subset */
    case 187: arguments=1; break; /* NtClose, typed managed handles */
    case 104: arguments=0; break; /* KeGetCurrentThread */
    case 320: arguments=2; break; /* RtlZeroMemory */
    case 258: arguments=1; break; /* PsTerminateSystemThread */
    default: tsfp_guest_raise(TSFP_FAULT_IMPORT,THUNK_BASE,0,ordinal);
    }
    uint32_t stack=g_esp, frame[11];
    tsfp_guest_read(stack,frame,(arguments+1)*4); /* all args before side effects */
    if (ordinal==255) {
        if (frame[2] || frame[8] || frame[9] || !tsfp_thread_service_call_enter()) {
            g_eax=TSFP_STATUS_NOT_SUPPORTED; g_esp=stack+44; return;
        }
        g_eax=create_thread(frame); g_esp=stack+44;
        tsfp_thread_service_call_leave(); return;
    }
    if ((ordinal==189 || ordinal==225) && !event_enter()) {
        g_eax=UINT32_C(0xc0000001); g_esp=stack+(arguments+1)*4; return;
    }
    if (ordinal==258) tsfp_guest_thread_exit(frame[1]);
    uint32_t result=0;
    if (ordinal==104) result=tsfp_current_thread_object();
    else if (ordinal==187) result=tsfp_handle_close(frame[1]);
    else if (ordinal==184 || ordinal==199) {
        uint32_t base_ptr=frame[1],size_ptr=frame[ordinal==184 ? 3 : 2];
        if (!event_output(base_ptr) || !event_output(size_ptr) || tsfp_tls_ranges_overlap(base_ptr,4,size_ptr,4))
            result=TSFP_STATUS_INVALID_PARAMETER;
        else {
            uint32_t base=tsfp_guest_load_u32(base_ptr),size=tsfp_guest_load_u32(size_ptr);
            result=ordinal==184 ? tsfp_heap_allocate(&base,&size,frame[2],frame[4],frame[5]) : tsfp_heap_free(&base,&size,frame[3]);
            if (!result) { tsfp_guest_store_u32(base_ptr,base); tsfp_guest_store_u32(size_ptr,size); }
        }
    }
    else if (ordinal==189) {
        result=event_attributes(frame[2]);
        if (!result) {
            if (frame[3]>1) result=TSFP_STATUS_INVALID_PARAMETER;
            else if ((result=pin_event_output(frame[1]))==0) {
                uint32_t handle; result=tsfp_event_create(frame[3],(uint8_t)frame[4]!=0,&handle);
                if (!result) tsfp_guest_store_u32(frame[1],handle);
            }
        }
    }
    else if (ordinal==225) {
        result=frame[2] ? pin_event_output(frame[2]) : 0;
        if (!result) {
            uint32_t previous; result=tsfp_event_set(frame[1],&previous);
            if (!result && frame[2]) tsfp_guest_store_u32(frame[2],previous);
        }
    }
    else if (ordinal==186) result=tsfp_event_clear(frame[1]);
    else if (ordinal==233) {
        TsfpWaitTimeout timeout;
        if (frame[2]) tsfp_guest_raise(TSFP_FAULT_IMPORT,THUNK_BASE,0,ordinal);
        uint64_t bits=frame[3] ? tsfp_guest_load_u64(frame[3]) : 0;
        if (!tsfp_thread_timeout_decode(frame[3]!=0,bits,&timeout)) tsfp_guest_raise(TSFP_FAULT_IMPORT,THUNK_BASE,0,ordinal);
        result=tsfp_handle_wait(frame[1],&timeout);
    }
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
    if (ordinal==189 || ordinal==225) event_leave();
}
static void allocate_memory(void) { dispatch(184); }
static void free_memory(void) { dispatch(199); }
static void create_event(void) { dispatch(189); }
static void set_event(void) { dispatch(225); }
static void clear_event(void) { dispatch(186); }
static void create_system_thread(void) { dispatch(255); }
static void wait_object(void) { dispatch(233); }
static void close_handle(void) { dispatch(187); }
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
    case 184: return allocate_memory;
    case 199: return free_memory;
    case 189: return create_event;
    case 225: return set_event;
    case 186: return clear_event;
    case 255: return create_system_thread;
    case 233: return wait_object;
    case 187: return close_handle;
    case 104: return current_thread;
    case 320: return zero_memory;
    case 258: return terminate_thread;
    default: return unsupported_slots[slot];
    }
}
