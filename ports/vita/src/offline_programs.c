/* SPDX-License-Identifier: GPL-3.0-only */
#include "offline_programs.h"
#include "stream_texture_v.h"
#include "stream_texture_f.h"
#include "stream_tint_f.h"
const SceGxmProgram *const tsfp_offline_texture_vertex=(const SceGxmProgram *)stream_texture_v;
const SceGxmProgram *const tsfp_offline_texture_fragment=(const SceGxmProgram *)stream_texture_f;
const SceGxmProgram *const tsfp_offline_tint_fragment=(const SceGxmProgram *)stream_tint_f;
