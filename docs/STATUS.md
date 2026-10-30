# Port status — 2026-10-29

**Not playable. No retail guest initialization, graphics, menu, audio, saves or
campaign execution. Physical Vita behavior is unverified.**

## Implemented

- Continued the clean `vita-bootstrap` branch and existing draft PR #1; no main
  merge. The original bootstrap CI run `40715066565` passed at `c787e0f`.
- Pinned PC submodule/source manifest, offline runtime reconstruction and full
  authoritative patch application, reproducible ARM header adaptation.
- Checked unaligned little-endian guest memory prototype and deterministic
  generation of 127 synthetic x86 functions using the real PC translator.
- Explicit 64 MiB RAM/28 mirror address views, independent optional contiguous
  backing, tiled alias and kernel-data/MMIO/flash/invalid classification.
  Checked byte transfers support RAM wrap; unsupported device accesses fault.
  Linux shared-map PC-header oracle tests translated accesses across all views.
- PC baseline/portable/sanitizer and ARMv7 QEMU comparison runner with command
  and provenance reports. TLS isolation, concurrent aligned atomics, call stack,
  SSE/x87 and fault subset assertions. Exhaustive controller-axis and individual
  button tests.
- Hash-verified VitaSDK installer, native diagnostic VPK and separate synthetic
  guest ABI test VPK targets; structured timestamp/event/error logs to stderr,
  initialization checks and controller failure/recovery handling.
- Host input CI retained; new CPU/QEMU and Vita cross-build CI. Renderer/reference
  license audit and reproducible build instructions.

## Validation

See [VALIDATION.md](VALIDATION.md) for commands, tool versions, failures resolved
and exact scope. Local host, sanitizer and ARMv7 QEMU checks passed; both VPKs
cross-built. The expanded memory suite passed locally on the PC shared-map oracle,
portable host, ASan/UBSan and ARMv7 QEMU. GitHub Actions CPU/Vita run
`40721699619` and host input run `40721699935` passed at code commit `9a48db1`.
See the linked validation record for earlier evidence and scope. VPK creation
is not on-device execution. No hardware behavior or FPS result is confirmed.

## Partial and blocked

- A: reproducible cross-build and deterministic host tests implemented. Vita
  installation, display, input, lifecycle and diagnostic log delivery await hardware.
- B: narrow synthetic CPU subset passes ARM32 execution under QEMU; complete generated instruction/flag/exception coverage and
  title layout/services, all flags/FP modes, exception delivery, real guest
  threads and full dispatch are incomplete. Acceptance is not complete.
- C: reviewed direct GXM versus vitaGL references; production backend undecided.
  No GPU benchmark/device, scene capture or working game renderer exists.
- D: no supported US Xbox data supplied. No title initialization/HLE/audio/video/
  saves integrated. Game tests must use isolated saves and verified user files.
- E: no working gameplay/hardware profile; performance optimization is premature.

## Handoff

Start with `python3 tools/test_guest.py --arm-cc arm-linux-gnueabihf-gcc` in a
checkout with the pinned submodule initialized. Read [GUEST_RUNTIME.md](GUEST_RUNTIME.md).
Generated scalar load/store lowering now passes the documented synthetic matrix.
Next: thread objects/kernel import boundary and Xbox exception delivery; see
[TITLE_BOOT_DEPENDENCIES.md](TITLE_BOOT_DEPENDENCIES.md). On physical Vita, separately
validate bootstrap installation, both sticks/buttons and Select+Start exit;
collect synthetic guest test results before connecting title boot. Never mark a
menu, diagnostic or synthetic CPU test as proof of gameplay.


## 2026-10-29: scalar boundary access extension

- Existing 64 MiB/28-mirror address space, contiguous/tiled alias and
  MMIO/device classification remain intact; no submodule/toolchain pins changed.
- Added typed unsigned/signed integer and floating-point split-safe
  read/write helpers with whole-range validation and bit-preserving copies.
- Tests cover 28 RAM-view transitions with all 1–7 byte splits of 64-bit
  scalars, native mapped-PC oracle writes, smaller signed/unsigned scalars,
  NaN payload and negative-zero preservation, kernel-data/contiguous
  cross-classification, final-view rejection and MMIO/flash/invalid failures.
