/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef TSFP_PC_MAPPED_ORACLE
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#if !defined(__linux__) || !defined(__x86_64__)
#error Native string oracle requires Linux x86_64
#endif
#include "string_native_oracle.h"
#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stddef.h>
extern ptrdiff_t g_xbox_mem_offset;
static sigjmp_buf resume;
static StringState *result_state;
static volatile sig_atomic_t active;
static void fault_handler(int signal_number, siginfo_t *info, void *context) {
    __asm__ volatile("cld" ::: "cc");
    if (!active) _Exit(128 + signal_number);
    const greg_t *registers = ((ucontext_t *)context)->uc_mcontext.gregs;
    result_state->source = (uint32_t)((uintptr_t)registers[REG_RSI] - (uintptr_t)g_xbox_mem_offset);
    result_state->destination = (uint32_t)((uintptr_t)registers[REG_RDI] - (uintptr_t)g_xbox_mem_offset);
    result_state->count = (uint32_t)registers[REG_RCX];
    result_state->accumulator = (uint32_t)registers[REG_RAX];
    result_state->flags = (uint32_t)registers[REG_EFL];
    result_state->fault_address = (uint32_t)((uintptr_t)info->si_addr - (uintptr_t)g_xbox_mem_offset);
    result_state->fault = 1;
    active = 0;
    siglongjmp(resume, 1);
}
#define EXECUTE(text) __asm__ volatile( \
    "pushq %[input_flags]\n\tpopfq\n\t" text "\n\tpushfq\n\tpopq %[output_flags]\n\tcld" \
    : "+S"(source), "+D"(destination), "+c"(count), "+a"(accumulator), [output_flags] "=r"(flags) \
    : [input_flags] "r"(flags) : "memory", "cc")
#define RUN(base) do { \
    if (!repeat) { EXECUTE(base); } \
    else if (repeat == 2) { EXECUTE("repne " base); } \
    else { EXECUTE("repe " base); } \
} while (0)
static void native_string_instruction(unsigned kind, unsigned width, unsigned repeat, StringState *state) {
    struct sigaction handler = {0}, old;
    handler.sa_sigaction = fault_handler;
    handler.sa_flags = SA_SIGINFO;
    sigemptyset(&handler.sa_mask);
    assert(sigaction(SIGSEGV, &handler, &old) == 0);
    result_state = state;
    state->fault = 0;
    active = 1;
    if (!sigsetjmp(resume,1)) {
        uintptr_t source = (uintptr_t)g_xbox_mem_offset + state->source;
        uintptr_t destination = (uintptr_t)g_xbox_mem_offset + state->destination;
        uintptr_t count = state->count, accumulator = state->accumulator, flags = state->flags;
        switch (kind * 8u + width) {
        case 9: RUN("movsb"); break;
        case 10: RUN("movsw"); break;
        case 12: RUN("movsl"); break;
        case 17: RUN("stosb"); break;
        case 18: RUN("stosw"); break;
        case 20: RUN("stosl"); break;
        case 25: RUN("cmpsb"); break;
        case 26: RUN("cmpsw"); break;
        case 28: RUN("cmpsl"); break;
        case 33: RUN("scasb"); break;
        case 34: RUN("scasw"); break;
        case 36: RUN("scasl"); break;
        case 41: RUN("lodsb"); break;
        case 42: RUN("lodsw"); break;
        case 44: RUN("lodsl"); break;
        default: abort();
        }
        active = 0;
        state->source = (uint32_t)(source - (uintptr_t)g_xbox_mem_offset);
        state->destination = (uint32_t)(destination - (uintptr_t)g_xbox_mem_offset);
        state->count = (uint32_t)count;
        state->accumulator = (uint32_t)accumulator;
        state->flags = (uint32_t)flags;
    }
    assert(sigaction(SIGSEGV, &old, NULL) == 0);
}
/* Modern fast-string engines can copy ahead through distinct virtual aliases
 * of overlapping physical RAM. CI CPUs disagree on bulk REP results for these
 * synonyms. Use real single-element MOVS for cross-view references, preserving
 * guest element order; ordinary ranges still execute native REP. */
static uint32_t memory_view(uint32_t address) {
    if (address < 0x74000000u) return address / 0x04000000u;
    if (address >= 0x80000000u && address < 0x84000000u) return 32;
    if (address >= 0xf0000000u && address < 0xf4000000u) return 33;
    return 34;
}
void native_string_oracle(unsigned kind, unsigned width, unsigned repeat, StringState *state) {
    if (kind == 1 && repeat && memory_view(state->source) != memory_view(state->destination)) {
        while (state->count) {
            native_string_instruction(kind,width,0,state);
            if (state->fault) return;
            --state->count;
        }
        return;
    }
    native_string_instruction(kind,width,repeat,state);
}
#endif
