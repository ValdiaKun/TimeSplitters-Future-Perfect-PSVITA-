/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_STRING_OPS_H
#define TSFP_STRING_OPS_H
#include <stdint.h>
enum TsfpStringKind { TSFP_MOVS = 1, TSFP_STOS, TSFP_CMPS, TSFP_SCAS, TSFP_LODS };
enum TsfpStringRepeat { TSFP_SINGLE, TSFP_REP_EQUAL, TSFP_REP_NOT_EQUAL };
/* Arithmetic EFLAGS plus preserved caller bits. Guest-thread entry must seed
 * this TLS cell; it is not a replacement for the complete CPU flags runtime. */
extern _Thread_local uint32_t g_tsfp_guest_flags;
extern _Thread_local uint32_t g_tsfp_flags_valid;
/* Compatibility name for the first string-only bridge. */
#define g_tsfp_string_flags g_tsfp_guest_flags
uint32_t tsfp_sub_flags(uint32_t left, uint32_t right, unsigned width, uint32_t previous);
uint32_t tsfp_alu_flags(uint32_t result_or_left, uint32_t right, unsigned width,
                        unsigned operation, uint32_t previous);
void tsfp_guest_string(unsigned kind, unsigned width, unsigned repeat,
                       uint32_t *source, uint32_t *destination, uint32_t *count,
                       uint32_t *accumulator, int direction);
#endif
