/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_XBOX_BC_CHAIN_H
#define TSFP_XBOX_BC_CHAIN_H
#include "xbox_bc_mips.h"
/* Asset-free contiguous mip reordering; no guest reads or GPU operations.
 * Rejects untrusted plans, overlapping backing, and short capacities before writes. */
enum TsfpBcResult tsfp_bc_reorder_chain(void *destination, size_t destination_bytes,
    const void *source, size_t source_bytes, const struct TsfpBcMipPlan *plan,
    unsigned block_bytes);
#endif
