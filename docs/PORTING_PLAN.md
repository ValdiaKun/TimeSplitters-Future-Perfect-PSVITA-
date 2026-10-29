# PS Vita Porting Plan and Technical References

**Project:** `ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-`  
**Source upstream:** [TimeSplitters PC](https://github.com/ValdiaKun/timesplitters-future-perfect-pc)  
**State:** native diagnostic and synthetic guest test VPKs cross-build; 122 PC-translated synthetic x86 functions execute on ARMv7 under QEMU. **No retail game runtime, renderer or gameplay. Hardware unverified.** See [STATUS.md](STATUS.md) and [VALIDATION.md](VALIDATION.md).

## Reference projects and applicability

| Reference | Verified architecture / practice | Adaptation to TimeSplitters | IP caveat |
| --- | --- | --- | --- |
| [Halo CE Vita](https://github.com/BirchWoodGod/halo-ce-vita) | Native ARM Xbox decomp, Direct3D-style graphics replacement, Xbox data files loaded at runtime, Vita control mappings, performance tuning | Closest Xbox-era FPS reference: investigate runtime memory, texture format conversion, geometry semantics, frame pacing, HUD and lifecycle | GPL-3.0: do not paste its source into this repo without a conscious license decision and preservation of notices |
| [THUG Vita](https://github.com/iwannagooutside/thug-vita-port) | Builds a native ARM Vita port from Xbox-oriented engine source; vitaGL backend, targeted C++ compiler compatibility fixes and per-file optimization; original game data separately supplied | Good for incremental boot path, Vita I/O, controller translation, crashes/logs, profiling and asset staging | No root LICENSE file identified; inspect authorship and specific file permissions before copying any code |
| [MH3 Wii/Vita](https://github.com/Grarak/mh3-wii) | Static recompilation, frame-packet rendering, native libgxm, compressed GPU textures, render-thread scheduling, offline shader banks, PGO/ThinLTO and profiling | Good performance/porting playbook when our own Xbox renderer and ARM guest code exist | Wii PowerPC/GX code is not compatible with Xbox x86/D3D8; check its GPL license before any direct reuse |
| [TimeSplitters PC](https://github.com/ValdiaKun/timesplitters-future-perfect-pc) | Original Xbox x86-to-C pipeline, HLE guest services, shared gameplay/input policies, native PC render backends | The required game-specific upstream; design explicit host interfaces and pin commits for future integration | Preserve existing upstream attribution and patches; no retail game code/assets here |

See [MH3's optimization index](https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/README.md).

**Renderer decision is not made.** THUG demonstrates vitaGL; the inspected Halo revision uses direct GXM; MH3 demonstrates direct libgxm with offline shaders. We should benchmark an asset-free subset of TimeSplitters D3D8 operations, including render-target feedback and depth behavior, before committing to vitaGL versus direct libgxm. The earlier bootstrap's vita2d screen is intentionally not the final renderer.

## Work boundaries

- This new GitHub repository is the **port's work repository**. Its `main` is separate from the Windows/Mac PC project; use task branches and PRs.
- Prefer integrating the existing PC source as a **pinned dependency or reproducible source import**, not a copied diverging engine snapshot. Record upstream commit and license notices.
- Do not modify or ship `vendor/ts3FPrecomp` or retail Xbox files in this repository until source, license and build integration have been audited.
- Keep game-owned executable, extracted files, textures, video/audio, caches, local logs and saves outside of Git and outside of released VPKs.
- No external agent has unlimited or perpetual rights: repository changes occur through the connected account's authorized actions. **Merges and releases require explicit review**.

## Milestone acceptance

### M0 — standalone VitaSDK bootstrap (ongoing)
- [x] Isolated VitaSDK/vita2d diagnostic source, two analog sticks, provisional action flags.
- [x] Deterministic host tests and CI workflow authored.
- [x] Original input CI execution verified (run `40715066565`); new CI results tracked in PR #1.
- [x] VitaSDK SELF/VPK cross-build verified for diagnostic and synthetic guest harness.
- [ ] Physical Vita/PS TV boot, controls, readable UI and clean exit verified.

### M1 — ARMv7 compatibility harness
- [x] Identify and pin PC upstream commit and generated-code/build requirements (`sources.lock.json`, `external/pc`).
- [ ] Build *asset-free* CPU fixtures on ARMv7; check size/alignment, little-endian layout, x87/SSE lowering, flags, exceptions, atomics and guest pointer validity.
- [x] Prototype 64 MiB RAM views and separate contiguous/tiled backings without booting game; stack/TLS/atomics have synthetic coverage. Generated scalar RAM-wrap lowering has synthetic coverage; unsupported pointer escapes and real guest thread entry remain open.
- [ ] Execute tests on an actual Vita or ARMv7 equivalent; don't treat host x86 test results as ARM validation.

### M2 — system services and title boot
- [ ] Loader/runtime bridge, file access, timing, Xbox kernel HLE, input delivery and save isolation.
- [ ] Game boot reaches a reliable title loop using user-supplied verified USA Xbox disc files.
- [ ] Produce bounded trace and crash reports; distinguish UI boot from playable controls.

### M3 — GPU/graphics
- [ ] Audit PC D3D8/RHI contracts, native texture/surface/vertex state and shader requirements.
- [ ] Pick vitaGL or libgxm based on measured constraints and correctness.
- [ ] Render basic triangle, textures, alpha/depth/blend, Xbox texture conversion, render target feedback, HUD then 3D model scenes.
- [ ] Batch draw packets and precompile required shader variants where possible.

### M4 — gameplay, correctness and performance
- [ ] Menu, first level movement and fire, load/save, audio/music, cinematics.
- [ ] Test later missions and multiplayer separately; publish only verified functions.
- [ ] Profile actual GPU/CPU times and memory; add worker threads, native hot functions, culling, streaming, compressed textures and LTO only after establishing baseline.

## Practical local build

For the diagnostic only (requires separately installed VitaSDK and vita2d):

```sh
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S ports/vita -B build/vita-bootstrap -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build/vita-bootstrap
```

Host input test (requires C compiler only):

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  ports/vita/src/input_map.c ports/vita/tests/test_input_map.c \
  -lm -o /tmp/tsfp-vita-input-test
/tmp/tsfp-vita-input-test
```

This bootstrap **does not run the game**. The final port cannot be declared runnable until native ARM guest execution and Xbox graphics/system services have been integrated and tested on hardware.

## Current foundation gates (A/B)

The older M0–M4 labels above remain roadmap groups. Milestone A's cross-build
and deterministic input gates pass; hardware platform validation remains open.
Milestone B has a passing ARMv7 synthetic subset, not full runtime acceptance.
RAM views and MMIO classification now have host/ARM coverage. Atomic flag coverage, device behavior, complete flags/FP behavior, exception
delivery, title thread entry and retail dispatch coverage remain open. The
renderer (C) requires device comparison of libgxm/vitaGL triangle, textures,
blend/depth and render-target tests before selecting a production backend.
Boot/services (D) and optimization (E) require verified user files and hardware.

Reviewed reference revisions and licensing constraints are in
[REFERENCE_AUDIT.md](REFERENCE_AUDIT.md). No baseline depends on kubridge,
CapUnlocker, clock changes or an on-device shader compiler.

Generated scalar integration now uses the real PC translator with a typed AST
emission adapter; tests cover 122 synthetic functions on the host/ARM matrix.
Bulk/string pointer escapes, complete instruction/flags/FP/exception coverage,
kernel services and title entry remain open. Retail source generation and all
hardware gates still require their explicit prerequisites.

REP/string execution now has per-element fault/progress and native-x86 reference
coverage (1,518 scenarios; 122 total translated fixtures). Prefix/flag provenance
patterns without a proven adapter still fail closed. Aligned 32-bit atomic flags now have native-x86 reference coverage. Next CPU
gate: exception/thread entry; game/kernel/render/hardware gates remain open.

Execution foundation now has nested C-only fault capture and explicit thread/TLS
entry tests (122 translated functions total). Full B acceptance remains open:
Xbox SEH delivery, thread objects/scheduling, arbitrary cross-call/join flags and
FP fidelity are not established. Continue the concrete
[TITLE_BOOT_DEPENDENCIES.md](TITLE_BOOT_DEPENDENCIES.md) integration order.

Before further title services, gate on the REP comparison-fault correction passing
both CI triggers on one revision. Intel fault semantics, not AMD native behavior,
define this Xbox guest subset. Thread objects/imports remain the next boot task.

After both regression CI triggers passed, added a narrow thread-object/import
bridge and real translated synthetic entry. It covers identity, TLS, three real
services and termination only. Production handles/scheduling, full imports and
bounded image loading remain open; no retail title or hardware gate is complete.

The synthetic image initialization gate now has source implementation and a full
host/ARM correctness matrix. R/W/execute guards and rollback tests cover the
bounded low-RAM subset; this is not retail boot or full M2 acceptance. Next:
normalize bounded synthetic XBE headers/sections into the descriptor, preserving
hash/analysis prerequisites for eventual retail generation.

Bounded raw XBE header/section/key/import normalization now feeds the synthetic
image path and has PC-parser comparison. Remaining boot gates include actual TLS
directory/index preparation, object/handle services, supported authenticated title
metadata, full dispatch and hardware validation. This does not complete M2.

TLS directory extraction, bounded allocation/index/zero-fill policy and worker
isolation now have synthetic source/PC/ARM coverage. This does not complete B/D:
non-empty callbacks, guest lifecycle/handles/waits, full flags/SEH, retail entry
and hardware checks remain open. Proceed to explicit thread ownership contracts.

Thread ownership now has a bounded generation-checked registry, strict NtClose
and live-image teardown protection, including close/finish races and exhaustion
coverage. This completes only the ownership prerequisite, not guest thread
creation or synchronization. Next acceptance: waitable completion and explicit
platform launch/join integration, preserving independent TLS and rollback.

Non-alertable zero-timeout thread-completion polling (ordinal 233) is now
implemented through the translated call path. All blocking/alertable modes
fail closed. Next gate remains lifetime-safe blocking completion references
and a platform creation/join adapter, not additional unrelated CPU fixtures.

### Blocking completion prerequisite — 2026-10-29

Added non-alertable relative/infinite thread waits, monotonic deadlines, atomic
waiter references, wake-all completion and close-versus-wait lifetime rules.
Owner release/teardown cannot recycle referenced records. Positive absolute
deadlines and alertable waits remain explicit faults. Native worker launch/join
is the next bounded adapter; no guest scheduler or retail boot is claimed.

### Native worker launch/join subset — 2026-10-29

Implemented a 32-record host worker adapter, private prepared guest storage,
trusted entry resolution, two start contexts, CPU/fault/exit capture, backend
stack ownership, create rollback, retryable join and a distinct native pin.
Handle close/completion cannot release storage before join. Callers still
prepare guest TLS/object/stack storage; no PsCreateSystemThread import or retail
scheduler is claimed. Next: bounded guest storage reservation/rollback, followed
by a safe thread-creation service boundary. Retail input/metadata and physical
Vita remain unavailable; no renderer/gameplay/performance milestone is met.

### Transactional guest storage — 2026-10-29

Added an explicitly ceded BSS pool, bounded to 32 slots/1 MiB, with aligned
stack/TIB/TLS/object layouts, generation leases, shared raw-worker/handle overlap
guards, unique identities and image pins. Initialization/native-create failures
restore prior metadata bytes; joined, closed, unreferenced workers alone can
release managed storage. Tests run three translated workers and cover mirror
conflicts, exhaustion, stale leases, competing reservations and concurrent teardown.
This is not automatic allocation from arbitrary retail BSS. Next: a controlled
thread-creation outcall/import boundary with checked ABI and output publication,
without longjmp across native resource owners. Retail files/metadata and hardware
remain unavailable; no game boot or gameplay/performance claim is made.

### Guest thread-creation import subset — 2026-10-29

Implemented ordinal 255 with checked 40-byte argument ABI and a bounded native
service thread owning reservation/create/join work off the guest recovery stack.
A durable C-scope bookkeeping guard releases service references on probe faults;
valid outputs/EAX/ESP publish before guard release. Real translated parent/child
fixtures create, validate contexts/identity, wait and close. Two concurrent
callers, native resource errors, exhaustion and safe shutdown/retry are tested.
126 authored translated functions now exist. Unsupported suspension/debug/extra
bytes/TLS-size modes return errors; this is not full Xbox threading or retail
boot. See HANDOFF.md for setup/teardown and restrictions. Next independent gate:
bounded event handles and NtCreateEvent/Set/Clear plus event waits, using the
pinned synchronization contract. Verified retail input/analysis and Vita hardware
remain required to identify title-specific blockers and measure gameplay.

### Event acceptance progress

Bounded manual/auto-reset event objects and translated 189/225/186/233/187 calls
now pass local host/reference/sanitizer/ARM testing, including lifetime and
timeout races. This does not establish a complete Xbox synchronization subsystem.
Next bounded compatibility task: validate empty unnamed ObjectAttributes without
adding speculative named-object support. Retail boot remains a separate gate.

Empty unnamed event attributes are now supported through checked 12-byte guest
decoding; named attributes remain unsupported. Next boot prerequisite to inspect:
bounded guest heap/allocation services using explicitly ceded storage and the
pinned allocator/import contracts. Retail traces remain unavailable.

### Reuse acceleration gate

Implemented bounded guest allocation using the audited xboxrecomp page model,
with actual translated imports, checked ownership and independent reference
traces. Small Xita compressed-block conversion is the first isolated graphics
compatibility proof. Neither establishes title boot or renderer choice.
Next priority: lifetime-safe heap-resident kernel outputs, with allocation pins
across event/thread publication and deterministic close/free/rollback tests.
Then use an authenticated retail layout/trace to select further services.

Heap-resident event result publication is now reference-protected and tested.
Next: extend the existing thread-creation service's durable guard to retain its
heap-resident handle/ID output allocations, with native-create failure, publication
fault and concurrent free tests. VM in/out words remain a separate guarded profile.
