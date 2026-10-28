/* SPDX-License-Identifier: GPL-3.0-only */
#include "memory.h"
#include <limits.h>
#include <stdlib.h>
_Static_assert(CHAR_BIT == 8 && sizeof(uint32_t) == 4, "guest requires 8-bit bytes and 32-bit words");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Guest adapter requires a little-endian target
#endif
static unsigned char *bytes;
static size_t length;
static uint32_t origin;
void tsfp_guest_bind(void *storage, size_t size, uint32_t base) {
    if (!storage || !size || (uint64_t)size > UINT64_C(0x100000000) - base)
        tsfp_guest_memory_fault(base, size);
    bytes = storage;
    length = size;
    origin = base;
}
void *tsfp_guest_pointer(uint32_t address, size_t width) {
    size_t offset = (size_t)(address - origin);
    if (!bytes || address < origin || !width || offset >= length || width > length - offset)
        tsfp_guest_memory_fault(address, width);
    return bytes + offset;
}
