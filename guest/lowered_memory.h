/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_LOWERED_MEMORY_H
#define TSFP_LOWERED_MEMORY_H
/* Included after recomp_types.h: retains the PC header as the baseline oracle. */
#include "memory.h"
#include "string_ops.h"
#ifdef TSFP_PORTABLE_GUEST_MEMORY
#define TSFP_LOAD_U8 tsfp_guest_load_u8
#define TSFP_LOAD_S8 tsfp_guest_load_s8
#define TSFP_STORE_U8 tsfp_guest_store_u8
#define TSFP_STORE_S8 tsfp_guest_store_s8
#define TSFP_LOAD_U16 tsfp_guest_load_u16
#define TSFP_STORE_U16 tsfp_guest_store_u16
#define TSFP_LOAD_U32 tsfp_guest_load_u32
#define TSFP_STORE_U32 tsfp_guest_store_u32
#define TSFP_LOAD_U64 tsfp_guest_load_u64
#define TSFP_STORE_U64 tsfp_guest_store_u64
#define TSFP_LOAD_S16 tsfp_guest_load_s16
#define TSFP_STORE_S16 tsfp_guest_store_s16
#define TSFP_LOAD_S32 tsfp_guest_load_s32
#define TSFP_STORE_S32 tsfp_guest_store_s32
#define TSFP_LOAD_S64 tsfp_guest_load_s64
#define TSFP_STORE_S64 tsfp_guest_store_s64
#define TSFP_LOAD_F32 tsfp_guest_load_f32
#define TSFP_STORE_F32 tsfp_guest_store_f32
#define TSFP_LOAD_F64 tsfp_guest_load_f64
#define TSFP_STORE_F64 tsfp_guest_store_f64
#define TSFP_BORROW tsfp_guest_pointer
#define TSFP_ATOMIC_POINTER tsfp_guest_atomic_pointer
#else
#define TSFP_LOAD_U8(a) MEM8(a)
#define TSFP_STORE_U8(a, v) ((void)(MEM8(a) = (v)))
#define TSFP_LOAD_S8(a) SMEM8(a)
#define TSFP_STORE_S8(a, v) ((void)(SMEM8(a) = (v)))
#define TSFP_LOAD_U16(a) MEM16(a)
#define TSFP_STORE_U16(a, v) ((void)(MEM16(a) = (v)))
#define TSFP_LOAD_S16(a) SMEM16(a)
#define TSFP_STORE_S16(a, v) ((void)(SMEM16(a) = (v)))
#define TSFP_LOAD_U32(a) MEM32(a)
#define TSFP_STORE_U32(a, v) ((void)(MEM32(a) = (v)))
#define TSFP_LOAD_S32(a) SMEM32(a)
#define TSFP_STORE_S32(a, v) ((void)(SMEM32(a) = (v)))
#define TSFP_LOAD_U64(a) MEM64(a)
#define TSFP_STORE_U64(a, v) ((void)(MEM64(a) = (v)))
#define TSFP_LOAD_S64(a) SMEM64(a)
#define TSFP_STORE_S64(a, v) ((void)(SMEM64(a) = (v)))
#define TSFP_LOAD_F32(a) MEMF(a)
#define TSFP_STORE_F32(a, v) ((void)(MEMF(a) = (v)))
#define TSFP_LOAD_F64(a) MEMD(a)
#define TSFP_STORE_F64(a, v) ((void)(MEMD(a) = (v)))
#define TSFP_BORROW(a, n) ((void)(n), (void *)XBOX_PTR(a))
#define TSFP_ATOMIC_POINTER(a) ((void *)XBOX_PTR(a))
#endif
static inline RecompXmm tsfp_xmm_load(uint32_t address) {
    RecompXmm value;
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    tsfp_guest_read(address, &value, sizeof(value));
#else
    memcpy(&value, (void *)XBOX_PTR(address), sizeof(value));
#endif
    return value;
}
static inline void tsfp_xmm_store(uint32_t address, RecompXmm value) {
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    tsfp_guest_write(address, &value, sizeof(value));
#else
    memcpy((void *)XBOX_PTR(address), &value, sizeof(value));
#endif
}
static inline void tsfp_xmm_load_half(RecompXmm *value, uint32_t address, int high) {
    uint64_t bits = TSFP_LOAD_U64(address);
    value->q[high] = bits;
}
static inline void tsfp_xmm_store_half(uint32_t address, RecompXmm value, int high) {
    TSFP_STORE_U64(address, value.q[high]);
}
#ifdef RECOMP_MMX_DEFINED
static inline RecompMmx tsfp_mmx_load(uint32_t address) {
    RecompMmx value;
    value.q = TSFP_LOAD_U64(address);
    return value;
}
static inline void tsfp_mmx_store(uint32_t address, RecompMmx value) {
    TSFP_STORE_U64(address, value.q);
}
#endif
#endif
