/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_IMAGE_H
#define TSFP_IMAGE_H
#include "execution.h"
#define TSFP_IMAGE_READ 1u
#define TSFP_IMAGE_WRITE 2u
#define TSFP_IMAGE_EXECUTE 4u
#define TSFP_IMAGE_MAX_SECTIONS 16u
#define TSFP_IMAGE_MAX_BYTES (1024u*1024u)
typedef struct TsfpImageSection {
    uint32_t address, size, flags;
    const void *data;
    size_t file_size;
} TsfpImageSection;
typedef void (*TsfpImageFunction)(void);
typedef TsfpImageFunction (*TsfpImageResolver)(uint32_t address);
typedef struct TsfpImageDescription {
    const TsfpImageSection *sections;
    size_t count;
    uint32_t entry, import_table, thread_id, stack_base, stack_size;
    size_t import_count;
    uint32_t tib, tls_template, tls_size, thread_data_size;
    uint32_t object, object_template;
    TsfpImageResolver resolve; /* Pure trusted compiled registry; never a RAM cast. */
} TsfpImageDescription;
enum TsfpImageError { TSFP_IMAGE_OK, TSFP_IMAGE_BUSY, TSFP_IMAGE_METADATA,
    TSFP_IMAGE_RANGE, TSFP_IMAGE_OVERLAP, TSFP_IMAGE_ENTRY, TSFP_IMAGE_IMPORT,
    TSFP_IMAGE_THREAD, TSFP_IMAGE_ALLOCATION };
/* Quiescent, exclusive memory bindings; host input buffers live until return.
 * Synthetic low-RAM image contract, not an XBE parser. Invalid input leaves RAM,
 * CPU, installed imports and published image unchanged. One active image only. */
/* Optional platform allocation pair; NULL/NULL restores malloc/free. Changing
 * it requires no active image, thread or recovery scope. One bounded temporary. */
int tsfp_image_set_allocator(void *(*allocate)(size_t), void (*release)(void *));
enum TsfpImageError tsfp_image_initialize(const TsfpImageDescription *description);
int tsfp_image_run(TsfpFault *fault); /* -1: no runnable image, 0 normal, 1 event */
int tsfp_image_reset(void); /* quiescent; never clears guest RAM or saves */
int tsfp_image_active(void);
#endif
