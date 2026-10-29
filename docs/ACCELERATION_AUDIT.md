# Focused acceleration audit — 2026-10-29

Scope: guest allocation, ABI conformance and one compressed-texture compatibility
proof. No retail input or physical Vita exists here. Classifications apply to
individual components, not entire repositories. Performance implications below
are hypotheses unless explicitly measured. PC source/toolchain pins are unchanged.

## Exact source and decisions

| Source / revision / file | License and attribution | Contract and existing equivalent | Adaptation, complexity and correctness risk | Effort saved / performance implication | Validation / decision |
| --- | --- | --- | --- | --- | --- |
| PC `759f7a0ecebc2341cdcedf638fcea68c4a7fbf4e`, prepared runtime `d90c92988a9f99d0580d2ca3f7efd6ec3a7b7845` plus patches; `src/kernel/kernel_bridge.c`, `xbox_memory_layout.c` | Existing GPL/MIT component notices retained in pinned dependency | 184/20-byte allocate, 199/12-byte free; existing RAM/image/thread storage owns backing | Keep ABI and release outputs, reject reserve clamps/no-op commit/decommit. No moving pin; remote PC HEAD still equals pin during audit | Avoid rediscovering title-specific import/stack contracts; no speed claim | Generator ABI guards, actual translated allocate/store/load/free and parser tests. **REUSE** pinned pipeline/ABI, **REFERENCE** old heap implementation |
| xboxrecomp `f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`, `src/kernel/guest_vmem.{c,h}` | MIT, copyright 2026 sp00nz; preserved in `LICENSES/xboxrecomp-MIT.txt`; no xemu-derived code | Per-4KiB owner/state, 64KiB reservation alignment, owned commit/decommit, exact release. No earlier Vita heap | Adapt ownership/rounding model to ceded BSS, bounded metadata and existing ownership lock; replace Windows mappings and native pointer arithmetic. Medium integration/race risk; strict flags/RW-only profile | Reuses established page ownership algorithm and independent runnable oracle; bounded metadata, no new backing or allocation in calls; performance unmeasured | Run original hash-verified C with bounded host shim; seven actual translated traces compared, missing/extra/changed controls fail. **ADAPT** |
| xboxrecomp same pin, `src/kernel/kernel_memory.c`, `kernel_bridge.c` | MIT except separately identified NOTICE paths not imported | Native VirtualAlloc/VirtualFree and guest bridge | Fixed host VMA/Windows APIs cannot link to Vita; additional flags accepted upstream are not silently adopted | Useful error/rounding comparison; no direct performance inference | Explicit extra-bit negative control exposes upstream acceptance of 0x3001; Vita rejects it. **REFERENCE**, **REJECT** direct linking |
| xboxrecomp same pin, `tools/conformance/{cases,harness,test_conformance}.py`, `tools/abi_analysis/analyzer.py` | MIT in inspected files | 197 authored cases, x87 live-depth/XMM capture; existing Vita tests cover smaller FP subset | Stock runner can skip without MSVC/Docker and GPR comparisons only check EAX. Analyzer infers conventions heuristically and needs private metadata | Future x87-depth cases can reduce test authoring; not a proven current runtime bug or authoritative ABI oracle | No silent-skip stock CI integration. New bounded VM differential gate is integrated instead. **REFERENCE**, future selective **ADAPT** |
| Xita `896d6512b6d8c11279de927284e23f86d674bd2d`, `runtime/xv_bc_layout.h` | GPL-3.0-only, copyright 2026 Xita contributors; NOTICE and source hash retained | Row-major Xbox DXT blocks to GXM Y-first block order; no previous Vita GPU layout adapter | Adapt pure layout into checked extents/ownership API, 1..4096 power-of-two levels, 8/16-byte blocks. Low integration risk; incorrect block order would corrupt textures | Reuses tested layout logic without decoding compressed pixels or importing runtime; no GPU/throughput claim | 162 independent coordinate-oracle cases, fixed order, range/overflow/overlap rollback; host/sanitizers/ARM and Vita compilation. **ADAPT** |
| Xita same pin, `runtime/xv_texture_decode.h`, `xv_d3d.{c,h}`, `xv_shader.{c,h}`, `xv_render_profile.c`, `xv_benchmark.c` | GPL repository; THIRD_PARTY explicitly excludes tracked game-derived material | Draw packets, variants, scene ownership, decoding, instrumentation | Raw guest pointers, Halo-sized rings, fiber ownership, format/channel differences require an explicit TSFP adapter; pump spans are not GPU timers | Useful future integration patterns; wholesale import would add risk/duplicate tested runtime | No game captures/shaders copied. Need authored state/shader/texture proofs and device timing. **REFERENCE** |
| Xita same pin, `runtime/xv_gpu_upload.c`, `tools/ps_pipeline.py`, `recompiler/{dx8_shader_parse,dx8_pixelshader_parse,shader_recomp_gen,pixelshader_recomp_gen}.py` | GPL tooling; game shader outputs are not reusable merely because tracked | Cache/upload and shader conversion | No-op flush assumes uncached uploads; DSB is not cached-RAM flush. Pipeline depends on Halo capture hashes; shader fallbacks for unsupported DOT/BRDF need fail-closed tests | Avoid adopting incompatible memory/coherency assumptions. No production shader compiler selected | **REJECT** direct upload/pipeline reuse; parsers **REFERENCE** pending authored shader cases. No proprietary compiler dependency introduced |
| Halo `79a3d1c28c7ac5312a85f2e04ab022d86d383a15`, `port/vita/platform/{d3d8_gxm,vita_textures,nv2a_psh_cg,nv2a_vsh_cg}.c`, `port/vita/host/{vita_gxm,vita_shader_cache}.c` | GPL-3.0-only root declaration, no files copied | Independent confirmation of compressed block order, native submission and versioned shader-cache checks | Game-specific state/memory limits are not TSFP policy; full renderer remains unselected | Confirms layout convention and future cache keys; Halo FPS is not TSFP evidence | Asset-free layout proof only; native renderer/GPU acceptance requires device. **REFERENCE** |

