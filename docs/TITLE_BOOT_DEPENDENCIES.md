# Title-entry dependency map — 2026-10-29

PC pin `759f7a0ecebc2341cdcedf638fcea68c4a7fbf4e`, prepared runtime
`d90c92988a9f99d0580d2ca3f7efd6ec3a7b7845` plus its authoritative patch stack.
Paths below are in that prepared runtime. This is a source dependency map, not
an observed retail initialization trace: no supported XBE/analysis data is present.

| Dependency | Authoritative entry point / Vita work |
| --- | --- |
| Verify executable and generate | `tools/recomp/`, PC `tools/compiler/`; existing `tools/guest_codegen.py` verifies US SHA-256 before retail generation. Matching analysis metadata still required. |
| Image sections, initial memory, imports | `src/kernel/xbox_memory_layout.c`, `xbox_memory_layout.h`; adapt section loading and checked import patching, retaining the existing Vita RAM policy. Bounded structural normalization and synthetic initialization exist in guest/xbe.c and guest/image.c; retail integration remains unverified. |
| Register initialization, title entry | `templates/new-game/src/main.c`, generated dispatch; connect validated entry address only after imports and title state are initialized. |
| Threads, stack arguments, TLS | `src/kernel/kernel_bridge.c` `bridge_run_thread_inline`, `bridge_thread_main`, `bridge_PsCreateSystemThreadEx`; `xbox_memory_layout.c` `xbox_AllocThreadTib`. `guest/execution.c` now initializes caller-owned CPU/stack/FS state and a minimal TIB/TLS block. Xbox thread objects, template extraction, handles and native scheduling remain open. |
| Faults and exception delivery | Header divide hook and checked memory/dispatch callbacks; new C-only capture boundary does not walk fs:[0] or deliver SEH. Need validated Xbox exception frame layout and safe dispatch before recovery into title code. |
| Allocation and object lifetime | `xbox_memory_layout.c`, `kernel_bridge.c`; adapt bounded guest heap/stack/object allocators, then import services. The test's 160 MiB heap is not a production budget. |
| File access and saves | `kernel_bridge.c` file/import bridges; replace Win32 dependencies behind Vita storage interfaces. Require user data root and isolated save root. No file-service success stubs. |
| Timers and synchronization | `src/kernel/kernel_thread.c`, `kernel_bridge.c`; adapt public native waits/clock facilities preserving guest clock semantics and resource-safe fault boundaries. |
| Input | `src/input/recomp_pad.c`, `src/hle/hle_input.c`; connect native input mapping to the title-facing HLE state after import integration. |
| Graphics/audio/devices | `src/hle/`, `src/kernel/`; identify actual import/workload subset from a verified title trace. MMIO continues to fault. GPU backend remains unselected pending device correctness/measurement. |
| Indirect dispatch | Generated lookup plus manual/kernel tables; unresolved targets now produce captured faults inside the execution boundary. Full title dispatch tables are absent. |

Shortest next dependency: guest thread-object/TIB initialization and an explicit
kernel import dispatch boundary using the pinned contracts, with deterministic
fixtures and fail-closed unsupported ordinals. Do not expand GPU infrastructure
as a substitute for establishing that entry path. Real title validation requires
the supported user-provided executable, matching metadata, game data and Vita.

Current correctness gate: Intel REP comparison-fault EFLAGS restoration must pass
push and PR validation before extending this boot path. The native host oracle
has a separate vendor contract; see VALIDATION.md for the captured regression.

Implemented subset after the CI gate: template-based thread object at fs:[0x28],
canonical function-slot imports (16-slot bound), current-thread query, zeroing and
termination. A real translated synthetic entry exercises all three services and
TLS memory. This does not complete thread objects/handles/waits or a title loader.
Next concrete gate is bounded image section/permission/import/entry initialization
with synthetic input; the retail XBE and metadata are still unavailable.

