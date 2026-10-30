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

## Execution/thread foundation

At the unchanged PC pin, inspected prepared `src/kernel/kernel_bridge.c`
(`bridge_run_thread_inline`, `bridge_thread_main`, `bridge_PsCreateSystemThreadEx`)
and `src/kernel/xbox_memory_layout.c` (`xbox_AllocThreadTib`). The portable
execution boundary and bounded TLS preparer are original GPL-3.0-only adapters
of documented ABI/layout behavior, not copied Win32 scheduling or SEH code.
No Halo/THUG/MH3 files, new third-party dependencies or source-pin changes.

## Thread object/import subset

Same PC pin: `xbox_memory_layout.c` defines object size 0x200, ID offset 0x12c,
fs:[0x28] current object and template copy/identity patch. `kernel_bridge.c`
records canonical ordinals 104/320/258, argument bytes 0/8/4, and per-slot
FE000000+4*i function targets. New adapters are original GPL-3.0-only code behind
checked memory and C execution boundaries, not copied Win32 lifecycle code.
Generator checks these exact anchors. No third-party pin/license changes.

## Synthetic image initialization

At the unchanged PC pin, inspected prepared `src/kernel/xbox_memory_layout.c`
section loading: virtual sizes zeroed, initialized bytes copied, executable flag
0x04 used for dispatch ranges. Existing TIB/template/import contracts remain the
source of field layouts. New `guest/image.c` is original GPL-3.0-only checked
normalization/publication logic, not copied Win32 mapping code. It rejects raw
file lengths above virtual size; a future XBE parser must normalize raw padding
(the PC clamps copy size). No XBE parser or retail source is copied here.

## Raw XBE structural subset

Inspected pinned PC tools/compiler/xbe_image.py (canonical retail/debug key pairs,
header/section bounds, file/virtual overlap, entry selection, imports) and prepared
xbox_memory_layout.c (raw copy clipping, temporary write access for read-only
thunk tables). New C decoder is original GPL-3.0-only code. The validation tool
invokes the pinned Python parser on authored build-local fixtures; it does not
copy parser code or export-name tables. No new dependencies or source pins.

## XBE TLS profile

Same PC pin: prepared xbox_memory_layout.c TLS initialization (round to 16 plus
4; fs:[4] block end; reserved slot zero; title-owned index) and xbox_AllocThreadTib
(worker copy and per-thread data). tools/compiler/xbe_image.py supplies TLS field
layout and file-backed/reversed-range validation. Original GPL-3.0-only adapters
add overflow, permissions, alias and callback checks. Worker object+0x28 is patched
to its own block instead of inheriting the main pointer; this is an explicit
isolation correction rather than blindly copying the Win32 worker path. No
foreign code, new dependency, proprietary data or source pin is introduced.

Thread-handle contract audit at the existing PC/runtime pins: `src/kernel/
kernel_bridge.c` defines BRIDGE_HANDLE_TAG 0x48000000 and ordinal 187 NtClose
with 4-byte arguments. `src/kernel/kernel_file.c` provides STATUS_INVALID_HANDLE
for invalid file handles; the bridge also accepts special fake handles. The
Vita adapter independently implements a strict registered-thread subset and
generation retirement, with no copied source or fake-handle success behavior.
Generator guards retain the pinned tag/ordinal/argument contract.

Native worker contract at the unchanged PC/runtime pins: kernel_bridge.c
bridge_run_thread_inline pushes context2/context1/return marker, and
bridge_thread_main returns native status zero on ordinary return. The Vita
adapter independently implements those entry/status contracts plus explicit
join and native lifetime pins. It does not copy the Win32 guest-lock/scheduler
or claim compatibility for priorities/suspension. Public pthread backend only;
Vita scheduling/hardware behavior still requires device validation.

Storage contract audit uses unchanged pinned kernel/xbox_memory_layout.c
xbox_AllocThreadStack, xbox_FreeThreadStack and xbox_AllocThreadTib. The PC
stack allocator reserves heap slices and aligns the top; it does not establish
Vita stack sizes. The adapter keeps private stack/TLS/object ownership and
existing 0x200/+0x12c/+0x28 layouts, independently implementing bounded slot
reuse and rollback. It does not copy the PC heap allocator or its 512 KiB/eight
stack policy. Production title allocation remains a separate integration gate.

