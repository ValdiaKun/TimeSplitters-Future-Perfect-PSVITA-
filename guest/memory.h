/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_GUEST_MEMORY_H
#define TSFP_GUEST_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include "address_space.h"
/* Retained bounded fixture mode; replaces any previous Xbox binding. */
void tsfp_guest_bind(void *storage, size_t size, uint32_t base);
void *tsfp_guest_pointer(uint32_t address, size_t width);
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
