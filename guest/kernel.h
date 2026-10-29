/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_KERNEL_H
#define TSFP_KERNEL_H
#include <stdint.h>
#include <stddef.h>
/* Quiescent initialization only. Caller owns exclusive, disjoint TIB/object
 * storage and the template's lifetime during this call. No native handles. */
int tsfp_thread_object_prepare(uint32_t object, uint32_t template_address, uint32_t tib, uint32_t id);
uint32_t tsfp_current_thread_object(void);
/* Canonical ordinal markers, bounded to 16 function slots. No remapped or data
 * exports. Caller must stop all guest threads before replacing the table. */
void tsfp_kernel_imports_reset(void);
int tsfp_kernel_imports_prepare(uint32_t table, size_t count);
void (*tsfp_kernel_lookup(uint32_t address))(void);
#endif
