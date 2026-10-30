/* SPDX-License-Identifier: GPL-3.0-only */
#include "address_space.h"
static TsfpGuestSpan span(TsfpGuestRegion region, uint32_t address,
                          uint32_t base, uint32_t end) {
    TsfpGuestSpan result = {region, address - base, end - address};
    return result;
}
TsfpGuestSpan tsfp_guest_classify(uint32_t address) {
    if (address < TSFP_XBOX_RAM_SIZE * TSFP_XBOX_RAM_VIEWS) {
        uint32_t offset = address % TSFP_XBOX_RAM_SIZE;
        TsfpGuestSpan result = {address < TSFP_XBOX_RAM_SIZE ? TSFP_REGION_RAM : TSFP_REGION_RAM_ALIAS,
                               offset, TSFP_XBOX_RAM_SIZE - offset};
        return result;
    }
    if (address >= UINT32_C(0x80010000) && address < UINT32_C(0x80011000))
        return span(TSFP_REGION_KERNEL_DATA, address, UINT32_C(0x80000000), UINT32_C(0x80011000));
    if (address >= UINT32_C(0x80000000) && address < UINT32_C(0x84000000)) {
        uint32_t end = address < UINT32_C(0x80010000) ? UINT32_C(0x80010000) : UINT32_C(0x84000000);
        return span(TSFP_REGION_CONTIGUOUS, address, UINT32_C(0x80000000), end);
    }
    if (address >= UINT32_C(0xf0000000) && address < UINT32_C(0xf4000000))
        return span(TSFP_REGION_TILED, address, UINT32_C(0xf0000000), UINT32_C(0xf4000000));
    if (address >= UINT32_C(0xfd000000) && address < UINT32_C(0xfe000000))
        return span(TSFP_REGION_NV2A, address, UINT32_C(0xfd000000), UINT32_C(0xfe000000));
    /* Classify the reserved synthetic dispatch namespace, never dereference it. */
    if (address >= UINT32_C(0xfe000000) && address < UINT32_C(0xfe800000))
        return span(TSFP_REGION_KERNEL_THUNK, address, UINT32_C(0xfe000000), UINT32_C(0xfe800000));
    if (address >= UINT32_C(0xfe800000) && address < UINT32_C(0xff000000))
        return span(TSFP_REGION_MCPX, address, UINT32_C(0xfe800000), UINT32_C(0xff000000));
    if (address >= UINT32_C(0xff000000)) {
        TsfpGuestSpan result = {TSFP_REGION_FLASH, address - UINT32_C(0xff000000),
                               UINT32_MAX - address};
        /* At most 16 MiB; addition cannot overflow this 32-bit count. */
        result.available++;
        return result;
    }
    { TsfpGuestSpan result = {TSFP_REGION_INVALID, 0, 0}; return result; }
}
