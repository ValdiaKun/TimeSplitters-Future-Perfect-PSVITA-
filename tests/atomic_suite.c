/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "recomp_types.h"
#include "memory.h"
#include "string_ops.h"
#include "memory_test_support.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
void fixture_xadd_flags(void); void fixture_cmpxchg_flags(void);
void fixture_xadd_raw(void); void fixture_cmpxchg_raw(void); void fixture_xadd_alias(void);
void fixture_xadd_carry(void); void fixture_atomic_string(void); void fixture_cmpxchg_branch(void);
#define ARITHMETIC 0xcd5u
#ifdef TSFP_PC_MAPPED_ORACLE
static void native_atomic(unsigned compare, uint32_t address, uint32_t *accumulator,
                           uint32_t *source, uint32_t *flags) {
    uintptr_t native_flags=*flags;
    uint32_t *memory=(uint32_t *)((uintptr_t)address+g_xbox_mem_offset);
    uint32_t a=*accumulator,b=*source;
#define NATIVE(op) __asm__ volatile("pushq %[input]\n\tpopfq\n\t" op "\n\tpushfq\n\tpopq %[output]\n\tcld" \
    : "+a"(a), "+b"(b), [output] "=r"(native_flags), "+m"(*memory) \
    : [input] "r"(native_flags), [ptr] "r"(memory) : "memory", "cc")
    if (compare) { NATIVE("lock cmpxchgl %%ebx, (%[ptr])"); }
    else { NATIVE("lock xaddl %%ebx, (%[ptr])"); }
