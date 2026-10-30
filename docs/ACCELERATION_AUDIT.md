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


Issue #6 telemetry: REFERENCE Xita896d6512 runtime/xv_render_profile.c
(single render-owner state, no per-frame logs and explicit elapsed-not-GPU warning);
REFERENCE Halo79a3d1c port/vita/platform/vita_memory_watch.c (generation tracking,
not a general memory-capacity probe). No code copied or new source pin introduced.
The original GPL recorder adds bounded loss reporting, overflow-safe counters and
synthetic-clock tests; it does not import Halo memory assumptions or Xita pump
state. VitaDebugger/Vita3K are not installed or validated by this change.


V5 issue #7: REFERENCE pinned prepared runtime src/platform/xbox_winnt.h BOOLEAN
uint8_t declaration, src/kernel/kernel.h NtWaitForSingleObject BOOLEAN signature,
and src/kernel/kernel_bridge.c explicit cast/ordinal23312-byte ABI. Existing
Vita import used a full-width truth test, independently reproduced via a real
translated CALL with0x100. Adapted the established low-byte marshaling convention
already used by event InitialState. No foreign source copied or pin changed.
Added512 positive/negative byte combinations without adding synthetic functions
or expanding unrelated CPU coverage. Other ABI/FP/title gaps remain separate.


Fault trace export REUSEs this project's existing guest/execution.{h,c} captured
instruction marker/CPU snapshot and strict JSON/address helpers in
check_title_inputs.py. REFERENCE unchanged PC tools/compiler function metadata
(start/end hex extents) and existing prepared translator instruction markers.
Original GPL adapters only; no foreign code, new parser dependency, source pin,
retail disassembly or game label is imported. Heuristic stack scans/nearest
symbols are REJECTED as misleading first-fault evidence. Unknown map provenance
is explicit; no claim of authenticated title execution follows from mapping.


Guest-entry timing REUSEs existing execution.c run/capture/cleanup and bounded
telemetry recording. Original GPL adapter, no foreign source copied, source pin,
platform privilege or alternate scheduler/recovery engine introduced. Reference
Xita's previously audited elapsed-not-GPU distinction remains applicable. Native
clock acquisition is injected outside recovery/resource ownership; failed timing
is explicit and never replaces real execution. Host/Vita timing is not inferred
from deterministic fake clock values or QEMU.

## Same-game comparison — 2026-10-30

Only repository metadata, licenses and bounded authored source were inspected;
no executable, retail-derived translation, game shader, analysis database or
asset was acquired. Executable identities below are **source statements**, not
newly authenticated files. Existing PC/runtime/toolchain pins are unchanged.

| Project / exact revision | License / identity | Startup, import and discovery observations | Graphics/audio / decision |
| --- | --- | --- | --- |
| Pinned PC `759f7a0ecebc2341cdcedf638fcea68c4a7fbf4e`, prepared `d90c92988a9f99d0580d2ca3f7efd6ec3a7b7845` | Existing GPL/MIT/LGPL notices; approved US SHA-256 in TITLE_INPUTS.md | Existing authoritative parser, compiler/ABI metadata and kernel bridge. Vita adapts bounded image ownership and checked services; genuine dispatch/layout still needs original inputs | Existing title-specific RHI/HLE is authoritative; no production Vita backend selected. **REUSE** contracts/pipeline |
| NeilSeligmann/tsfp-recomp `dd3f062cdd7f6b7a35e15185dd14ee1b046ae64f` | Root GPL-3.0; inspected authored kernel files GPL-3.0-or-later. README states NTSC-U SHA-256 `3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc` | `docs/ARCHITECTURE.md`: xdvdfs -> XBE -> vendored xboxrecomp lifting -> authored loader/kernel -> stop report. `src/xbox/kernel_{call,thread,event,sync,memory}.c` separates guest argument marshaling/services. Thread source also tests full BOOLEAN words: **not** an independent oracle for that behavior | Linux-oriented Vulkan and DirectSound host. Useful title-specific comparisons; do not link incompatible host/runtime or assume all services work. **REFERENCE** |
| lmplied/TSFPRecomp `06640ec503e0b18e808087c7f5f365551739dcf7` | Root GPL-3.0; NOTICE explicitly identifies MIT xboxrecomp fork and component exceptions. README states USA/NTSC SHA-1 `daf0bcc57b6e93a7ebf5498074c8a9e7dc90381e`; cannot equate to approved SHA-256 without the same actual file | `titles/futureperfect/src/main.c`: memory -> kernel -> paths -> bridge -> APU -> dispatch/binary-search fallback -> entry. README requires XbSymbolDatabase/xdk_symbols before generation to recover imports; entry constant `0x38024d` is reference data, not an instruction to bypass authenticated entry. `src/kernel/kernel.h` is byte-identical to the pinned prepared header: shared ancestry, **not independent agreement** | D3D8-to-D3D11, DirectSound/software mixer/waveOut and xemu APU/NV2A components; Windows pointers/mappings do not fit Vita. **REFERENCE**, **REJECT** whole runtime or legacy parser replacement |
| phobos665/Split-Again-Recomp `217bb24e37d9688a70a6449c967dda853a321121` | No root LICENSE/NOTICE found; component licenses separate. This is **TimeSplitters 2**, title `4553000A`, not Future Perfect; no supported XBE hash established | README documents symbols and `--seeds` for indirect-pointer-only functions. xboxrecomp gitlink `af7bef370b392357ff1293b0e8c2cc378fb942ad`; main follows a shared startup template with different entry `0x1cf3c9`. No offsets, seeds or analysis imported | Methods **REFERENCE** only; **REJECT** copying root code absent license or transplanting title assumptions |

