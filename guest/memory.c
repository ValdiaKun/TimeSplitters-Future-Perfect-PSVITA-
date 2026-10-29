/* SPDX-License-Identifier: GPL-3.0-only */
#include "memory.h"
#include "execution.h"
_Thread_local enum TsfpAccess g_tsfp_access;
#include <limits.h>
#include <string.h>
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8, "guest requires IEEE storage widths");
_Static_assert(CHAR_BIT == 8 && sizeof(uint32_t) == 4, "guest requires 8-bit bytes and 32-bit words");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Guest adapter requires a little-endian target
#endif
static unsigned char *bytes, *contiguous_bytes;
static size_t length;
static uint32_t origin;
static int xbox_mode;
static TsfpMemoryPolicy access_policy;
int tsfp_guest_policy_active(void) { return access_policy!=NULL; }
void tsfp_guest_set_policy(TsfpMemoryPolicy policy) { access_policy=policy; }
int tsfp_guest_access_allowed(uint32_t address, size_t width, unsigned mode) {
    return !access_policy || access_policy(address,width,mode);
}
void tsfp_guest_check_access(uint32_t address, size_t width, unsigned mode) {
    if (!tsfp_guest_access_allowed(address,width,mode)) tsfp_guest_memory_fault(address,width);
}
static int valid_storage(const void *storage, size_t size) {
    return storage && size && size - 1 <= UINTPTR_MAX - (uintptr_t)storage;
}
static int overlaps(const void *a, size_t a_size, const void *b, size_t b_size) {
    uintptr_t left = (uintptr_t)a, right = (uintptr_t)b;
    return left <= right ? right - left < a_size : left - right < b_size;
}
void tsfp_guest_unbind(void) {
    g_tsfp_access=TSFP_ACCESS_UNKNOWN;
    if (access_policy) tsfp_guest_memory_fault(0,0);
    bytes = contiguous_bytes = NULL;
    length = 0;
    origin = 0;
    xbox_mode = 0;
}
void tsfp_guest_bind(void *storage, size_t size, uint32_t base) {
    g_tsfp_access=TSFP_ACCESS_UNKNOWN;
    if (access_policy) tsfp_guest_memory_fault(0,0);
    if (!valid_storage(storage, size) || (uint64_t)size > UINT64_C(0x100000000) - base)
        tsfp_guest_memory_fault(base, size);
    bytes = storage;
    length = size;
    origin = base;
    contiguous_bytes = NULL;
    xbox_mode = 0;
}
void tsfp_guest_bind_xbox(void *ram, size_t ram_size, void *contiguous, size_t contiguous_size) {
    g_tsfp_access=TSFP_ACCESS_UNKNOWN;
    if (access_policy) tsfp_guest_memory_fault(0,0);
    if (ram_size != TSFP_XBOX_RAM_SIZE || !valid_storage(ram, ram_size) ||
        (contiguous ? contiguous_size != TSFP_XBOX_RAM_SIZE ||
                      !valid_storage(contiguous, contiguous_size) ||
                      overlaps(ram, ram_size, contiguous, contiguous_size)
                    : contiguous_size != 0))
        tsfp_guest_memory_fault(0, ram_size);
    bytes = ram;
    length = ram_size;
    origin = 0;
    contiguous_bytes = contiguous;
    xbox_mode = 1;
}
static unsigned char *resolve(uint32_t address, size_t *available) {
    if (!bytes) return NULL;
    if (!xbox_mode) {
        size_t offset = (size_t)(address - origin);
        if (address < origin || offset >= length) return NULL;
        *available = length - offset;
        return bytes + offset;
    }
    TsfpGuestSpan span = tsfp_guest_classify(address);
    *available = span.available;
    switch (span.region) {
    case TSFP_REGION_RAM: case TSFP_REGION_RAM_ALIAS:
        return bytes + span.offset;
    case TSFP_REGION_CONTIGUOUS: case TSFP_REGION_KERNEL_DATA: case TSFP_REGION_TILED:
        *available = TSFP_XBOX_RAM_SIZE - span.offset;
        return contiguous_bytes ? contiguous_bytes + span.offset : NULL;
    default:
        return NULL;
    }
}
int tsfp_guest_range_valid(uint32_t address, size_t width) {
    if (!width || (uint64_t)width > UINT64_C(0x100000000) - address) return 0;
    while (width) {
        size_t available = 0;
        if (!resolve(address, &available) || !available) return 0;
        size_t chunk = width < available ? width : available;
        width -= chunk;
        if (width) address += (uint32_t)chunk;
    }
    return 1;
}
void *tsfp_guest_pointer(uint32_t address, size_t width) {
    g_tsfp_access=TSFP_ACCESS_UNKNOWN;
    tsfp_guest_check_access(address,width,2); /* borrowed lvalues may write */
    size_t available = 0;
    unsigned char *pointer = resolve(address, &available);
    /* A borrowed native pointer cannot represent a wrapped physical span. */
    if (!pointer || !width || width > available)
        tsfp_guest_memory_fault(address, width);
    return pointer;
}
static void validate_transfer(uint32_t address, const void *host, size_t width) {
    uint32_t cursor = address;
    size_t remaining = width;
    if (!valid_storage(host, width) || (uint64_t)width > UINT64_C(0x100000000) - address ||
        (bytes && overlaps(host, width, bytes, length)) ||
        (contiguous_bytes && overlaps(host, width, contiguous_bytes, TSFP_XBOX_RAM_SIZE)))
        tsfp_guest_memory_fault(address, width);
    /* Validate every segment before copying so failed transfers change nothing. */
    while (remaining) {
        size_t available = 0;
        if (!resolve(cursor, &available) || !available)
            tsfp_guest_memory_fault(address, width);
        size_t chunk = remaining < available ? remaining : available;
        remaining -= chunk;
        if (remaining) cursor += (uint32_t)chunk;
    }
}
static void transfer(uint32_t address, void *host, size_t width, int write) {
    g_tsfp_access=write ? TSFP_ACCESS_WRITE : TSFP_ACCESS_READ;
    tsfp_guest_check_access(address,width,write ? 2u : 1u);
    validate_transfer(address, host, width);
    unsigned char *host_bytes = host;
    while (width) {
        size_t available = 0;
        unsigned char *guest = resolve(address, &available);
        size_t chunk = width < available ? width : available;
        if (write) memcpy(guest, host_bytes, chunk);
        else memcpy(host_bytes, guest, chunk);
        width -= chunk;
        host_bytes += chunk;
        if (width) address += (uint32_t)chunk;
    }
}
void tsfp_guest_read(uint32_t address, void *destination, size_t width) {
    transfer(address, destination, width, 0);
}
void tsfp_guest_write(uint32_t address, const void *source, size_t width) {
    transfer(address, (void *)source, width, 1);
}

