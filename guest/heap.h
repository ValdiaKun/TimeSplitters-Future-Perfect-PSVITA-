/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_HEAP_H
#define TSFP_HEAP_H
#include <stdint.h>
#include <stddef.h>
/* Bounded suballocator of explicitly ceded writable image BSS. No new backing
 * or fixed host mappings. Configure/disable with image/guest lifecycle quiescent. */
int tsfp_heap_configure(uint32_t base,uint32_t size);
int tsfp_heap_disable(void);
int tsfp_heap_configured(void);
uint32_t tsfp_heap_allocate(uint32_t *base,uint32_t *size,uint32_t zero_bits,uint32_t type,uint32_t protect);
uint32_t tsfp_heap_free(uint32_t *base,uint32_t *size,uint32_t type);
/* Lock-free permission filter; binding/configuration changes are quiescent.
 * Configuration pins image policy/backing. All aliases share physical state. */
int tsfp_heap_access_allowed(uint32_t address,size_t size,unsigned mode);
int tsfp_heap_overlaps(uint32_t address,size_t size);
#endif
