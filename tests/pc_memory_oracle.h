/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_PC_MEMORY_ORACLE_H
#define TSFP_PC_MEMORY_ORACLE_H
#include <stdint.h>
/* Linux host equivalent of the pinned PC's shared-view map, not kernel/HLE. */
void pc_memory_oracle_open(void **ram, void **contiguous, uintptr_t *base);
void pc_memory_oracle_close(void);
#endif