Bounded synthetic image gate now implemented: normalized sections, physical alias
conflict checks, staged file/BSS state, initial thread/TLS/object/import setup,
software permissions and compiled entry resolution. One-shot entry and teardown
are tested through real translated code. Next dependency is bounded normalization
of authored XBE headers/section records into this descriptor. Retail hash checking
and matching generated analysis/dispatch data remain mandatory before real use.

Raw-header normalization subset is now implemented in guest/xbe.c and compared
with the pinned PC parser. Authored retail/debug headers both reach synthetic
entry through image initialization. The next concrete dependency is TLS-directory
extraction (raw data, zero fill, index writes and callback policy) into initial
thread storage. Caller-provided synthetic thread configuration remains explicit;
no authenticated retail image or complete XBE/title boot is claimed.

The bounded TLS profile now extracts initialized bytes/zero fill, validates index
and callback metadata, and stages main-thread TLS. Default indexing is title-owned
as in the PC runtime; an explicit loader-write policy is tested. Authored x86
entry and two workers execute negative-index fs:[4] accesses and private object
TlsData links. Next dependency: ownership-safe guest thread handles/lifecycle,
then validated wait/thread imports. Non-empty callbacks, full TLS characteristics,
retail metadata and physical hardware remain separate gates.

Bounded ownership prerequisite implemented: managed thread tokens, lifecycle
state/exit status, strict NtClose and teardown pins. The translated TLS entry
closes a registered token and terminates through real imports. Native worker
creation/join remains explicit test/platform ownership. Next gate: waitable
thread completion, timeout/reference semantics, then a bounded creation/join
adapter; do not claim guest scheduler support from these registry tests.

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
125 authored translated functions now exist. Unsupported suspension/debug/extra
bytes/TLS-size modes return errors; this is not full Xbox threading or retail
boot. See HANDOFF.md for setup/teardown and restrictions. Next independent gate:
bounded event handles and NtCreateEvent/Set/Clear plus event waits, using the
pinned synchronization contract. Verified retail input/analysis and Vita hardware
remain required to identify title-specific blockers and measure gameplay.

## Event boundary now supported

Ordinals 189/225/186 and event waits through 233 execute through the actual
translated import path with typed, retained handles. The profile supports
unnamed events with null or validated empty attributes, manual/auto reset, polling and non-alertable
relative/infinite waits. Named objects, multi-object waits, KeEvent addresses and
absolute/alertable modes remain missing. Empty unnamed
ObjectAttributes descriptors are now validated before allocation/publication. Supported
retail XBE and matching analysis metadata are still unavailable for an entry trace.

## Allocation prerequisite now bounded

Ordinals184/199 now have actual translated reserve/commit/free call coverage
using explicitly ceded BSS and existing ownership/image-policy services. This is
not full Xbox virtual memory and cannot choose a retail arena without an
authenticated ownership/layout plan. No reserve clamps or fake commit/decommit
are used. Kernel outputs residing in this heap remain unsupported pending
publication-lifetime pins; address reuse does not provide stale-pointer identity.
The next testable dependency is reference-safe heap-resident outputs before
normal event/thread allocations can use heap-allocated result structures.
See ACCELERATION_AUDIT.md for pinned source candidates and rejected assumptions.

Heap-resident event result words now have retained output references across
translated create/set publication. Thread-create handle/ID and VM in/out words
remain rejected in this heap; extend their existing guard contracts next rather
than removing checks. This is still synthetic boot preparation without retail data.

Thread creation can now publish heap-resident handle/ID words through retained
CallGuard references. VM in/out words are the next bounded publication dependency
(issue #3). No retail allocation arena or actual initialization trace is inferred.

VM in/out publication now supports eligible heap words with two retained output
references. The next critical gate is authentic executable/analysis intake and
real first-fault tracing (#4). General VM modes, managed-pool output pointers,
retail memory ownership/layout, full services and native GPU execution remain
separate dependencies, not implied by passing synthetic publication tests.