- [CPU + Vita cross-build 40722808711](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40722808711):
  **both jobs passed** at `f778686d`. [Host input 40722808716](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40722808716)
  passed at the same revision.
- Scope: source, host/sanitizers, ARMv7 QEMU and Vita cross-compilation.
  No physical Vita, retail title, GPU or FPS validation was performed.
- Existing translated `MEM*` lvalues still fault on valid wrapped scalars.
  Generator lowering is the next correctness gate.

## 2026-10-29: generated scalar integration

- Real PC FunctionTranslator emission is adapted through `tools/guest_codegen.py`
  and `tools/guest_lowering.py`; typed AST handling replaces raw lvalue use in
  separately generated portable function bodies. Existing helpers are reused.
- Added MMX/SSE whole-range memory adapters, atomic alignment/span checks and
  tested PUSH/POP commit ordering. Source adapters repair pinned MOVSD lane
  stores and ADD/SUB overflow consumers; the PC source pin is unchanged.
- 35 translated fixtures and seven source-pass tests pass locally with PC mapped
  oracle, portable host, ASan/UBSan and Cortex-A9 ARMv7 QEMU. VitaSDK Release
  cross-build of both VPKs passes. Code commit `c3da8a1` passed CPU/ARM/Vita CI
  `40727117655` and input CI `40727117635`. Follow-up store-path assertions
  also pass locally and in CPU/Vita CI `40727763246` at `bc7aa80`
  (input CI `40727763221` passed too).
- Current old paragraphs describing pointer-only generated scalar lowering are
  historical; see the new contract section in GUEST_RUNTIME.md.
- No supported US XBE or physical Vita is available. Retail generation, full
  runtime/gameplay, GPU/device services, heap availability and FPS remain unverified.
- Next: lower REP/string native pointer escapes with explicit guest memory
  semantics and oracle tests; keep unknown escapes fatal at generation time.

## 2026-10-29: REP/string execution

- Added `guest/string_ops.{h,c}` and `tools/string_lowering.py`; MOVS/STOS/CMPS/
  SCAS/LODS at 8/16/32-bit widths execute one validated element at a time.
  REP(E)/REPNE, CLD/STD, partial progress, overlap and zero count are tested.
- The real PC translator now generates 103 asset-free functions. 1,518 data
  scenarios plus translated LAHF/SETO, Jcc, ADC and incoming-CMP consumers pass
  locally on mapped PC, portable host, ASan/UBSan and Cortex-A9 QEMU. Linux x86_64
  also executes actual string opcodes and captures real fault registers/flags.
- VitaSDK Release diagnostic/guest VPK cross-build and strict input tests pass.
  CPU/ARM/Vita CI [40799068560](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40799068560)
  and input CI 40799068641 passed at `b981d94`. No pins changed.
- Known limits: 16-bit address-size and unimplemented segment/duplicate prefixes
  fail generation; unproven incoming arithmetic flag provenance fails generation.
  The flags bridge is not complete guest EFLAGS/SEH or thread initialization.
- No real title files, Vita hardware, GPU tests or measured FPS are available.
  Next: accurate 32-bit XADD/CMPXCHG flags with native-x86 comparisons.

## 2026-10-29: aligned atomic arithmetic flags

- Added `tools/atomic_lowering.py`, shared condition expressions, and eight real
  translated fixtures (111 total). Aligned dword memory XADD/CMPXCHG retain the
  pinned atomic primitives, snapshot aliased operands before updates, publish
  width-correct arithmetic flags, and feed LAHF/SETO/Jcc/ADC/string consumers.
- 294 arithmetic cases compare registers, memory and flags against independent
  arithmetic and actual native x86 opcodes. Fault-state, address alias, TLS and
  two-worker mirror increments are tested. Unsupported widths/forms fail closed.
- Host oracle, portable host, sanitizers, Cortex-A9 QEMU and Vita cross-build
  pass locally. Remote checks are recorded after push. Physical Vita remains
  unavailable; no gameplay, allocation-capacity or performance claim is made.
