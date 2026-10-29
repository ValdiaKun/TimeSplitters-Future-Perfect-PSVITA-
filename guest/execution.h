/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_EXECUTION_H
#define TSFP_EXECUTION_H
#include <stdint.h>
#include <stddef.h>
/* Snapshot ABI is independent of the generated C register aliases. */
typedef struct TsfpCpuState {
    uint32_t registers[8]; /* EAX ECX EDX EBX ESP EBP ESI EDI */
    uint32_t pc, flags, flags_valid, fs_base, seh_ebp;
    unsigned char xmm[8][16], mmx[8][8];
    double fp[8];
    int fp_top, fp_cmp;
    uint16_t fp_control, fp_cc;
} TsfpCpuState;
enum TsfpFaultKind { TSFP_FAULT_MEMORY=1, TSFP_FAULT_DIVIDE, TSFP_FAULT_DISPATCH, TSFP_THREAD_EXIT, TSFP_FAULT_IMPORT };
enum TsfpAccess { TSFP_ACCESS_UNKNOWN, TSFP_ACCESS_READ, TSFP_ACCESS_WRITE, TSFP_ACCESS_ATOMIC };
typedef struct TsfpFault {
    enum TsfpFaultKind kind;
    enum TsfpAccess access;
    uint32_t address, detail, thread_id;
    size_t width;
    TsfpCpuState cpu;
} TsfpFault;
extern _Thread_local uint32_t g_tsfp_guest_pc;
extern _Thread_local enum TsfpAccess g_tsfp_access;
void tsfp_cpu_capture(TsfpCpuState *state);
/* C-only boundary. Callback must hold no resources requiring unwinding, no
 * locks, VLAs, cleanup attributes, C++ objects or foreign-language frames.
 * Returns 0 normally, 1 for a captured event. Does NOT resume Xbox SEH. */
int tsfp_guest_run(void (*callback)(void *), void *context, TsfpFault *fault);
int tsfp_guest_recovery_active(void);
_Noreturn void tsfp_guest_raise(enum TsfpFaultKind kind, uint32_t address, size_t width, uint32_t detail);
/* Initialize already-owned stack/TIB/TLS state on the CURRENT native thread.
 * Caller initializes/owns TIB data following the title's layout; this API does
 * not invent a TIB or spawn/schedule a native thread. Failure changes nothing. */
int tsfp_guest_thread_enter(uint32_t id, uint32_t stack_base, size_t stack_size,
                            uint32_t fs_base, size_t tls_size);
/* Minimal upstream-layout TIB + TLS template + zeroed thread data, bounded to
 * 64 KiB. Caller owns exclusive destination and stable memory bindings. No
 * thread object at fs:[0x28] or Xbox SEH dispatcher is supplied here. */
int tsfp_guest_tls_prepare(uint32_t destination, uint32_t template_address,
                           size_t template_size, size_t thread_data_size);
_Noreturn void tsfp_guest_thread_exit(uint32_t status);
int tsfp_guest_thread_leave(void);
uint32_t tsfp_guest_thread_id(void);
#endif
