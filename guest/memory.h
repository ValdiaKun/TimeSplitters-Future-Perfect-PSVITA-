/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_GUEST_MEMORY_H
#define TSFP_GUEST_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include "address_space.h"
/* Optional image access policy: mode READ=1, WRITE=2, EXECUTE=4.
 * Install/remove only with guest execution and bindings quiescent. */
typedef int (*TsfpMemoryPolicy)(uint32_t address, size_t width, unsigned mode);
int tsfp_guest_policy_active(void);
void tsfp_guest_set_policy(TsfpMemoryPolicy policy);
int tsfp_guest_access_allowed(uint32_t address, size_t width, unsigned mode);
void tsfp_guest_check_access(uint32_t address, size_t width, unsigned mode);
/* Retained bounded fixture mode; replaces any previous Xbox binding. */
void tsfp_guest_bind(void *storage, size_t size, uint32_t base);
/* Side-effect-free whole-range probe; binding must remain stable during use. */
int tsfp_guest_range_valid(uint32_t address, size_t width);
void *tsfp_guest_pointer(uint32_t address, size_t width);
/* Split-safe scalar accesses for generated guest loads/stores. These preserve
 * little-endian bit patterns across a 64 MiB RAM mirror boundary, unlike a
 * borrowed lvalue pointer. Accesses are validated in full before mutation.
 * These are non-atomic; LOCK operations need separate implementation.
 * tools/guest_codegen.py connects generated memory expressions to these
 * helpers. Raw MEM lvalues remain available for bounded native adapters. */
void *tsfp_guest_atomic_pointer(uint32_t address);
uint8_t tsfp_guest_load_u8(uint32_t address);
int8_t tsfp_guest_load_s8(uint32_t address);
void tsfp_guest_store_u8(uint32_t address, uint8_t value);
void tsfp_guest_store_s8(uint32_t address, int8_t value);
uint16_t tsfp_guest_load_u16(uint32_t address);
uint32_t tsfp_guest_load_u32(uint32_t address);
uint64_t tsfp_guest_load_u64(uint32_t address);
int16_t tsfp_guest_load_s16(uint32_t address);
int32_t tsfp_guest_load_s32(uint32_t address);
int64_t tsfp_guest_load_s64(uint32_t address);
float tsfp_guest_load_f32(uint32_t address);
double tsfp_guest_load_f64(uint32_t address);
void tsfp_guest_store_u16(uint32_t address, uint16_t value);
void tsfp_guest_store_u32(uint32_t address, uint32_t value);
void tsfp_guest_store_u64(uint32_t address, uint64_t value);
void tsfp_guest_store_s16(uint32_t address, int16_t value);
void tsfp_guest_store_s32(uint32_t address, int32_t value);
void tsfp_guest_store_s64(uint32_t address, int64_t value);
void tsfp_guest_store_f32(uint32_t address, float value);
void tsfp_guest_store_f64(uint32_t address, double value);

/* Override in the platform adapter; must not return. */
_Noreturn void tsfp_guest_memory_fault(uint32_t address, size_t width);
#if !defined(__GNUC__) && !defined(__clang__)
#error The portable guest lvalue adapter requires GCC/Clang packed may_alias types
#endif
/* Explicit unaligned, alias-safe lvalues retain upstream load/store ordering.
 * Volatile preserves the upstream contract, not inter-thread synchronization.
 * LOCK operations still require aligned atomic access and separate validation. */
#define TSFP_CELL(name, type) \
    typedef struct __attribute__((packed, may_alias)) { type value; } name
TSFP_CELL(TsfpU16, uint16_t); TSFP_CELL(TsfpU32, uint32_t);
TSFP_CELL(TsfpU64, uint64_t); TSFP_CELL(TsfpI16, int16_t);
TSFP_CELL(TsfpI32, int32_t); TSFP_CELL(TsfpI64, int64_t);
TSFP_CELL(TsfpF32, float); TSFP_CELL(TsfpF64, double);
#define TSFP_MEM(cell, addr) \
    (((volatile cell *)tsfp_guest_pointer((uint32_t)(addr), sizeof(cell)))->value)
#endif