/* Scalar loads/stores use the validated two-phase transfer path. memcpy
 * preserves signed and FP payload bits (including NaNs and negative zero).
 * On a fast contiguous span the underlying copy is a single memcpy; valid
 * physical RAM wraps are split only when necessary. No guest lvalue escapes. */
#define TSFP_DEFINE_SCALAR_ACCESS(suffix, type)                                  \
    type tsfp_guest_load_##suffix(uint32_t address) {                           \
        type value;                                                              \
        tsfp_guest_read(address, &value, sizeof(value));                         \
        return value;                                                            \
    }                                                                            \
    void tsfp_guest_store_##suffix(uint32_t address, type value) {               \
        tsfp_guest_write(address, &value, sizeof(value));                        \
    }

TSFP_DEFINE_SCALAR_ACCESS(u8, uint8_t)
TSFP_DEFINE_SCALAR_ACCESS(s8, int8_t)
TSFP_DEFINE_SCALAR_ACCESS(u16, uint16_t)
TSFP_DEFINE_SCALAR_ACCESS(u32, uint32_t)
TSFP_DEFINE_SCALAR_ACCESS(u64, uint64_t)
TSFP_DEFINE_SCALAR_ACCESS(s16, int16_t)
TSFP_DEFINE_SCALAR_ACCESS(s32, int32_t)
TSFP_DEFINE_SCALAR_ACCESS(s64, int64_t)
TSFP_DEFINE_SCALAR_ACCESS(f32, float)
TSFP_DEFINE_SCALAR_ACCESS(f64, double)
#undef TSFP_DEFINE_SCALAR_ACCESS


void *tsfp_guest_atomic_pointer(uint32_t address) {
    g_tsfp_access=TSFP_ACCESS_ATOMIC;
    tsfp_guest_check_access(address,sizeof(uint32_t),2);
    size_t available=0;
    void *pointer=resolve(address,&available);
    g_tsfp_access=TSFP_ACCESS_ATOMIC;
    if (!pointer || available<sizeof(uint32_t)) tsfp_guest_memory_fault(address,sizeof(uint32_t));
    if ((address & 3u) || ((uintptr_t)pointer & 3u))
        tsfp_guest_memory_fault(address, sizeof(uint32_t));
    return pointer;
}
