/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_GUEST_ADDRESS_SPACE_H
#define TSFP_GUEST_ADDRESS_SPACE_H
#include <stddef.h>
#include <stdint.h>
#define TSFP_XBOX_RAM_SIZE UINT32_C(0x04000000)
#define TSFP_XBOX_RAM_VIEWS 29u
/* Pinned PC runtime's successful 64 MiB mapping policy, not a hardware MMU. */
typedef enum TsfpGuestRegion {
    TSFP_REGION_INVALID,
    TSFP_REGION_RAM,
    TSFP_REGION_RAM_ALIAS,
    TSFP_REGION_CONTIGUOUS,
    TSFP_REGION_KERNEL_DATA,
    TSFP_REGION_TILED,
    TSFP_REGION_NV2A,
    TSFP_REGION_KERNEL_THUNK,
    TSFP_REGION_MCPX,
    TSFP_REGION_FLASH
} TsfpGuestRegion;
typedef struct TsfpGuestSpan {
    TsfpGuestRegion region;
    uint32_t offset;
    uint32_t available; /* bytes until this classification/view ends */
} TsfpGuestSpan;
TsfpGuestSpan tsfp_guest_classify(uint32_t address);
/* Caller owns exact-size, disjoint backing allocations, kept alive until unbind.
 * No allocation, fixed address mapping, kernel plugin or memory clearing occurs.
 * Optional contiguous storage may be NULL/0. Invalid rebinds preserve the map.
 * Bind/unbind only while guest threads and borrowed-pointer users are stopped. */
void tsfp_guest_bind_xbox(void *ram, size_t ram_size, void *contiguous, size_t contiguous_size);
void tsfp_guest_unbind(void);
/* Validated whole-range, byte-preserving transfers; low RAM mirror wraps are
 * supported, 32-bit address rollover and all unbacked/device regions rejected.
 * Host buffers must not overlap guest backing. These are not atomic operations. */
void tsfp_guest_read(uint32_t address, void *destination, size_t width);
void tsfp_guest_write(uint32_t address, const void *source, size_t width);
#endif