- Next: deterministic exception recovery and real guest thread-entry contracts;
  complete flags across calls/complex joins and x87 precision remain open.

- Follow-up oracle portability finding: GitHub native bulk REP disagreed on
  physical synonyms on one runner and passed on another. Cross-view MOVS native
  references now sequence real single elements; ordinary REP remains native.
  See VALIDATION.md for failing run, addresses and byte evidence. Generated
  guest behavior and the complete byte-model assertions remain unchanged.

Final implementation `180a0a7`: [CPU/ARM/Vita CI 40800471487](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40800471487)
passed both jobs; input CI 40800476515 passed. Includes the alias-oracle fix.
No physical Vita execution or performance measurement was performed.

## 2026-10-29: execution boundaries and thread/TLS foundation

- Added `guest/execution.{h,c}` with nested thread-local C-only recovery,
  CPU/FP/vector snapshots, guest instruction sites, memory access metadata,
  divide/dispatch/exit classifications and explicit thread attach/detach.
- Bounded TIB/TLS preparation follows pinned PC layout (own SEH head, copied TLS
  template, own zeroed thread data). It does not create Xbox thread objects or
  implement SEH delivery. Native thread creation remains a platform responsibility.
- Six additional real translated fixtures (117 total), two workers and repeated
  faults test independent CPU/TLS/recovery, stdcall arguments/return, fault ordering,
  invalid indirect calls and flag preservation through a MOV-only callee.
- EFLAGS snapshots include a validity mask: unpublished arithmetic state is
  explicitly unknown. Arbitrary call/join flags are not solved. Existing legacy
  oracle behavior remains outside the new execution boundary only in test support.
- Host PC, portable, sanitizers, Cortex-A9 QEMU and Vita cross-build results are
  recorded in VALIDATION.md. No hardware, retail boot or performance measurement.
- The overall exception/thread milestone remains partial. Next: thread-object and
  kernel-import contracts from the boot dependency map; SEH delivery is separate.

Execution implementation `a8e96ec`: [CPU/ARM/Vita CI 40802669954](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40802669954)
passed both jobs. Input CI 40802669848 passed. Push CI 40802663739 also passed.
Scope remains synthetic execution and cross-compilation; no hardware/game/FPS evidence.

## 2026-10-29: REP comparison fault-flags correction

- Corrected a real guest semantic defect: Intel REP CMPS/SCAS faults restore
  instruction-entry flags, with completed pointer/count progress preserved.
- Added a standalone native reproduction and CPUID diagnostics. Local AMD retains
  completed comparison flags; Intel specifies entry restoration. Native reference
  expectations are explicit and strict by vendor; guest always follows Intel.
- This supersedes the earlier observed-PC conclusion that last comparison flags
  were the Xbox contract. No defined flag comparison was weakened. See VALIDATION.
- Full local host/sanitizer/ARM matrix and Vita cross-build pass. New runtime work
  waits for both push and PR CI on the fix. No game/hardware/performance claim.

REP correction `09a1431`: push CI 40804082165 and PR CI 40804088172 both passed
CPU and Vita jobs on the same revision; input CI 40804088185 passed. Push probe
reported AMD signature a00f11 with hypervisor bit set and flags 44. The failing
old run did not record CPUID; its entry-restored flags match Intel's specified
behavior, but its vendor cannot be established retrospectively from those logs.

## 2026-10-29: thread-object and import subset

- Added `guest/kernel.{h,c}`, real translated synthetic entry/call functions
  (119 total), and `tests/kernel_suite.c`.
- Template-based thread objects expose fs:[0x28] and ID+0x12c. Physical aliases
  cannot overlap TIB/object storage. Caller retains storage/lifecycle ownership.
- Three actual kernel adapters: current thread, checked whole-range zeroing and
  captured termination. Import table setup and stdcall marshaling are bounded;
  unknown ordinals fault with metadata. Pinned layout/ordinal/ABI anchors checked.
- Tests cover end-to-end entry/TLS/object/import/memory/exit plus unsupported
  imports, rejected rebinding, argument faults and nonmutating destination faults.
- This is partial boot infrastructure: no object handles, wait primitives,
  production scheduling, image section loader/permissions, full imports or retail
  entry. No Vita hardware, game data/metadata, GPU test or performance result.
