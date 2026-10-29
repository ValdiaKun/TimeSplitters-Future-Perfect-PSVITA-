# Reference and license audit (2026-10-28)

Exact revisions are locked in `sources.lock.json`. References informed design;
no Halo, THUG or MH3 implementation was copied. This is a source review, not
proof that their hardware behavior applies to TimeSplitters.

| Source | Inspected paths and applicable techniques | Constraints |
| --- | --- | --- |
| Halo CE Vita `79a3d1c` | `port/vita/platform/d3d8_gxm.c`: capture guest draw inputs in a bounded producer/consumer ring, separate preparation from replay; `host/vita_gxm.c`: fence frame ring reuse using fragment notifications, retain depth surfaces across target switches, resolution changes use a subrectangle; `platform/vita_textures.c`: preserve DXT blocks and reorder for GXM, convert other formats on upload, invalidate caches when source data changes; `host/vita_shader_cache.c`: version/checksum/compiler-setting cache keys; `host/vita_input.c`: native controller and touch/gyro adapters | Root GPL-3.0. Preserve notices/source if code is reused. This revision uses direct GXM, correcting the earlier roadmap's blanket vitaGL description. Runtime shader fallback needs user-supplied `libshacccg.suprx`; exclude that dependency from our baseline. Do not adopt interpolation or performance claims. |
| THUG Vita `6cb8cbe` | `vita/CMakeLists.txt`: native VitaSDK packaging, file-specific compiler scope, explicit profiling switches; `vita/src/vita_runtime.cpp`, `vita_log.c`, `vita_prof.c`: native heap/runtime/logging and instrumentation; `Code/Gfx/Vita/p_NxTexture.cpp`, `p_scene_load.cpp`: Xbox texture dictionary conversion and persistent geometry allocation, vitaGL engine adaptation | No root LICENSE/COPYING located. Inspected Vita files have project descriptions but no standalone grant. Engine provenance and per-file terms require review before reuse; take no code. Its `-ffast-math`, permissive C++ flags and heap/clocks are not correctness evidence for our guest runtime. `vita_prof.c` explicitly documents incorrect global profiling call stacks in multithreaded use: use per-thread accounting if instrumentation is later implemented. |
| MH3 Wii/Vita `98a4ef7` | Optimization index and linked flat-memory, native-function, render-thread, offline-shader-bank, GPU-skinning, compressed-texture, Vita-platform and TLS pages; `runtime/vita/guest_memory.cpp`, `platform.cpp`: bounded memory ownership; `runtime/vita/fast_tls.c` contract described by TLS guide. Frame packets capture lifetimes; precompiled finite shader variants avoid gameplay compilation; native replacements retain translated oracles; converted textures keep supported compressed blocks; GPU palette batches reduce repeated skinning | Root GPL-3.0; nested `aurora-main`, toolchain and third-party components have separate notices. No code reuse here. Its kubridge/CapUnlocker mappings, extra core and clocks are outside our baseline. Wii big-endian PPC/GX memory, registers and shaders cannot be used for Xbox x86/D3D8. TLS layout shortcuts depend on exact SDK ABI and need separate validation. |
| TimeSplitters PC `759f7a0` | `AGENTS.md`, context README/runtime/build-test/independent guides, `docs/development/independent-runtime.md`, `requirements.txt`, `tsfp.ps1`, runtime snapshot and prepare/apply scripts; runtime `templates/runtime/recomp_types.h`, translator/lifter/disasm/config, memory-layout and indirect-call guides | PC GPL-3.0 and NOTICE exclude proprietary game content. Runtime GPL-3.0 retains original MIT attribution and LGPL components from xemu; authoritative notices are under the pinned dependency's `LICENSES/`. The fixture harness uses only compiler/runtime-header interfaces; does not link audio/APU/SDL/FFmpeg or copy engine source. |

Before distributing a game runtime, inventory actual linked components, supply
corresponding source/build instructions and all applicable notices. Reference
licenses do not grant rights to retail Xbox content. This audit does not approve
uninspected third-party files for copying.

## Memory-policy follow-up

At the same PC pin, inspected prepared `src/kernel/xbox_memory_layout.c`
(contiguous allocation, 28-mirror loop, tiled mapping),
`src/kernel/xbox_memory_layout.h` (mirror/tiled constants) and
`src/kernel/kernel.h` (contiguous window). The PC policy uses a distinct
contiguous backing and maps tiled storage to it, rather than to low RAM.
`tests/pc_memory_oracle.c` independently implements equivalent Linux shared
views for the synthetic oracle, using checked generated PC constants. No PC
mapping implementation was copied or edited. MMIO/flash remain unimplemented;
classification does not reproduce their PC emulation or hardware registers.

## Generated-memory source adapter

Inspected the pinned PC `tools/compiler/translation_operands.py`,
`translation_moves.py`, `translation_integer.py`, prepared
`tools/recomp/{lifter,translator,__main__}.py` and runtime
`templates/runtime/recomp_types.h` for scalar lvalues, atomic pointer arguments,
SSE/MMX helpers and stack commit ordering. New process-local source adapters are
GPL-3.0-only and leave that dependency unchanged. The AST parser is pycparser
2.23 (BSD; notice in LICENSES/pycparser-BSD.txt), a host build dependency only.
No new Halo/THUG/MH3 code is used; their previously recorded audit remains valid.

## String instruction follow-up

At the same PC pin, inspected prepared `tools/recomp/lifter.py` REP/string
emission, EFLAGS consumers and basic-block flag seeding, `translator.py` flag
local declarations and preserved decoder instruction bytes. The new source and
runtime adapters are independently implemented GPL-3.0-only code. The x86 host
oracle uses authored assembly fixtures and Linux signal context interfaces; no
retail instructions or foreign port implementation was copied. Existing
Halo/THUG/MH3 audits and all dependency pins are unchanged. Their bulk-copy or
threading techniques do not establish Xbox string overlap/fault semantics.
