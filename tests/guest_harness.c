/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic fixture support only; no Xbox HLE or title initialization. */
#ifdef NDEBUG
#undef NDEBUG /* Acceptance checks must also execute in Release VPKs. */
#endif
#include "recomp_types.h"
#include "memory.h"
#include "memory_test_support.h"
#ifdef __vita__
/* Two 64 MiB test backings plus allocator/thread overhead; device use unverified. */
unsigned int _newlib_heap_size_user = 160u * 1024u * 1024u;
#endif
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

ptrdiff_t g_xbox_mem_offset;
int g_force_return;
uint32_t g_xbox_code_lo = 0x10000, g_xbox_code_hi = 0x14000;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp, g_fs_base;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_df, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS uint32_t g_icall_saved_esp, g_icall_dispatch_form;
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_trace_idx;
volatile uint64_t g_icall_count;
static unsigned char storage[0x10000] __attribute__((aligned(16)));
static jmp_buf fault_jump;
static int expected_fault;
static unsigned divide_reason = UINT32_MAX;
static uint32_t fault_address;
static size_t fault_width;
static unsigned unresolved;
static uint32_t shared_counter __attribute__((aligned(4)));

void fixture_add(void); void fixture_carry(void); void fixture_callee(void);
void fixture_call(void); void fixture_indirect(void); void fixture_unaligned(void);
void test_memory_map(void);
void fixture_sse(void); void fixture_x87(void); void fixture_div(void);
_Noreturn void tsfp_guest_memory_fault(uint32_t address, size_t width) {
    fault_address = address; fault_width = width;
    if (expected_fault) longjmp(fault_jump, 1);
    fprintf(stderr, "event=guest_memory_fault address=%08x width=%u\n", address, (unsigned)width);
    abort();
}
void tsfp_guest_divide_error(uint32_t site, unsigned reason) {
    (void)site; divide_reason = reason;
    if (expected_fault) longjmp(fault_jump, 1);
    abort();
}
recomp_func_t recomp_lookup(uint32_t address) {
    return address == 0x10200 ? fixture_callee : NULL;
}
recomp_func_t recomp_lookup_manual(uint32_t address) { (void)address; return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t address) { (void)address; return NULL; }
void recomp_icall_fail_log(uint32_t address) { (void)address; ++unresolved; }
void recomp_icall_not_code_log(uint32_t address, uint32_t stack) {
    (void)address; (void)stack; ++unresolved;
}
void test_expect_fault(void (*operation)(void *), void *context,
                       uint32_t address, size_t width) {
    expected_fault = 1;
    if (!setjmp(fault_jump)) {
        operation(context);
        assert(!"invalid memory operation accepted");
    }
    assert(fault_address == address && fault_width == width);
    expected_fault = 0;
}
typedef struct TestAccess { uint32_t address; size_t width; int write; } TestAccess;
static void invalid_access(void *context) {
    TestAccess *access = context;
    unsigned char buffer[8] = {0};
    assert(access->width <= sizeof(buffer));
    if (access->write) tsfp_guest_write(access->address, buffer, access->width);
    else (void)tsfp_guest_pointer(access->address, access->width);
}
void test_expect_memory_fault(uint32_t address, size_t width, int write) {
    TestAccess access = {address, width, write};
    test_expect_fault(invalid_access, &access, address, width);
}
static void enter(void) {
    g_esp = 0x1f000;
    PUSH32(g_esp, 0x10ffe);
}
static void *tls_worker(void *unused) {
    (void)unused;
    assert(g_eax == 0);
    g_eax = 0xfeed1234;
    assert(g_eax == 0xfeed1234);
    for (unsigned i = 0; i < 10000; ++i) RECOMP_ATOMIC_ADD32(&shared_counter, 1);
    return NULL;
}
int main(void) {
#ifdef TSFP_REQUIRE_ARMV7
    _Static_assert(sizeof(void *) == 4 && sizeof(uintptr_t) == 4, "ARM32 ABI required");
#ifndef __arm__
#error ARM instruction target required
#endif
#endif
    tsfp_guest_bind(storage, sizeof(storage), 0x10000);
    /* Unsigned arithmetic avoids ptrdiff_t signed overflow on ARM32. */
    g_xbox_mem_offset = (ptrdiff_t)((uintptr_t)storage - (uintptr_t)0x10000u);
    enter(); MEM32(g_esp + 4) = UINT32_MAX; fixture_add();
    assert(g_eax == 0 && g_esp == 0x1f000);
    enter(); fixture_carry(); assert(g_eax == 1 && g_esp == 0x1f000);
    enter(); g_ebx = 0x12345678; fixture_call();
    assert(g_eax == 48 && g_ebx == 0x12345678 && g_esp == 0x1f000);
    assert(MEM32(0x1eff8) == 0x1030a); /* real guest call return VA */
    enter(); fixture_indirect();
    assert(g_eax == 48 && g_esp == 0x1f000 && g_icall_count == 1);
    /* Match upstream missing-target behavior, but count the unresolved event. */
    enter(); { uint32_t eax = 77; RECOMP_ICALL(0x10f00); assert(eax == 0); }
    assert(unresolved == 1 && g_esp == 0x1f000);
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    enter(); MEM32(0x18001) = 0x12345678; fixture_unaligned();
    assert(g_eax == 0x12345679 && MEM32(0x18001) == 0x12345679);
    MEM64(0x18003) = UINT64_C(0x123456789abcdef0);
    assert(MEM64(0x18003) == UINT64_C(0x123456789abcdef0));
    MEMD(0x18005) = -3.5; assert(MEMD(0x18005) == -3.5);
    for (uint32_t offset = 0; offset < 16; ++offset) {
        uint32_t address = 0x18100 + offset;
        MEM16(address) = 0x1234; assert(MEM16(address) == 0x1234);
        MEM32(address) = 0x12345678; assert(MEM32(address) == 0x12345678);
        assert(MEM8(address) == 0x78 && MEM8(address + 3) == 0x12);
        MEM64(address) = UINT64_C(0x123456789abcdef0);
        assert(MEM64(address) == UINT64_C(0x123456789abcdef0));
        SMEM32(address) = -17; assert(SMEM32(address) == -17);
        MEMF(address) = -1.25f; assert(MEMF(address) == -1.25f);
        MEMD(address) = -3.5; assert(MEMD(address) == -3.5);
    }
#endif
    enter(); g_ecx = 0x18020; g_edx = 0x18040;
    for (unsigned i = 0; i < 4; ++i) MEMF(g_ecx + i * 4) = (float)i + 0.5f;
    fixture_sse();
    for (unsigned i = 0; i < 4; ++i) assert(MEMF(0x18040 + i * 4) == (float)i * 2 + 1);
    for (unsigned mode = 0; mode < 4; ++mode) {
        static const double want[] = {2, 2, 3, 2};
        enter(); g_ecx = 0x18020; g_edx = 0x18040;
        g_fp_top = 0; g_fp_control_word = (uint16_t)(0x37f | mode << 10);
        MEMD(g_ecx) = 2.5; fixture_x87();
        assert(MEMD(0x18040) == want[mode] && g_fp_top == 0);
    }
    assert(RECOMP_FCMP(NAN, 1.0) == 2 && signbit(recomp_frndint(-0.5, 0x37f)));
    enter(); g_ecx = 3; MEM32(g_esp + 4) = 10; fixture_div();
    assert(g_eax == 3 && g_edx == 1);
    expected_fault = 1;
    if (!setjmp(fault_jump)) {
        enter(); g_ecx = 0; MEM32(g_esp + 4) = 10; fixture_div();
        assert(!"divide by zero did not fault");
    }
    assert(divide_reason == 0);
    if (!setjmp(fault_jump)) {
        (void)tsfp_guest_pointer(0x1ffff, 4);
        assert(!"cross-boundary memory read accepted");
    }
    assert(fault_address == 0x1ffff && fault_width == 4);
    if (!setjmp(fault_jump)) {
        (void)tsfp_guest_pointer(UINT32_MAX, 8);
        assert(!"invalid guest address accepted");
    }
    if (!setjmp(fault_jump)) {
        tsfp_guest_bind(storage, SIZE_MAX, 0x10000);
        assert(!"overflowing region accepted");
    }
    assert(fault_width == SIZE_MAX);
    expected_fault = 0;
    assert(tsfp_guest_pointer(0x10000, 1) == storage);
    _Alignas(4) uint32_t atomic_value = 7;
    assert(RECOMP_ATOMIC_ADD32(&atomic_value, 3) == 7 && atomic_value == 10);
    assert(RECOMP_ATOMIC_CAS32(&atomic_value, 10, 20) == 10 && atomic_value == 20);
    assert(RECOMP_ATOMIC_CAS32(&atomic_value, 10, 30) == 20 && atomic_value == 20);
    pthread_t thread;
    g_eax = 0xabcdef01;
    assert(pthread_create(&thread, NULL, tls_worker, NULL) == 0);
    for (unsigned i = 0; i < 10000; ++i) RECOMP_ATOMIC_ADD32(&shared_counter, 1);
    assert(pthread_join(thread, NULL) == 0 && g_eax == 0xabcdef01);
    assert(shared_counter == 20000);
    test_memory_map();
    puts("guest-fixtures-v3: add=0 carry=1 call=48 indirect=48 sse=1,3,5,7 x87=2,2,3,2 div=3r1 faults=pass atomic=pass tls=pass memory-map=pass");
    return 0;
}
