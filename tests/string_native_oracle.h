/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_STRING_NATIVE_ORACLE_H
#define TSFP_STRING_NATIVE_ORACLE_H
#include <stdint.h>
typedef struct StringState {
    uint32_t source, destination, count, accumulator, flags;
    uint32_t fault_address, fault_width;
    int fault;
} StringState;
#ifdef TSFP_PC_MAPPED_ORACLE
void native_string_oracle(unsigned kind, unsigned width, unsigned repeat, StringState *state);
#endif
#endif
