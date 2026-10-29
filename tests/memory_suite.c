/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "memory.h"
#include "recomp_types.h"
#include "pc_memory_oracle.h"
#include "memory_test_support.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
void fixture_memory(void);
void test_generated_memory(void);
void test_strings(void);
void test_atomics(void);
void test_execution(void);
void test_kernel(void);
void test_image(void);
typedef struct InvalidBind { void *ram; size_t ram_size; void *contiguous; size_t contiguous_size; } InvalidBind;
static void invalid_bind(void *context) {
    InvalidBind *bind = context;
    tsfp_guest_bind_xbox(bind->ram, bind->ram_size, bind->contiguous, bind->contiguous_size);
}
typedef struct InvalidTransfer { uint32_t address; void *host; size_t width; int write; } InvalidTransfer;
static void invalid_transfer(void *context) {
    InvalidTransfer *access = context;
    if (access->write) tsfp_guest_write(access->address, access->host, access->width);
    else tsfp_guest_read(access->address, access->host, access->width);
}

#ifdef TSFP_PORTABLE_GUEST_MEMORY
static void denied_translated_read(void *context) {
    g_ecx = *(uint32_t *)context;
    g_edx = 0x20100;
    g_esp = 0x1f000;
    PUSH32(g_esp, 0x10ffe);
    fixture_memory();
}
#endif
static void translated_copy(uint32_t source, uint32_t destination, uint32_t value) {
    MEM32(source) = value;
    g_ecx = source; g_edx = destination;
    g_esp = 0x1f000;
    PUSH32(g_esp, 0x10ffe);
    fixture_memory();
    assert(g_eax == value + 1u && MEM32(destination) == value + 1u && g_esp == 0x1f000);
}
static void classify(uint32_t address, TsfpGuestRegion region, uint32_t offset, uint32_t available) {
    TsfpGuestSpan span = tsfp_guest_classify(address);
    assert(span.region == region && span.offset == offset && span.available == available);
}
/* Portable split-safe scalar accessors are checked against native PC MMAP
 * aliases when compiled as the pristine oracle. All test data is synthetic. */
