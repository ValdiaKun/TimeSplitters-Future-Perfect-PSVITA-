/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef TSFP_PC_MAPPED_ORACLE
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "pc_memory_oracle.h"
#include "pc_memory_contract.h"
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
_Static_assert(sizeof(uintptr_t) >= 8, "Linux oracle reserves a 4 GiB guest VA window");
static unsigned char *window;
static const size_t window_size = UINT64_C(0x100000000);
static void *map_shared(int fd, uint32_t address, size_t size) {
    void *requested = window + address;
    void *result = mmap(requested, size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_FIXED, fd, 0);
    if (result != requested) { perror("pc oracle shared view"); abort(); }
    return result;
}
void pc_memory_oracle_open(void **ram, void **contiguous, uintptr_t *base) {
    /* MAP_FIXED only replaces parts of this operation's own reservation. */
    window = mmap(NULL, window_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (window == MAP_FAILED) { perror("pc oracle reservation"); abort(); }
    const size_t size = (size_t)PC_CONTIG_MIB * 1024u * 1024u;
    int ram_fd = memfd_create("tsfp-oracle-ram", MFD_CLOEXEC);
    int contig_fd = memfd_create("tsfp-oracle-contiguous", MFD_CLOEXEC);
    assert(ram_fd >= 0 && contig_fd >= 0);
    assert(ftruncate(ram_fd, (off_t)size) == 0 && ftruncate(contig_fd, (off_t)size) == 0);
    for (unsigned i = 0; i <= PC_RAM_MIRRORS; ++i)
        map_shared(ram_fd, (uint32_t)(i * size), size);
    *ram = window;
    *contiguous = map_shared(contig_fd, PC_CONTIG_BASE, size);
    map_shared(contig_fd, PC_TILED_BASE, size);
    assert(close(ram_fd) == 0 && close(contig_fd) == 0);
    *base = (uintptr_t)window;
}
void pc_memory_oracle_close(void) {
    assert(munmap(window, window_size) == 0);
    window = NULL;
}
#endif