## Deliberate allocation differences

The bounded profile uses explicitly ceded, non-executable image BSS; it is not an
additional RAM manager or a model of the entire Xbox virtual address space. The
existing image arena checker excludes initial stack/TIB/TLS/object/import/template
roles, and shared ownership guards exclude thread storage/raw workers. Configuration
pins image backing and permissions. Reserve and commit are distinct; decommit
revokes permission and recommit zeroes newly committed pages. Recommit preserves
already-committed bytes. Only PAGE_READWRITE and exact supported flag combinations
are accepted; fixed hints outside the arena (including RAM mirrors) fail.

Original xboxrecomp's successful release reports its freed extent; the pinned PC
bridge clears base/size to zero. The differential tool explicitly normalizes only
this successful-release output. All statuses and other outputs are compared
strictly. The host shim tests page bookkeeping, not Win32 VM or console behavior.
No second guest backing, privileged mapping, general virtual-address reservation,
large-reserve clamp, or fake commit/decommit success is introduced.

## Retail integration and measurement gate

No supported `default.xbe` or matching analysis metadata was found in the existing
ignored `private/` / `game-data/` setup locations. Do not download them. The user
must supply `private/game/default.xbe` and matching analysis inputs for the pinned
PC pipeline; verify US SHA-256
`3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc` before generation.
Existing retail-generation verification remains mandatory. Ceded arenas cannot
be inferred from zero-filled retail BSS; an authenticated layout/ownership plan
is required before enabling allocation in the real title. No source audit,
synthetic test or VPK establishes retail boot, GPU rendering or Vita FPS.

Follow-up reuse: event publication reuses this project's existing durable C-scope
guard design from thread_service.c; it does not introduce a second recovery or
thread engine. Generation-checked native allocation references protect event
outputs. Thread-create/VM output integration remains separate.

Issue #2 reuses the existing heap reference registry and thread-service CallGuard,
keeping pinned PC ordinal255/40-byte ABI and the server-owned creation packet.
No new external source/dependency or competing execution guard was introduced.

Issue #3 reuses the existing kernel output scope guard, heap pins and original
xboxrecomp differential trace. No new upstream pin/copy or second VM engine.
The supported profile is failure-atomic with retained result storage, not an
atomic two-word read for concurrent consumers. Self-release/decommit stays rejected.

Issue #4 intake reuses tracked PC tools/compiler/xbe_image.py at unchanged
759f7a0 on a pre-hashed byte snapshot. The original summary-basename check is
not build authentication; local provenance binds all five actual recompiler
analysis inputs to supported XBE/source hashes, with an explicit attestation
limitation. No new parser, analysis binary, retail code or external dependency
is copied. Actual generation and first-fault tracing require missing inputs.