static void rejected_scalar_store(void *context) {
    tsfp_guest_store_u32(*(const uint32_t *)context, UINT32_C(0x13572468));
}
static void rejected_scalar_load(void *context) {
    (void)tsfp_guest_load_u64(*(const uint32_t *)context);
}
static void test_scalar_wraps(void) {
    for (unsigned view = 0; view + 1 < TSFP_XBOX_RAM_VIEWS; ++view) {
        const uint32_t end = (view + 1u) * TSFP_XBOX_RAM_SIZE;
        for (unsigned left = 1; left < sizeof(uint64_t); ++left) {
            const uint32_t address = end - left;
            const uint64_t value = UINT64_C(0xfedcba9876543210) ^
                                   ((uint64_t)view << 8) ^ (uint64_t)left;
            unsigned char bytes[8];
            tsfp_guest_store_u64(address, value);
            assert(tsfp_guest_load_u64(address) == value);
            tsfp_guest_read(address, bytes, sizeof(bytes));
            for (unsigned i = 0; i < sizeof(bytes); ++i)
                assert(bytes[i] == (unsigned char)(value >> (8u * i)));
#ifdef TSFP_PC_MAPPED_ORACLE
            /* The unchanged PC header reads the next shared view directly. */
            assert(MEM64(address) == value);
            MEM64(address) = value ^ UINT64_C(0x55aa55aa55aa55aa);
            assert(tsfp_guest_load_u64(address) ==
                   (value ^ UINT64_C(0x55aa55aa55aa55aa)));
#endif
        }
        tsfp_guest_store_u16(end - 1u, UINT16_C(0xa123));
        assert(tsfp_guest_load_u16(end - 1u) == UINT16_C(0xa123));
        tsfp_guest_store_u32(end - 3u, UINT32_C(0x81234567));
        assert(tsfp_guest_load_u32(end - 3u) == UINT32_C(0x81234567));
        tsfp_guest_store_s16(end - 1u, -1234);
        assert(tsfp_guest_load_s16(end - 1u) == -1234);
        tsfp_guest_store_s32(end - 3u, -1234567);
        assert(tsfp_guest_load_s32(end - 3u) == -1234567);
        tsfp_guest_store_s64(end - 7u, -INT64_C(0x123456789abcdef));
        assert(tsfp_guest_load_s64(end - 7u) == -INT64_C(0x123456789abcdef));
        /* Inspect raw float bits: NaN payload and signed zero must survive. */
        const uint32_t float_bits = UINT32_C(0x7fc12345);
        const uint64_t double_bits = UINT64_C(0x8000000000000000);
        float float_value, float_result;
        double double_value, double_result;
        uint32_t float_after;
        uint64_t double_after;
        memcpy(&float_value, &float_bits, sizeof(float_value));
        memcpy(&double_value, &double_bits, sizeof(double_value));
        tsfp_guest_store_f32(end - 2u, float_value);
        float_result = tsfp_guest_load_f32(end - 2u);
        memcpy(&float_after, &float_result, sizeof(float_after));
        assert(float_after == float_bits);
        tsfp_guest_store_f64(end - 5u, double_value);
        double_result = tsfp_guest_load_f64(end - 5u);
        memcpy(&double_after, &double_result, sizeof(double_after));
        assert(double_after == double_bits);
    }
    /* The final RAM alias has no next mapped guest view. Fault before write. */
    const uint32_t final = TSFP_XBOX_RAM_SIZE * TSFP_XBOX_RAM_VIEWS - 1u;
    tsfp_guest_store_u16(final - 1u, UINT16_C(0x7777));
    unsigned char previous[2];
    tsfp_guest_read(final - 1u, previous, sizeof(previous));
    test_expect_fault(rejected_scalar_store, (void *)&final, final, 4);
    unsigned char after[2];
    tsfp_guest_read(final - 1u, after, sizeof(after));
    assert(memcmp(after, previous, sizeof(previous)) == 0);

    /* 32-bit VA rollover, MMIO, and unbacked windows never alias RAM. */
    static const uint32_t invalid[] = {
        UINT32_C(0xffffffff), UINT32_C(0xfd000000),
        UINT32_C(0xfe000000), UINT32_C(0xfe800000),
        UINT32_C(0xff000000), UINT32_C(0x83fffffe),
        UINT32_C(0xf3fffffe)
    };
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        uint32_t address = invalid[i];
        test_expect_fault(rejected_scalar_store, &address, address, 4);
        test_expect_fault(rejected_scalar_load, &address, address, 8);
    }
    /* Kernel-data changes classification but not contiguous backing. */
    tsfp_guest_store_u64(UINT32_C(0x8000fffd), UINT64_C(0xf0e1d2c3b4a59687));
    assert(tsfp_guest_load_u64(UINT32_C(0xf000fffd)) ==
           UINT64_C(0xf0e1d2c3b4a59687));
}

