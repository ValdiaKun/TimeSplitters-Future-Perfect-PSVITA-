/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_XBE_H
#define TSFP_XBE_H
#include "image.h"
typedef struct TsfpXbeImage {
    TsfpImageSection sections[TSFP_IMAGE_MAX_SECTIONS];
    size_t count,import_count;
    uint32_t entry,import_table,tls_directory,stack_commit;
} TsfpXbeImage;
/* Borrowed file views; keep accessible input bytes alive until image_initialize
 * returns. Bounded structural decoder, NOT signature/hash/retail verification.
 * Output is unchanged on failure. Thread allocation/TLS extraction stay explicit. */
int tsfp_xbe_normalize(const void *file, size_t size, TsfpXbeImage *output);
#endif