#undef NATIVE
    *accumulator=a; *source=b; *flags=(uint32_t)native_flags;
}
#endif
static void enter(uint32_t address, uint32_t a, uint32_t b, uint32_t flags) {
    g_ecx=address; g_eax=a; g_ebx=b; g_edx=0xbadca00;
    g_df=!!(flags & 0x400u); g_tsfp_guest_flags=flags;
    g_esp=0x1f000; PUSH32(g_esp,0x10ffe);
}
#ifdef TSFP_PORTABLE_GUEST_MEMORY
static void execute(void *context) { (*(recomp_func_t *)context)(); }
#endif
static void *worker(void *context) {
    uintptr_t id=(uintptr_t)context;
    for (unsigned i=0;i<1000;++i) {
        g_ecx=id ? 0x04060000 : 0x60000; g_ebx=1; g_eax=0;
        g_esp=id ? 0x62000 : 0x61000; PUSH32(g_esp,0x10ffe);
        fixture_xadd_raw();
    }
    return NULL;
}
void test_atomics(void) {
    const uint32_t values[]={0,1,0xffffffff,0x7fffffff,0x80000000,0x80000001,0xf};
    unsigned cases=0;
    for (unsigned operation=0;operation<2;++operation)
    for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)
    for (unsigned j=0;j<sizeof(values)/sizeof(values[0]);++j)
    for (unsigned direction=0;direction<2;++direction)
    for (unsigned match=0;match<(operation ? 2u : 1u);++match) {
        uint32_t old=values[i], source=values[j], initial=0x893u|(direction ? 0x400u : 0);
        uint32_t a=operation ? (match ? old : (source==old ? source^1u : source)) : 0xdeadbeef, b=source;
        uint32_t initial_a=a;
        uint32_t expected_memory=operation ? (a==old ? source : old) : old+source;
        /* Independent arithmetic expectations; hardware verifies all affected flags. */
        uint32_t result=operation ? a-old : old+source;
        uint32_t left=operation ? a : old, right=operation ? old : source;
        unsigned parity=0; for (unsigned bit=0;bit<8;++bit) parity+=(result>>bit)&1u;
        uint64_t wide=(uint64_t)old+source;
        uint32_t expected_flags=(initial&~0x8d5u) | (operation ? a<old : wide>UINT32_MAX) |
            (parity%2==0 ? 4u : 0u) | ((left^right^result)&0x10u) | (!result ? 64u : 0u) |
            (result&0x80000000u ? 128u : 0u) |
            ((operation ? (left^right)&(left^result) : ~(left^right)&(left^result)) & 0x80000000u ? 2048u : 0u);
        uint32_t expected_a=operation && a!=old ? old : a;
        uint32_t expected_b=operation ? source : old;
#ifdef TSFP_PC_MAPPED_ORACLE
        tsfp_guest_store_u32(0x60000,old);
        uint32_t native_flags=initial;
        native_atomic(operation,0x04060000,&a,&b,&native_flags);
        assert(a==expected_a && b==expected_b && (native_flags&ARITHMETIC)==(expected_flags&ARITHMETIC));
        assert(tsfp_guest_load_u32(0x60000)==expected_memory);
#endif
        tsfp_guest_store_u32(0x60000,old);
        enter(0x04060000,initial_a,source,initial);
        if (operation) fixture_cmpxchg_flags(); else fixture_xadd_flags();
        assert(g_eax==((expected_a&~0xff00u)|(((expected_flags&0xd5u)|2u)<<8)));
        assert(g_ebx==expected_b && (g_edx&255u)==!!(expected_flags&0x800u));
        assert((g_tsfp_guest_flags&ARITHMETIC)==(expected_flags&ARITHMETIC));
        assert(g_ecx==0x04060000 && g_esp==0x1f000 && g_df==(int)direction);
        assert(tsfp_guest_load_u32(0x60000)==expected_memory);
        ++cases;
    }
    tsfp_guest_store_u32(0x60000,0x12345678);
    enter(0,0x60000,0,2); fixture_xadd_alias();
    assert(g_eax==0x12345678 && tsfp_guest_load_u32(0x60000)==0x123a5678);
    tsfp_guest_store_u32(0x60000,0xffffffff);
    enter(0x60000,0,1,2); g_edx=7; fixture_xadd_carry(); assert(g_edx==8);
    /* Atomic flags remain live across a zero-count REP comparison. */
    tsfp_guest_store_u32(0x60000,0xffffffff);
    enter(0x60000,0,1,2); /* address is ECX, so use a zero-count alias address. */
    g_ecx=0; tsfp_guest_store_u32(0,0xffffffff); fixture_atomic_string();
    assert((g_eax&0xff00u)==0x5700 && (g_edx&255u)==0);
    for (unsigned match=0;match<2;++match) {
        tsfp_guest_store_u32(0x60000,19);
        enter(0x60000,match ? 19 : 18,29,2); fixture_cmpxchg_branch();
        assert(g_edx==(match ? 1u : 2u));
    }
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    const uint32_t bad[]={0xfd000000,0xfe000000,0xfe800000,0xff000000,0x74000000,0x03ffffff,0x60001};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) for (unsigned operation=0;operation<2;++operation) {
        enter(bad[i],0x12345678,0x87654321,0xc93);
        recomp_func_t function=operation ? fixture_cmpxchg_raw : fixture_xadd_raw;
        test_expect_fault(execute,&function,bad[i],4);
        assert(g_eax==0x12345678 && g_ebx==0x87654321 && g_ecx==bad[i]);
        assert(g_esp==0x1effc && g_tsfp_guest_flags==0xc93);
    }
#endif
    tsfp_guest_store_u32(0x60000,0);
    g_tsfp_guest_flags=0xc93;
    pthread_t first,second;
    assert(pthread_create(&first,NULL,worker,(void *)(uintptr_t)0)==0);
    assert(pthread_create(&second,NULL,worker,(void *)(uintptr_t)1)==0);
    assert(pthread_join(first,NULL)==0 && pthread_join(second,NULL)==0);
    assert(tsfp_guest_load_u32(0x60000)==2000 && g_tsfp_guest_flags==0xc93);
    printf("atomic-fixtures-v1: cases=%u flags=pass alias=pass fault=pass threads=pass\n",cases);
}