- Next: bounded section/import initialization with a synthetic image and dispatch
  entry validation; then adapt the next genuinely required kernel/object service.

Final runtime implementation `d5952d4`: push [40804967536](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40804967536)
and PR [40804973727](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40804973727)
both passed CPU/ARM and Vita jobs. Input CI 40804973827 passed. PC baseline,
portable and ASan/UBSan executables additionally passed three repeated local runs
each. Push probe identified AMD a10f11/hypervisor=1, flags44. No device measurements.

## 2026-10-29: bounded synthetic image initialization

- Added `guest/image.{h,c}`: up to 16 low-RAM sections and 1 MiB total staging,
  checked file/BSS initialization, virtual/physical alias overlap rejection,
  import-table/TLS/object/stack preparation and trusted entry resolution.
- Input validation and allocation finish before guest publication. Invalid inputs
  preserve existing RAM/CPU/import state. Memory and import policies publish only
  after commit; setup/reset require exclusive, quiescent bindings.
- Added checked R/W/execute permissions, alias-consistent guards, translated
  instruction-site checks, one-shot entry, teardown and policy-aware REP faults.
- 120 translated functions; 27 adversarial/OOM rollback cases plus complete
  initialized-image execution to status 37 pass local host/sanitizer/ARM matrix.
  Vita cross-build evidence is in VALIDATION.md. Physical hardware unverified.
- Explicit limits: normalized synthetic metadata, not an XBE parser; low RAM only,
  no RWX/self-modifying code or executable BSS; no full title services or retail
  boot. PC raw-lvalue oracle does not enforce Vita software write permissions.
- Next: a bounded XBE-section/header normalization adapter to this validated
  descriptor, tested with authored binary headers before retail input is allowed.

Image commit `06f876b` passed push CPU/Vita CI 40809921287 and PR CPU/Vita CI
40809933198 (both jobs); input CI 40809925583 passed before continuing.

## 2026-10-29: bounded raw XBE normalization

- Added `guest/xbe.{h,c}` and independent pinned-PC parser verification tool.
  Checked header/section extents, raw padding, canonical key pairs, import ordinal
  termination and unchanged-output failures; file buffers remain caller-owned.
- Authored retail/debug-encoded byte buffers both normalize, initialize and execute
  the real translated synthetic entry to exit 37. Read-only imports patch before
  policy publication and remain read-only during guest execution.
- 28 malformed raw-header cases plus truncation/host-output-alias rejection pass;
  all earlier 27 image rollback cases and CPU suites remain enabled.
- Structural subset only: no retail/certificate authentication, full TLS directory
  extraction, callbacks/index allocation, handles/scheduling or real game boot.
- Next: validated XBE TLS-directory/index normalization into initial-thread storage,
  checked against pinned PC behavior with authored directory fixtures.

Final implementation `d1038cb`: [push 40811313846](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40811313846)
and [PR 40811318719](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40811318719)
both passed CPU/ARM and Vita jobs; input CI 40811318711 passed. PC, portable and
sanitizer executables each passed three repeated local runs; pinned PC parser
accept/reject comparison passed. Scope remains synthetic/structural and
cross-compilation, with no physical device or retail/performance evidence.

## 2026-10-29: XBE TLS initialization profile

- Added `guest/tls.{h,c}`: file-backed directory/raw-data validation, checked
  rounding/zero-fill/index metadata, physical overlap checks and explicit callback
  policy. Null/empty tables accepted; non-empty tables and characteristics rejected.
- Normalized XBE TLS now drives staged initial TIB/block/thread-data/object state.
  Default preserves the PC title-owned index; explicit loader-write policy is
  tested. The real translated entry computes/stores its negative index and reads
  initialized/zero-filled TLS through fs:[4].
- Two translated workers test isolated TLS, CPU, stacks, thread data and object
  TlsData links. Permission/alias-invalid thread entry preserves CPU state.
- 122 translated functions, 38 malformed TLS cases with RAM/CPU/import rollback,
  empty raw ranges, both index policies and all previous tests pass locally on
  PC/portable/sanitizers/ARMv7 QEMU; Vita Release cross-build passes.
