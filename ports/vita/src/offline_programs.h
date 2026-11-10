/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_OFFLINE_PROGRAMS_H
#define TSFP_OFFLINE_PROGRAMS_H
#include <psp2/gxm.h>
/* Experimental source-generated programs. Static lifetime, no runtime compiler.
 * Structural/interface validation is not physical GXM acceptance. */
extern const SceGxmProgram *const tsfp_offline_texture_vertex;
extern const SceGxmProgram *const tsfp_offline_texture_fragment;
extern const SceGxmProgram *const tsfp_offline_tint_fragment;
#endif
