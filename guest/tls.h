/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_TLS_H
#define TSFP_TLS_H
#include <stdint.h>
#include <stddef.h>
struct TsfpImageSection;
#define TSFP_TLS_MAX_BLOCK (65536u-64u)
typedef struct TsfpTlsSpec {
    uint32_t directory,raw_start,raw_size,zero_fill,index_address,callbacks;
    uint32_t block_size,index_value;
} TsfpTlsSpec;
enum TsfpTlsError { TSFP_TLS_OK, TSFP_TLS_DIRECTORY, TSFP_TLS_RANGE,
    TSFP_TLS_INDEX, TSFP_TLS_CALLBACK_TABLE, TSFP_TLS_CALLBACK_UNSUPPORTED,
    TSFP_TLS_CHARACTERISTICS, TSFP_TLS_OVERLAP };
enum TsfpTlsIndexPolicy { TSFP_TLS_TITLE_INDEX, TSFP_TLS_WRITE_INDEX };
/* Physical overlap for bounded low-RAM ranges (at most one mirror wrap). */
int tsfp_tls_ranges_overlap(uint32_t a,size_t na,uint32_t b,size_t nb);
int tsfp_tls_layout(uint32_t raw_size,uint32_t zero_fill,uint32_t *total,uint32_t *index);
/* Output unchanged on failure. Metadata/raw sources must be file-backed;
 * index may be writable BSS. Only null or an empty callback table is supported. */
enum TsfpTlsError tsfp_tls_parse(const struct TsfpImageSection *sections,size_t count,
                                 uint32_t directory,TsfpTlsSpec *output);
#endif