- Scope remains bounded: directory/raw/callback metadata in declared file-backed
  sections, zero characteristics, no callback execution or production scheduler.
  No retail title, physical Vita or measured performance. Next: thread lifecycle/
  handle ownership contracts before broader kernel thread/wait imports.

## 2026-10-29 — bounded thread ownership and NtClose

Implemented a fixed 32-record generation-checked thread-owner registry and
ordinal 187 `NtClose` for its registered handles. Created/running/terminated
state, exit status, close and owner release are distinct. Live owners block
image initialization/reset; stale/double/unknown closes return INVALID_HANDLE.
The actual translated TLS title entry now closes its registered handle before
termination. Native workers remain caller-created/joined; this is not a guest
thread scheduler or PsCreateSystemThread implementation. Waits remain unsupported.

TLS commit `75c8831` passed push CPU/Vita [40814706110](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40814706110),
PR CPU/Vita [40814711797](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40814711797),
and input [40814711841](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40814711841).
Next: waitable thread completion with explicit timeout and ownership contracts,
then a platform creation/join adapter. Retail files/metadata and Vita unavailable.

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

## Bounded events — 2026-10-29

Implemented typed, generation-checked manual/auto-reset events sharing the
32-record thread registry. Actual translated imports 189/225/186 create/set/clear;
233 waits on events or threads, and 187 closes either. Wait references retain
closed objects and block image reset. Per-waiter grants survive clear, and late
signals do not get consumed by expired waiters. All 126 translated functions pass
the local PC/portable/sanitizer/ARM matrix. No retail boot or hardware claim.
Nonempty ObjectAttributes, alertable/absolute waits, named events, guest-address
KeEvent objects and multiple-object waits remain unsupported.

Empty unnamed event ObjectAttributes now accept checked aligned, unaligned and
mirrored descriptors, including output/input aliasing. Nonempty metadata fails
closed; malformed reads leave handles/output unchanged. Next inspect bounded
guest heap services against the pinned PC allocation path. Retail XBE/analysis
and Vita hardware are still missing; no performance measurements exist.

## Reuse acceleration and guest allocation — 2026-10-29

- Added focused ACCELERATION_AUDIT.md with exact revisions/licenses and explicit
  REUSE/ADAPT/REFERENCE/REJECT decisions; PC remote HEAD still equals its pin.
- Adapted MIT xboxrecomp page ownership/rounding to a bounded ceded image-BSS
  heap. Actual184/199 translated calls support distinct reserve/commit/decommit/
  release with alias-safe page policy, zeroing, strict flags and output rollback.
- Added authored allocation/store/load/free function (127 translated functions),
  fourteen-section synthetic XBE, concurrent callers and independent original
  xboxrecomp differential checks with strict missing/extra/changed controls.
- Added small Xita-derived compressed-block layout proof with162 independent
  cases; it is a host/ARM layout adapter, not a GXM renderer or rendered scene.
- Heap-resident kernel output pointers remain rejected until safe publication
  lifetimes exist. No retail files/analysis or physical console is present.
  No boot, gameplay, GPU behavior, production budget or FPS is established.

## Follow-up: heap-resident event outputs

Added generation-checked output references and durable event-call cleanup.
Translated NtCreateEvent/NtSetEvent can publish into committed heap allocations,
including RAM mirror pointers. Concurrent release/decommit fail unchanged while
a result is pinned; stale/double unpins and the64-reference limit are tested.
A native test gate pauses actual translated creation and races actual translated
free/decommit; injected post-pin faults verify cleanup and cancellation restoration.
Thread-create/VM outputs in the heap remain unsupported. No retail/device/FPS claim.

## Issue #2: thread-creation heap outputs

Extended existing CallGuard with two retained allocation references; handles
and optional IDs may be in distinct committed allocations or mirror aliases.
Translated callers create, wait and close; concurrent release/decommit fails
unchanged while publication is pending. Tests cover both pin-exhaustion points,
second-pointer faults, exact/partial alias conflicts, native EAGAIN rollback,
post-pin fault cleanup and cancellation restoration. All127 fixtures retained.
VM in/out words remain guarded outside the heap pending issue #3. Retail and
physical Vita/gameplay/performance gates remain unverified.