Creation-service audit at unchanged PC/runtime pins: kernel_bridge.c documents
PsCreateSystemThreadEx ordinal 255, ten arguments/40 bytes and two start contexts;
the generator checks its ABI/lookup. Its fake initial handle and unconditional
success are not reused. The new request service, durable C bookkeeping guard,
strict argument/output profile and shutdown ownership are original adapters,
not copied Win32 scheduler code or a full Xbox kernel implementation.

## Event contracts

Inspected pinned PC kernel_sync.c, kernel_bridge.c and kernel.h: ordinals
189/16-byte NtCreateEvent, 225/8-byte NtSetEvent, 186/4-byte NtClearEvent,
233/12-byte NtWaitForSingleObject; NotificationEvent=0 and SynchronizationEvent=1.
Generator guards pin these declarations. Independent implementation; no copied
third-party event code. Wrong-kind Set/Clear uses the pinned invalid-handle status.
Unlike the PC previous-state placeholder, this implementation tracks state.
Bridge notification-generation comments motivate retaining grants across a
subsequent clear; test schedules verify both event types.

Pinned bridge file-I/O layout comments explicitly define Xbox OBJECT_ATTRIBUTES
as 12 bytes with RootDirectory/ObjectName/Attributes at offsets 0/4/8. Its event
implementation ignores the native descriptor; this port only accepts null or a
checked empty descriptor and fails closed for names/directories/flags.

See ACCELERATION_AUDIT.md for the2026-10-29 focused reuse audit. New references
are pinned without moving PC/runtime/toolchain revisions. MIT xboxrecomp
guest_vmem ownership/rounding and GPL Xita BC layout are adapted with attribution
and bounded wrappers; original VM source is hash-checked for differential tests.
No full Windows kernel, Halo runtime, game captures/shaders or binaries imported.

## 2026-10-30: vitaGL precompiled clear shaders (ADAPT)

| Item | Finding |
| --- | --- |
| Source | `Rinnegatamante/vitaGL` `3cb7872d6d5808fb1cddf4f643f29306cb53be4e`, `source/shaders/precompiled_clear_{v,f}.h`. |
| License | `COPYING` (GPL-3.0) and `COPYING.LESSER` (LGPL-3.0). Compatible with this repository's GPL-3.0-only; notice kept in `NOTICE` and the file headers. |
| Provenance | Each header carries the authored Cg source in a comment; the GXP blob is its compiled form. Blob SHA-256 values are pinned in `tools/test_gxm_shaders.py`. |
| Use | `ports/vita/src/gxm_draw.c` registers them with the shader patcher for a GPU-executed, indexed, uniform-driven rectangle draw. |
| Limits | Both vitaGL precompiled pairs (`clear`, `blit`) derive geometry from the vertex INDEX input and uniforms, so neither provides a vertex-attribute-stream test. vitaGL's fixed-function shaders are source text compiled at runtime by `libshacccg.suprx`, which this baseline does not use. The new experimental public-source offline Cg build provides a bounded position/UV profile; see docs/OFFLINE_SHADERS.md. Hardware acceptance and vertex COLOR remain unverified/unsupported respectively. |
| Rejected | Hand-assembled GXP microcode (unverifiable without hardware); other collections of binary shaders of unreviewed provenance. |

## 2026-10-30: open-shacccg (ADAPT)

MIT source revision `26da8ebc44717147d002c60d4a80cd6604aace7f`; exact files and
hashes in tools/shader_compiler.lock.json. Own strict driver, two host build
compatibility patches, zero unsafe optimization flags and full byte/interface
locks. Public Debian dependency notices remain in extracted copyright files.
See ACCELERATION_AUDIT.md and OFFLINE_SHADERS.md for rejected alternatives and
experimental limits. No firmware or upstream GXP binaries fetched.
