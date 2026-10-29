/* SPDX-License-Identifier: GPL-3.0-only */
#include "string_ops.h"
#include "memory.h"
#include "execution.h"
#include <string.h>
#ifdef TSFP_PC_MAPPED_ORACLE
/* Successful accesses in the Linux mapped-PC oracle use its real shared views. */
extern ptrdiff_t g_xbox_mem_offset;
#endif
_Thread_local uint32_t g_tsfp_guest_flags = 2u;
_Thread_local uint32_t g_tsfp_flags_valid = 0x400u;
#define ARITHMETIC_FLAGS UINT32_C(0x8d5)
static uint32_t mask_for(unsigned width) {
    if (width != 1 && width != 2 && width != 4) tsfp_guest_memory_fault(0,width);
    return width == 4 ? UINT32_MAX : (UINT32_C(1) << (width * 8u)) - 1u;
}
static uint32_t result_flags(uint32_t value, unsigned width) {
    uint32_t sign = UINT32_C(1) << (width * 8u - 1u);
    unsigned byte = value & 255u;
    byte ^= byte >> 4; byte ^= byte >> 2; byte ^= byte >> 1;
    return (value == 0 ? 0x40u : 0u) | (value & sign ? 0x80u : 0u) |
           ((byte & 1u) == 0 ? 4u : 0u);
}
uint32_t tsfp_sub_flags(uint32_t left, uint32_t right, unsigned width, uint32_t previous) {
    uint32_t mask = mask_for(width), sign = UINT32_C(1) << (width * 8u - 1u);
    left &= mask; right &= mask;
    uint32_t result = (left - right) & mask;
    return (previous & ~ARITHMETIC_FLAGS) | result_flags(result, width) |
           (left < right ? 1u : 0u) | ((left ^ right ^ result) & 0x10u) |
           (((left ^ right) & (left ^ result) & sign) ? 0x800u : 0u);
}
/* Operations: CMP=0, SUB result=1, ADD result=2, TEST operands=3, logic result=4.
 * Logical AF is undefined on x86; this bridge chooses zero, never claims parity
 * with hardware for undefined bits. Other preserved bits remain unchanged. */
uint32_t tsfp_alu_flags(uint32_t value, uint32_t right, unsigned width,
                        unsigned operation, uint32_t previous) {
    uint32_t mask = mask_for(width);
    if (operation > 4) tsfp_guest_memory_fault(0,width);
    if (operation == 0) return tsfp_sub_flags(value, right, width, previous);
    if (operation == 1) return tsfp_sub_flags((value + right) & mask, right, width, previous);
    if (operation == 3 || operation == 4)
        return (previous & ~ARITHMETIC_FLAGS) | result_flags((operation == 3 ? value & right : value) & mask, width);
    uint32_t left = (value - right) & mask;
    uint32_t sign = UINT32_C(1) << (width * 8u - 1u);
    return (previous & ~ARITHMETIC_FLAGS) | result_flags(value & mask, width) |
           ((uint64_t)left + (right & mask) > mask ? 1u : 0u) |
           ((left ^ right ^ value) & 0x10u) |
           ((~(left ^ right) & (left ^ value) & sign) ? 0x800u : 0u);
}
static void require_span(uint32_t address, unsigned width, enum TsfpAccess access) {
    g_tsfp_access=access;
    if (!tsfp_guest_range_valid(address, width)) {
        tsfp_guest_memory_fault(address, width);
    }
}
static uint32_t load_element(uint32_t address, unsigned width) {
    require_span(address, width, TSFP_ACCESS_READ);
#ifdef TSFP_PC_MAPPED_ORACLE
    uint32_t value = 0;
    memcpy(&value, (const void *)((uintptr_t)address + g_xbox_mem_offset), width);
    return value;
#else
    if (width == 1) return tsfp_guest_load_u8(address);
    if (width == 2) return tsfp_guest_load_u16(address);
    return tsfp_guest_load_u32(address);
#endif
}
static void store_element(uint32_t address, uint32_t value, unsigned width) {
    require_span(address, width, TSFP_ACCESS_WRITE);
#ifdef TSFP_PC_MAPPED_ORACLE
    memcpy((void *)((uintptr_t)address + g_xbox_mem_offset), &value, width);
#else
    if (width == 1) tsfp_guest_store_u8(address, (uint8_t)value);
    else if (width == 2) tsfp_guest_store_u16(address, (uint16_t)value);
    else tsfp_guest_store_u32(address, value);
#endif
}
void tsfp_guest_string(unsigned kind, unsigned width, unsigned repeat,
                       uint32_t *source, uint32_t *destination, uint32_t *count,
                       uint32_t *accumulator, int direction) {
    if (kind < TSFP_MOVS || kind > TSFP_LODS ||
        (width != 1 && width != 2 && width != 4) || repeat > TSFP_REP_NOT_EQUAL)
        tsfp_guest_memory_fault(0, width);
    uint32_t entry_flags = (g_tsfp_string_flags & ~0x400u) | (direction ? 0x400u : 0u);
    g_tsfp_string_flags = entry_flags;
    if (repeat && !*count) return; /* No pointer arithmetic or access. */
    uint32_t step = direction ? 0u - width : width;
    for (;;) {
        /* Intel REP CMPS/SCAS faults restore pre-instruction EFLAGS, while
         * completed index/count progress remains visible (Intel SDM REP). */
        if (repeat && (kind == TSFP_CMPS || kind == TSFP_SCAS)) {
            uint32_t bad = *destination;
            int invalid = !tsfp_guest_range_valid(bad,width);
            if (kind == TSFP_CMPS && !tsfp_guest_range_valid(*source,width)) {
                bad=*source; invalid=1;
            }
            if (invalid) {
                g_tsfp_string_flags=entry_flags;
                g_tsfp_access=TSFP_ACCESS_READ;
                tsfp_guest_memory_fault(bad,width);
            }
        }
        uint32_t left = *accumulator, right;
        if (kind == TSFP_MOVS || kind == TSFP_CMPS || kind == TSFP_LODS)
            left = load_element(*source, width);
        if (kind == TSFP_MOVS || kind == TSFP_STOS)
            store_element(*destination, left, width);
        else if (kind == TSFP_CMPS || kind == TSFP_SCAS) {
            right = load_element(*destination, width);
            g_tsfp_string_flags = tsfp_sub_flags(left, right, width, g_tsfp_string_flags);
        } else {
            uint32_t mask = mask_for(width);
            *accumulator = (*accumulator & ~mask) | (left & mask);
        }
        /* A failed element commits neither pointer nor count nor a partial store.
         * Previous successful elements, including alias overlap, stay committed. */
        if (kind == TSFP_MOVS || kind == TSFP_CMPS || kind == TSFP_LODS) *source += step;
        if (kind != TSFP_LODS) *destination += step;
        if (!repeat) break;
        --*count;
        if (!*count) break;
        if ((kind == TSFP_CMPS || kind == TSFP_SCAS) &&
            (((g_tsfp_string_flags & 0x40u) != 0) != (repeat == TSFP_REP_EQUAL))) break;
    }
}