## Issue #3: VM heap in/out words

Reused the kernel output guard for both NtAllocate/NtFree words, pinned before
reads through publication and EAX/ESP. Mixed, separate and mirrored heap/image
outputs work; destructive self-reference and physical overlap reject unchanged.
Tests cover zero/recommit semantics, pointer and post-pin faults, quota errors,
ordinary exhaustion/unsupported flags, retry, and concurrent release/decommit
of result containers. No new allocator, mapping, buffer or dependency introduced.
Authentic title boot still requires the supported XBE and bound analysis data.

## Issue #4: authenticated intake subset, retail trace blocked

Added read-only check_title_inputs.py, reusing the pinned PC XBE parser on the
exact hashed snapshot. Verifies supported US build/title, PC source identity,
local hash-bound analysis provenance, function/entry/ABI coverage and import
dispatch presence. Safe path/size/JSON controls reject malformed inputs; no game
data is downloaded, generated or executed. Thirteen asset-free tests pass.
Live preflight reports missing XBE plus provenance and five analysis files.
Issue #4 remains open: provenance is local attestation, not proof of analysis
correctness; no generation, actual title entry or fault trace has occurred.
See TITLE_INPUTS.md for exact ignored setup paths and required hashes.


## 2026-10-30: bounded diagnostic telemetry (issue #6, partial)

Added caller-owned fixed256-sample elapsed-time recording, drop/rejection
accounting, saturating typed counters and non-mutating nearest-rank summaries.
The diagnostic can opt in via TSFP_DIAGNOSTIC_TELEMETRY (default OFF); reports
include loop elapsed time and presentation submissions, never game/display FPS.
See [TELEMETRY.md](TELEMETRY.md) for exact contracts, build/test commands,
capture metadata and limits. Guest runtime hooks/device capture are still absent.
No retail inputs, physical console results or performance measurements exist.
Issue #4 still requires the seven inputs in TITLE_INPUTS.md before real tracing.


## 2026-10-30: wait-import BOOLEAN ABI correction (issue #7)

NtWaitForSingleObject Alertable now decodes only the low8 bits of its four-byte
stdcall slot, matching pinned BOOLEAN and bridge casts. High bits alone no
longer cause an unsupported-import fault. Every nonzero low byte still faults
before reading Timeout; alertable waits are not implemented. The existing real
translated CALL fixture now exercises512 high/low-byte combinations, completed
threads, exact EAX/ESP and invalid-timeout fault ordering. Source-generation
checks pin the authoritative type/signature/cast; source pins remain unchanged.
This fixes a demonstrated import defect, not full Xbox ABI/runtime compatibility.
Authentic game-entry tracing still requires the seven missing TITLE_INPUTS.md
inputs; no retail/hardware/performance behavior has been established.


## Captured fault export and offline mapping — 2026-10-30

Added bounded integer-only TsfpFault JSON export after the existing recovery
boundary, with no guest reads or cleanup/recovery changes. The offline mapper
uses declared end-exclusive function ranges, reports unverified_local_map
assurance and never guesses nearest symbols or stack frames. See
[FAULT_TRACING.md](FAULT_TRACING.md) for schema, commands and limits. Real
translated fixture faults validate this path, not retail execution. Runtime
timing hooks, actual title inputs, full stacks/SEH and physical Vita captures
remain incomplete; issue #4's seven-file input gate is unchanged.


## Optional guest-entry elapsed hook — 2026-10-30

Added tsfp_guest_run_profiled in profiling/guest_run.{h,c}, delegating the existing
outermost recovery boundary without new guards/locks. Disabled mode makes no
clock calls; invalid/nested use returns-1 unchanged. Failed clock pairs are
counted without skipping guest code or inventing duration. Capture and guard
cleanup precede the final clock read. Tests use real translated code and fake
clock values; they establish semantics, not CPU usage or Vita performance.
See TELEMETRY.md. No default guest-run behavior, title inputs or source pin
changed. Next critical gate remains authenticated title input/dispatch/first-entry
tracing, then actual per-service/hardware instrumentation as evidence permits.
