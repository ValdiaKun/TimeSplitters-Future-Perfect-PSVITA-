# Runtime handoff — 2026-10-29

Not playable; no retail boot, GPU/audio/saves, hardware execution or FPS. Verify
branch/HEAD/PR #1 before continuing. Source/toolchain pins remain unchanged.

The next entry points are guest/storage.c and guest/thread_service.c. A host
controller initializes the bounded XBE/image, cedes known-unused writable BSS to
storage_configure, starts the service with an explicit default guest stack, then
enters guest code. Do not infer unused retail BSS from zero initialization.
Register/control the initial root owner as the image tests do. Native creation
and join are performed by the service owner, not on a translated callback stack.
After guest callers quiesce, service_stop refuses live/referenced workers and
closes/joins completed workers; retry a failed native join before pool disable.
Then storage_disable and image_reset are permitted. Never forcibly kill native
workers or change bindings/permissions/templates while pinned.

Supported create profile: ordinal 255/40 bytes, no extra/suspended/debug modes;
TLS size zero or matching profile, stack zero (host default) or aligned/in-bounds,
trusted compiled start entry, two opaque contexts, checked physically disjoint
outputs outside managed arena/templates. Other modes fail explicitly.
The C-scope guard has one durable TLS/static-context slot per native thread and
is only bookkeeping cleanup; it is not Xbox SEH or a cancellation unwinder.

Reproduce with tools/test_guest.py using the exact ARM compiler/sysroot/QEMU
command in VALIDATION.md, then the hash-locked VitaSDK Release build. The new
thread_service_suite.c drives actual translated creation/wait/close; storage,
wait and worker suites retain all fault/concurrency/rollback regressions.
Final CI evidence and latest source revision are in draft PR #1.

Events now use typed cells in the same 32-record registry. tests/event_suite.c
drives translated 189/225/186/233/187, including grants, deadlines, reference
retention and mixed event/thread quota rollback. The thirteenth authored XBE
section adds function 0x13b00; there are 126 translated functions.

Empty unnamed ObjectAttributes are now supported through explicit checked
12-byte guest decoding. Nonzero names, root directories and flags remain errors.
All 126 translated functions and existing regression suites remain enabled.

Bounded allocation now lives in guest/heap.c. Configure before entering guest
code with an explicitly ceded canonical low-RAM64KiB-aligned BSS span (<=1MiB);
never infer unused retail BSS. Configuration pins the image. Both storage pool
configuration orders reject physical overlap. Reserve/commit flags are distinct;
only RW pages, ZeroBits0 and the documented exact flag combinations are supported.
Wait for callers to quiesce, release every reservation, then disable before
image reset. Plain guest VA reuse cannot detect a stale pointer after reallocation.

The main CPU runner now executes127 translated functions and an independent
hash-pinned xboxrecomp VM differential gate. Missing sources fail closed unless
--fetch explicitly allows verified public source downloads. tools/test_graphics.py
validates the isolated Xita BC layout adapter; no GPU or device run is implied.
See ACCELERATION_AUDIT.md and NOTICE for exact revisions/licenses.

Event outputs now use tsfp_heap_pin_output/unpin_output plus a durable TLS
scope guard in kernel.c. Their64 bounded generation records retain the allocation
through result and EAX/ESP publication, releasing on normal/fault paths. Native
cancellation is deferred. Release/decommit returns UNSUCCESSFUL unchanged while
an event output is pinned; retry after the call. Guest code cannot forge pins.

Thread-create outputs now reuse CallGuard's two retained heap references across
probes/RPC/publication/EAX/ESP and fault cleanup. Distinct allocations and mirrors
are tested; server packets still contain no guest output pointers. Do not call
the storage role predicate as though it retained lifetime.

VM imports184/199 now reuse a two-reference KernelOutputGuard, pinning both
words before reads and through stores/EAX/ESP. Independent heap/image and mirror
locations are supported. Release/decommit of an output owner rejects unchanged;
relocate result words and retry. This is failure-atomic publication, not a
concurrent two-word atomic snapshot. Callers synchronize word and target access.

Next: issue #4 real-input gate. Run `python3 tools/check_title_inputs.py` first.
Exact ignored installation layout and local provenance contract are in
TITLE_INPUTS.md. The current command returns2/missing_inputs with all seven
logical slots absent. No hash override exists in its production CLI. It reuses
the pinned PC parser and verifies source identity plus exact snapshots; local
metadata attestation does not prove analysis derivation. Unknown imports are
reported, not stubbed. Nothing is generated or executed by this tool.

Once the supported XBE and matching analysis are available, validate intake,
use the existing guarded generator with explicit analysis-directory overrides,
then integrate authenticated dispatch/image ownership and capture the first
actual entry/fault. Do not infer unused retail BSS from zeros. Issue #4 stays
open: original inputs, generation and real trace remain unavailable. Managed-
pool result roles, broader services and native graphics/hardware gates remain.
Physical Vita/retail/gameplay/FPS are unverified. Issues #2/#3 have passed their
bounded acceptance gates; do not reimplement them.


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


## Same-game audit and thread BOOLEAN correction — 2026-10-30

Compared pinned PC, NeilSeligmann/tsfp-recomp, lmplied/TSFPRecomp and TS2
Split-Again at exact revisions/licenses in ACCELERATION_AUDIT.md. Shared ancestry
and source-stated build identity are explicit; no retail code or maps acquired.
A reproduced PsCreateSystemThreadEx ABI mismatch now decodes suspended/debug
from their low-byte BOOLEAN values. True modes remain rejected; ULONG extra
bytes do not truncate. Existing translated creation/wait/close, heap publication,
rollback and two concurrent callers remain the execution path, not new stubs.
The approved executable/hash, source pins and seven-file intake blocker remain
unchanged. This is a narrow ABI fix, not retail boot or gameplay evidence.

## 2026-10-30: recovered offline shader/vertex-stream milestone

Independent branch `chatgpt-vita-development`, starting HEAD
`171a8ecc6c8146e7b8c1d55cc98d4319b08843dc`. Recovered source changes were backed
up before editing. Original `vita-bootstrap` and `main` remain untouched.

Implemented public-source, MIT open-shacccg offline compilation with locked
source/dependency hashes, strict warning rejection/zero fast flags, deterministic
GXP bytes and reflected interface checks. Added a real position/UV vertex-stream,
texture/depth/blend offscreen GXM fixture, default OFF. See
[OFFLINE_SHADERS.md](OFFLINE_SHADERS.md) for commands, provenance and exact limits.
Vertex COLOR is explicitly rejected; UV uses a reflected padded float4 slot.

Local validation: fresh offline shader source rebuild (each positive twice),
bundle verification, nine GXP validator tests, shader corruption/stale-source
controls, 55 Python unit tests, strict input mapping, full PC/portable/ASan/UBSan/
ARMv7 Cortex-A9 guest runner, host graphics and telemetry suites, and pinned
VitaSDK Release cross-build passed. All five VPKs contain eboot.bin and distinct
expected title IDs TFPV00001..5. Generated GXP/build outputs are ignored.

Physical Vita execution, shader acceptance/pixels, memory capacity and performance
are NOT measured. No retail boot or gameplay claim. All seven authenticated title
inputs remain missing; `check_title_inputs.py` correctly returns missing_inputs
(exit 2). Next gate: install the opt-in stream fixture on a legitimate Vita/PS TV,
record GXP acceptance, five readback samples and cleanup results before expanding
D3D8 semantics. Authentic supported game files/analysis remain a separate blocker.