### Focused kernel compatibility matrix

This compares inspected contracts, not complete service coverage or successful
retail execution. Native desktop implementations are not automatically correct.

| Contract | Comparative evidence | Vita result / missing behavior |
| --- | --- | --- |
| 255 thread creation, 40 argument bytes | Pinned formal BOOLEAN CreateSuspended/DebugStack and lmplied's identical declaration; Neil's full-word implementation shares the observed risk | Low-byte decoding corrected; true suspend/debug modes still NOT_SUPPORTED; DWORD ThreadExtraSize remains full width; native ownership/RPC architecture retained |
| 233 single-object wait | Pinned BOOLEAN and bridge cast; newer same-game implementations are useful comparisons, not substitute hardware evidence | Existing low-byte/non-alertable wait profile retained; positive absolute/alertable modes still unsupported |
| 189/225/186 events | Pinned bridge + Neil authored event/sync separation | Existing retained manual/auto-reset handles and timeout profile retained; names/multi-object modes remain incomplete |
| 184/199 virtual memory | Desktop native allocation/reservation policies are host-dependent | Existing bounded owned arena, transactional outputs and independent xboxrecomp trace retained; no desktop pointer/clamp or fake commit copied |
| Indirect calls / import discovery | lmplied's symbol pass and binary-search fallback; TS2 seed method | Full trusted retail dispatch requires matching approved metadata. Do not merge title/build datasets or infer guessed targets |

### Implemented finding and provenance

Two independently isolated translated-call reproductions with `0x100` in
CreateSuspended or DebugStack failed the prior success assertion. The pinned
`src/platform/xbox_winnt.h` defines BOOLEAN as uint8_t and
`src/kernel/kernel.h` declares both parameters that way. The fix reuses the
already-established wait/event low-byte marshaling convention. It adds no
suspension feature, scheduler, foreign code or new source pin. Real translated
creation/wait/close, native failure rollback, output faults, true-low-byte
rejections and concurrent callers exercise the change. No title behavior is
inferred from these authored tests.

Audited source fingerprints: Neil LICENSE SHA-256
`3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`,
architecture `465756b17d5153b09100d38239eac64e020933825c9071dd8431fbbb8ef3bf6a`;
lmplied kernel.h `bd50ed42f1d17f6fbbc18b1b63c040ec222d3f2be6f9a8bdca194701c2fe1cef`
(matches pinned), title main
`cb75b540e03b828f12ce34872f6c3e2c9e142e4d5792c5c690c7fa19ff441280`;
TS2 README `6dbb988a223813179c1affbcaa6b1bd3e6d7cfbdf0cf24689aff15bffa376c72`.
All source locations are at the exact revisions above. No additional reference
is an authorization to distribute retail content or a proven Vita performance gain.