void test_memory_map(void) {
    void *ram, *contiguous;
#ifdef TSFP_PC_MAPPED_ORACLE
    uintptr_t oracle_base;
    pc_memory_oracle_open(&ram, &contiguous, &oracle_base);
    g_xbox_mem_offset = (ptrdiff_t)oracle_base;
#else
    ram = calloc(1, TSFP_XBOX_RAM_SIZE);
    contiguous = calloc(1, TSFP_XBOX_RAM_SIZE);
    assert(ram && contiguous);
#endif
    tsfp_guest_bind_xbox(ram, TSFP_XBOX_RAM_SIZE, contiguous, TSFP_XBOX_RAM_SIZE);
    classify(0, TSFP_REGION_RAM, 0, TSFP_XBOX_RAM_SIZE);
    classify(0x04000000, TSFP_REGION_RAM_ALIAS, 0, TSFP_XBOX_RAM_SIZE);
    classify(0x73ffffff, TSFP_REGION_RAM_ALIAS, 0x03ffffff, 1);
    classify(0x74000000, TSFP_REGION_INVALID, 0, 0);
    classify(0x80010000, TSFP_REGION_KERNEL_DATA, 0x10000, 0x1000);
    classify(0xf0000000, TSFP_REGION_TILED, 0, TSFP_XBOX_RAM_SIZE);
    classify(0xfd000000, TSFP_REGION_NV2A, 0, 0x1000000);
    classify(0xfe000000, TSFP_REGION_KERNEL_THUNK, 0, 0x800000);
    classify(0xfe800000, TSFP_REGION_MCPX, 0, 0x800000);
    classify(0xffffffff, TSFP_REGION_FLASH, 0xffffff, 1);
    classify(0x03ffffff, TSFP_REGION_RAM, 0x03ffffff, 1);
    classify(0x7fffffff, TSFP_REGION_INVALID, 0, 0);
    classify(0x80000000, TSFP_REGION_CONTIGUOUS, 0, 0x10000);
    classify(0x8000ffff, TSFP_REGION_CONTIGUOUS, 0xffff, 1);
    classify(0x80010fff, TSFP_REGION_KERNEL_DATA, 0x10fff, 1);
    classify(0x80011000, TSFP_REGION_CONTIGUOUS, 0x11000, 0x03fef000);
    classify(0x83ffffff, TSFP_REGION_CONTIGUOUS, 0x03ffffff, 1);
    classify(0x84000000, TSFP_REGION_INVALID, 0, 0);
    classify(0xefffffff, TSFP_REGION_INVALID, 0, 0);
    classify(0xf3ffffff, TSFP_REGION_TILED, 0x03ffffff, 1);
    classify(0xf4000000, TSFP_REGION_INVALID, 0, 0);
    classify(0xfcffffff, TSFP_REGION_INVALID, 0, 0);
    classify(0xfdffffff, TSFP_REGION_NV2A, 0xffffff, 1);
    classify(0xfe7fffff, TSFP_REGION_KERNEL_THUNK, 0x7fffff, 1);
    classify(0xfeffffff, TSFP_REGION_MCPX, 0x7fffff, 1);
    classify(0xff000000, TSFP_REGION_FLASH, 0, 0x1000000);
    for (unsigned view = 0; view < TSFP_XBOX_RAM_VIEWS; ++view) {
        uint32_t alias = view * TSFP_XBOX_RAM_SIZE;
        translated_copy(alias + 0x20000, 0x20004, 0x12340000u + view);
        assert(MEM32(alias + 0x20004) == 0x12340001u + view);
        translated_copy(0x20000, alias + 0x20008, 0x56780000u + view);
        assert(MEM32(0x20008) == 0x56780001u + view);
        translated_copy(alias + TSFP_XBOX_RAM_SIZE - 4, alias, view);
        assert(MEM32(0) == view + 1u);
    }
    MEM32(0x20000) = 0x11111111;
    translated_copy(0x80020000, 0xf0020004, 0x22222222);
    assert(MEM32(0x80020004) == 0x22222223 && MEM32(0x20000) == 0x11111111);
    translated_copy(0xf0020000, 0x80020004, 0x33333333);
    assert(MEM32(0x20000) == 0x11111111);
    translated_copy(0x80010000, 0xf0010004, 0x44444444);
    assert(MEM32(0x80010004) == 0x44444445 && MEM32(0x10000) == 0);
    test_scalar_wraps();
    test_generated_memory();
    test_strings();
    test_atomics();
    test_execution();
    test_kernel();
    test_image();
    /* Byte transfers support RAM wrap; pointer lvalues must reject it on ARM. */
    static const unsigned char pattern[] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char actual[sizeof(pattern)] = {0};
    tsfp_guest_write(0x03fffffd, pattern, sizeof(pattern));
    tsfp_guest_read(0x07fffffd, actual, sizeof(actual));
    assert(memcmp(actual, pattern, sizeof(pattern)) == 0);
    for (unsigned i = 0; i < sizeof(pattern); ++i)
        assert(MEM8(0x03fffffd + i) == pattern[i]);
    /* Classification changes inside one contiguous backing do not break transfers. */
    tsfp_guest_write(0x8000ffff, pattern, sizeof(pattern));
    tsfp_guest_read(0xf000ffff, actual, sizeof(actual));
    assert(memcmp(actual, pattern, sizeof(pattern)) == 0);
    assert(tsfp_guest_pointer(0x8000ffff, sizeof(pattern)) == (unsigned char *)contiguous + 0xffff);
    /* Rejected rebinds preserve all previous backing pointers and bytes. */
    InvalidBind bindings[] = {
        {ram, TSFP_XBOX_RAM_SIZE - 1u, contiguous, TSFP_XBOX_RAM_SIZE},
        {NULL, TSFP_XBOX_RAM_SIZE, contiguous, TSFP_XBOX_RAM_SIZE},
        {ram, TSFP_XBOX_RAM_SIZE, ram, TSFP_XBOX_RAM_SIZE},
        {ram, TSFP_XBOX_RAM_SIZE, (unsigned char *)ram + 1, TSFP_XBOX_RAM_SIZE},
        {ram, TSFP_XBOX_RAM_SIZE, contiguous, 1},
        {ram, TSFP_XBOX_RAM_SIZE, NULL, TSFP_XBOX_RAM_SIZE}
    };
    for (unsigned i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        test_expect_fault(invalid_bind, &bindings[i], 0, bindings[i].ram_size);
        assert(tsfp_guest_pointer(0x20000, 4) == (unsigned char *)ram + 0x20000);
        assert(tsfp_guest_pointer(0x80020000, 4) == (unsigned char *)contiguous + 0x20000);
    }
    memset(actual, 0xa5, sizeof(actual));
    InvalidTransfer accesses[] = {
        {0x73ffffff, actual, 2, 0},
        {0xffffffff, actual, 2, 0},
        {0x20000, NULL, 1, 0},
        {0x20000, actual, 0, 1},
        {0x20000, ram, 4, 0},
        {0x20000, contiguous, 4, 1}
    };
    for (unsigned i = 0; i < sizeof(accesses) / sizeof(accesses[0]); ++i) {
        test_expect_fault(invalid_transfer, &accesses[i], accesses[i].address, accesses[i].width);
        for (unsigned byte = 0; byte < sizeof(actual); ++byte) assert(actual[byte] == 0xa5);
    }
    /* Current typed-pointer lowering rejects logical wraps rather than overrun. */
    test_expect_memory_fault(0x03ffffff, 2, 0);
    test_expect_memory_fault(0x07ffffff, 2, 0);
    /* A read/write crossing the final mapped view is rejected before mutation. */
    MEM8(0x73ffffff) = 99;
    test_expect_memory_fault(0x73ffffff, 2, 1);
    assert(MEM8(0x73ffffff) == 99);
    static const uint32_t denied[] = {0x74000000, 0x7fffffff, 0x84000000,
        0xefffffff, 0xf4000000, 0xfcffffff, 0xfd000000, 0xfdffffff,
        0xfe000000, 0xfe7fffff, 0xfe800000, 0xfeffffff, 0xff000000, 0xffffffff};
    for (unsigned i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i)
        test_expect_memory_fault(denied[i], 1, 0);
    test_expect_memory_fault(0xffffffff, 2, 1);
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    /* Real translated instructions must fault, not silently fold MMIO to RAM. */
    MEM32(0x20100) = 0xdeadbeef;
    uint32_t translated_denied[] = {0xfd000000, 0xfe000000, 0xfe800000,
                                   0xff000000, 0x74000000, 0x73fffffe};
    for (unsigned i = 0; i < sizeof(translated_denied) / sizeof(translated_denied[0]); ++i) {
        test_expect_fault(denied_translated_read, &translated_denied[i], translated_denied[i], 4);
        assert(MEM32(0x20100) == 0xdeadbeef);
    }
#endif
    /* Contiguous and tiled pointer accesses remain bounded at their ends. */
    test_expect_memory_fault(0x83ffffff, 2, 0);
    test_expect_memory_fault(0xf3ffffff, 2, 0);
    tsfp_guest_unbind();
    test_expect_memory_fault(0x20000, 4, 0);
    tsfp_guest_bind_xbox(ram, TSFP_XBOX_RAM_SIZE, NULL, 0);
    test_expect_memory_fault(0x80000000, 4, 0);
    test_expect_memory_fault(0xf0000000, 4, 0);
    assert(tsfp_guest_pointer(0x20000, 4) == (unsigned char *)ram + 0x20000);
    tsfp_guest_unbind();
#ifdef TSFP_PC_MAPPED_ORACLE
    pc_memory_oracle_close();
#else
    free(contiguous);
    free(ram);
#endif
}
