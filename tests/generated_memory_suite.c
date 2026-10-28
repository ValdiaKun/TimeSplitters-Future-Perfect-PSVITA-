/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "recomp_types.h"
#include "memory.h"
#include "memory_test_support.h"
#include <assert.h>
#include <string.h>
void fixture_sub(void); void fixture_rmw8(void);
void fixture_ss_register(void); void fixture_sd_register(void);
void fixture_sse(void); void fixture_mmx(void); void fixture_half(void); void fixture_s32(void);
void fixture_u16(void); void fixture_s16(void); void fixture_memory(void);
void fixture_f32(void); void fixture_f64(void); void fixture_i64(void);
void fixture_rmw(void); void fixture_rmw16(void); void fixture_not(void);
void fixture_xchg(void); void fixture_indirect_mem(void); void fixture_address(void);
void fixture_push16(void); void fixture_pop_mem(void); void fixture_pop16_mem(void); void fixture_pop_esp_mem(void);
void fixture_push(void); void fixture_pop(void); void fixture_atomic(void);
static void enter(uint32_t source, uint32_t destination) {
    g_ecx = source; g_edx = destination;
    g_esp = 0x1f000; PUSH32(g_esp, 0x10ffe);
}
#ifdef TSFP_PORTABLE_GUEST_MEMORY
static void execute(void *context) { (*(recomp_func_t *)context)(); }
static void expect_instruction_fault(recomp_func_t function, uint32_t address, unsigned width) {
    test_expect_fault(execute, &function, address, width);
}
/* Preserve readable bytes and architectural cells when an instruction faults. */
static void check_fault(recomp_func_t function, uint32_t address, unsigned width) {
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, esp = g_esp;
    expect_instruction_fault(function, address, width);
    assert(g_eax == eax && g_ecx == ecx && g_edx == edx && g_esp == esp);
}
#endif
void test_generated_memory(void) {
    for (unsigned view = 1; view < TSFP_XBOX_RAM_VIEWS; ++view) {
        uint32_t edge = view * TSFP_XBOX_RAM_SIZE;
        uint32_t src = edge - 1, dst = edge * 1u + 0x30001;
        enter(src, dst); tsfp_guest_store_u16(src, 0x8123); fixture_u16();
        assert(g_eax == 0x8123 && tsfp_guest_load_u16(dst) == 0x8123);
        enter(src, dst); fixture_s16();
        assert(g_eax == 0xffff8123 && tsfp_guest_load_s16(dst) == (int16_t)0x8123);
        for (unsigned split = 1; split < 4; ++split) {
            src = edge - split; dst = edge + TSFP_XBOX_RAM_SIZE - split;
            /* At the last edge choose an earlier destination mirror. */
            if (view == TSFP_XBOX_RAM_VIEWS - 1) dst -= TSFP_XBOX_RAM_SIZE;
            enter(src, dst); tsfp_guest_store_u32(src, 0x12345678); fixture_memory();
            assert(g_eax == 0x12345679 && tsfp_guest_load_u32(dst) == 0x12345679);
            enter(src, dst); tsfp_guest_store_u32(src, 0x7fc12345); fixture_f32();
            assert(tsfp_guest_load_u32(dst) == 0x7fc12345);
            enter(src, dst); tsfp_guest_store_u32(src, 0x80000000); fixture_f32();
            assert(tsfp_guest_load_u32(dst) == 0x80000000);
        }
        for (unsigned split = 1; split < 8; ++split) {
            src = edge - split; dst = edge + 0x30000;
            enter(src, dst); tsfp_guest_store_u64(src, UINT64_C(0x7ff8123456789abc));
            fixture_f64(); assert(tsfp_guest_load_u64(dst) == UINT64_C(0x7ff8123456789abc));
            enter(src, dst); tsfp_guest_store_u64(src, UINT64_C(0x8000000000000000));
            fixture_f64(); assert(tsfp_guest_load_u64(dst) == UINT64_C(0x8000000000000000));
            enter(src, dst); tsfp_guest_store_u64(src, UINT64_C(0xfedcba9876543210));
            fixture_mmx(); assert(tsfp_guest_load_u64(dst) == UINT64_C(0xfedcba9876543210));
            enter(src, dst); g_xmm0.q[1] = UINT64_C(0x123456789abcdef0);
            fixture_half(); assert(tsfp_guest_load_u64(dst) == UINT64_C(0xfedcba9876543210));
            assert(g_xmm0.q[1] == UINT64_C(0x123456789abcdef0));
            /* x87's inherited double model is only exact for this integer. */
            enter(src, dst); tsfp_guest_store_s64(src, -INT64_C(123456789));
            g_fp_top = 0; fixture_i64();
            assert(tsfp_guest_load_s64(dst) == -INT64_C(123456789) && g_fp_top == 0);
        }
        for (unsigned split = 1; split < 16; ++split) {
            enter(edge - split, edge + 0x50000);
            for (unsigned lane = 0; lane < 4; ++lane)
                tsfp_guest_store_f32(g_ecx + lane * 4u, (float)lane + 0.5f);
            fixture_sse();
            for (unsigned lane = 0; lane < 4; ++lane)
                assert(tsfp_guest_load_f32(g_edx + lane * 4u) == (float)lane * 2.0f + 1.0f);
        }
        static const uint32_t values[] = {0xffffffff, 0x7fffffff, 0x80000000, 0};
        for (unsigned i = 0; i < 4; ++i) {
            uint32_t value = values[i];
            enter(edge - 2, 0); g_eax = g_ebx = 0;
            tsfp_guest_store_u32(g_ecx, value); fixture_rmw();
            uint32_t result = value + 1;
            assert(tsfp_guest_load_u32(g_ecx) == result);
            assert((g_eax & 255) == (result == 0)); /* carry */
            assert((g_ebx & 255) == (result == 0)); /* zero */
            assert((g_edx & 255) == (value == 0x7fffffff)); /* overflow */
            assert(((g_edx >> 8) & 255) == (result >> 31));
        }
        for (unsigned i = 0; i < 4; ++i) {
            uint32_t value = values[i], result = value - 1u;
            enter(edge - 2, 0); g_eax = g_ebx = 0;
            tsfp_guest_store_u32(g_ecx, value); fixture_sub();
            assert(tsfp_guest_load_u32(g_ecx) == result);
            assert((g_eax & 255) == (value == 0));
            assert((g_ebx & 255) == (result == 0));
            assert((g_edx & 255) == (value == 0x80000000));
            assert(((g_edx >> 8) & 255) == (result >> 31));
        }
        enter(edge - 1, 0); g_eax = g_ebx = 0;
        tsfp_guest_store_u8(g_ecx, 0xff); fixture_rmw8();
        assert(tsfp_guest_load_u8(g_ecx) == 0 && (g_eax & 255) == 1 && (g_ebx & 255) == 1);
        enter(edge - 3, edge + 0x30000); tsfp_guest_store_s32(g_ecx, -1234567);
        g_fp_top = 0; fixture_s32(); assert(tsfp_guest_load_s32(g_edx) == -1234567);
        enter(edge - 1, 0); g_eax = g_ebx = 0;
        tsfp_guest_store_u16(g_ecx, 0xffff); fixture_rmw16();
        assert(tsfp_guest_load_u16(g_ecx) == 0 && (g_eax & 255) == 1 && (g_ebx & 255) == 1);
        enter(edge - 3, 0); tsfp_guest_store_u32(g_ecx, 0x12345678); fixture_not();
        assert(tsfp_guest_load_u32(g_ecx) == 0xedcba987);
        enter(edge - 1, 0); g_eax = 0x11223344;
        tsfp_guest_store_u32(g_ecx, 0x55667788); fixture_xchg();
        assert(g_eax == 0x55667788 && tsfp_guest_load_u32(g_ecx) == 0x11223344);
        enter(edge - 1, 0); g_eax = 41;
        tsfp_guest_store_u32(g_ecx, 0x10200); fixture_indirect_mem();
        assert(g_eax == 48 && g_esp == 0x1f000);
    }
    enter(0, 0); g_xmm0.q[0] = UINT64_C(0xaabbccdd11223344); g_xmm0.q[1] = 0x12345678;
    g_xmm1.q[0] = UINT64_C(0xfedcba987fc12345); fixture_ss_register();
    assert(g_xmm0.q[0] == UINT64_C(0xaabbccdd7fc12345) && g_xmm0.q[1] == 0x12345678);
    enter(0, 0); fixture_sd_register();
    assert(g_xmm0.q[0] == UINT64_C(0xfedcba987fc12345) && g_xmm0.q[1] == 0x12345678);
    /* Effective address arithmetic wraps modulo 32 bits before memory access. */
    enter(0xffffffff, 0x30000); g_eax = 0xfffffffa;
    tsfp_guest_store_u32(1, 0x87654321); fixture_address();
    assert(g_eax == 0x87654321 && tsfp_guest_load_u32(0x30000) == 0x87654321);
    /* PUSH/POP values straddle a real RAM backing boundary. */
    g_ecx = 0x30000; tsfp_guest_store_u32(g_ecx, 0x11223344);
    g_esp = TSFP_XBOX_RAM_SIZE + 2; fixture_push();
    assert(g_esp == TSFP_XBOX_RAM_SIZE + 2 && tsfp_guest_load_u32(TSFP_XBOX_RAM_SIZE - 2) == 0x11223344);
    g_esp = TSFP_XBOX_RAM_SIZE - 2; fixture_pop();
    assert(g_eax == 0x11223344 && g_esp == TSFP_XBOX_RAM_SIZE + 6);
    g_ecx = 0x30000; tsfp_guest_store_u16(g_ecx, 0x8123);
    g_esp = TSFP_XBOX_RAM_SIZE + 1; fixture_push16();
    assert(g_esp == TSFP_XBOX_RAM_SIZE + 3 && tsfp_guest_load_u16(TSFP_XBOX_RAM_SIZE - 1) == 0x8123);
    g_edx = TSFP_XBOX_RAM_SIZE - 1; g_esp = 0x30000;
    tsfp_guest_store_u32(g_esp, 0x12345678); fixture_pop_mem();
    assert(tsfp_guest_load_u32(g_edx) == 0x12345678 && g_esp == 0x30008);
    g_esp = 0x30000; fixture_pop16_mem();
    assert(tsfp_guest_load_u16(g_edx) == 0x5678 && g_esp == 0x30006);
    g_esp = 0x30000; fixture_pop_esp_mem();
    assert(tsfp_guest_load_u32(0x30004) == 0x12345678 && g_esp == 0x30008);
    enter(0x30000, 0); g_eax = 3; tsfp_guest_store_u32(g_ecx, 7); fixture_atomic();
    assert(g_eax == 7 && tsfp_guest_load_u32(g_ecx) == 10);
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    const uint32_t final = TSFP_XBOX_RAM_SIZE * TSFP_XBOX_RAM_VIEWS;
    static const uint32_t invalid[] = {0xfd000000,0xfe000000,0xfe800000,0xff000000,0xffffffff,0x74000000};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        enter(invalid[i], 0x30000); g_eax = 0xabcdef01;
        check_fault(fixture_memory, invalid[i], 4);
        enter(0x30000, invalid[i]);
        uint32_t loaded = tsfp_guest_load_u32(0x30000);
        expect_instruction_fault(fixture_f32, invalid[i], 4);
        /* The preceding load commits; the faulting store changes no memory. */
        assert(g_xmm0.u[0] == loaded && g_xmm0.q[1] == 0);
    }
    enter(0x30000, final - 2); tsfp_guest_store_u16(final - 2, 0x5555);
    expect_instruction_fault(fixture_f32, final - 2, 4);
    assert(tsfp_guest_load_u16(final - 2) == 0x5555);
    enter(0x30000, final - 8); tsfp_guest_store_u64(final - 8, UINT64_C(0x5555555555555555));
    expect_instruction_fault(fixture_sse, final - 8, 16);
    assert(tsfp_guest_load_u64(final - 8) == UINT64_C(0x5555555555555555));
    enter(0xfd000000, 0); check_fault(fixture_indirect_mem, 0xfd000000, 4);
    enter(0x30000, 0); g_esp = 0x74000000;
    check_fault(fixture_pop, 0x74000000, 4);
    enter(0x30000, 0); g_esp = 0xfd000004;
    check_fault(fixture_push, 0xfd000000, 4);
    enter(0x30000, 0xfd000000); check_fault(fixture_pop_mem, 0xfd000000, 4);
    enter(0x30000, 0xfd000000); check_fault(fixture_pop16_mem, 0xfd000000, 2);
    enter(0x30000, 0); g_esp = 0xfd000002; check_fault(fixture_push16, 0xfd000000, 2);
    enter(0x30001, 0); check_fault(fixture_atomic, 0x30001, 4);
#endif
}
