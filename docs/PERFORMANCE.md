# Performance baseline

No TimeSplitters gameplay performance has been measured. There is no Vita
hardware, Vita GPU profiler or configured supported Vita emulator in this
session. QEMU Cortex-A9 CPU test execution is correctness evidence only.

| Metric | Baseline |
| --- | --- |
| Displayed FPS / game simulation rate | Unavailable: game not integrated |
| Frame-time median / p95 / p99 | Unavailable |
| Guest / render preparation / GPU execution time | Unavailable |
| Draws / triangles / uploads / shader changes | Unavailable |
| Peak general / GPU memory | Unavailable |
| Loading / stalls / audio underruns / input latency | Unavailable |
| Before/after optimization effect | No measurements; no optimization claim |

Initial target is correct gameplay with consistent 30 FPS; 60 FPS is provisional.
No clock override, kernel plugin, interpolation or fake-frame path is enabled.
The bootstrap's vblank wait does not measure or establish game simulation rate.

Future benchmark records must include device model, actual CPU/GPU clocks,
renderer, internal resolution, compiler/options, commit and dirty state, source
pins, scene, warmup, duration, sample count and repeated-run distributions.
Report simulation steps and completed presentations separately; count lost frames
and dropped samples. Use monotonic CPU timestamps and GPU notifications where
supported, account for profiler overhead, and preserve the original game clock.
Use fixed scenes and the same conditions before/after each change. Record missing
metrics explicitly instead of substituting host timing or other ports' numbers.

The synthetic harness currently records reproducibility and pass/fail, not CPU
microbenchmark timings. Add bounded call/memory/dispatch benchmarks after the
full memory contract is established and validate costs on a real ARM/Vita host.

The memory tests now request two 64 MiB backings and a 160 MiB newlib heap in
the synthetic Vita VPK. These are configured sizes, not measured peak RAM or
proof of on-device allocation success. No speed claim follows from replacing
virtual aliases with checked software translation.

Generated scalar emission now calls checked accessors; source-pass tests and
QEMU establish correctness in the tested subset only. No CPU-overhead or frame
rate gain has been measured, and no checks were removed as a speed optimization.

REP/string execution currently uses validated per-element accesses. No bulk fast
path is introduced: virtual-disjoint ranges can physically overlap through RAM
mirrors, and faults must preserve element progress. The 1,518 string scenarios
are correctness coverage, not timing measurements. TLS flag and probe overhead
remain unmeasured on Vita; no FPS or speedup is claimed.

Aligned atomic source adapters preserve atomic primitives and add correct flags;
294 arithmetic scenarios and concurrent mirror increments are correctness checks.
No atomic throughput, contention, hardware frame-time or speedup is measured.

Execution snapshots and per-instruction site recording are correctness
instrumentation, not measured optimizations. Their Vita overhead is unavailable.
Snapshots allocate no heap per event; TLS preparation uses one bounded temporary
(up to 64 KiB) freed before guest execution. No production memory budget or
thread throughput is established. No device/frame-time results exist.

REP comparison-fault correction adds whole-element probes before repeated
comparisons to restore Intel entry flags before a fault. This is a correctness
change; overhead is unmeasured. No string fast path or speed claim is introduced.

Kernel subset uses a fixed 16-slot dispatch table, a 512-byte object staging
buffer and 256-byte constant zero chunks; no per-call heap allocations. Range
validation and stdcall marshaling are correctness-first. Throughput and device
memory/performance remain unmeasured; these bounds are not a title capacity claim.

Image initialization stages at most 1 MiB once, releases it before execution,
and retains fixed section/permission metadata. Instruction and memory-policy
guards introduce unmeasured overhead. No throughput, frame-rate, device allocation
capacity or production budget is established. The transactional startup copy is
for validation/publication correctness, not a measured loading optimization.

Raw XBE normalization borrows file views and uses fixed bounded local metadata;
it allocates no section copies. Initialization still stages at most 1 MiB once.
Read-only imports patch before final policy publication. No loader throughput or
real-device frame/memory measurement is available.

TLS extraction reuses the image staging buffer; no persistent duplicate template
is retained. Standalone worker preparation uses one bounded temporary (at most
64 KiB) and frees it before guest execution. Tests use 36-byte TLS blocks and
64-byte thread-data buffers. These are synthetic configuration, not measured
production capacity or throughput. Vita performance/allocation data unavailable.

The thread registry uses 32 static records and one native mutex, without per-record
heap allocation. Closing does not free guest memory; native owners release after
stopping/joining. No throughput or Vita synchronization cost has been measured.
This bounded ownership implementation is not a claimed performance optimization.

Zero-timeout polling performs a checked timeout read and one fixed-record mutex
lookup; it allocates no memory and does not sleep/spin. No device latency or
throughput has been measured. Polling is not a proposed game scheduling policy.

Blocking completion uses a shared monotonic condition and bounded registry
references; no per-wait allocation or busy loop is introduced. Timed sleeps
are bounded to one second for long-deadline time_t safety. Condition scheduling
latency, Vita thread capacity and actual wait overhead are unmeasured. No FPS
or production memory-budget conclusion follows from host/QEMU tests.

Worker metadata is caller-owned and globally capped at 32 registered workers.
Backend-owned native stack size is explicit (64 KiB to 1 MiB); tests request
256 KiB each, with separate 4 KiB guest stacks and existing 164-byte TIB/TLS/data
spans. These are test settings, not measured production stack bounds. The
adapter introduces no duplicate guest RAM buffer. Native stack allocation,
Vita capacity/high-water use, thread latency and gameplay effects are unmeasured.

Storage reservations add 32 fixed native metadata records and an explicit guest
arena of at most 1 MiB in this bounded profile. Prepared reservations retain
only metadata backups (704 bytes each in current tests); they do not duplicate
whole guest stacks. Aggregate retained backups cannot exceed the configured
arena. The existing TLS builder uses one temporary up to 64 KiB. Successful
launch immediately frees its backup. Tests cede three 5 KiB slots, request 4 KiB
guest/256 KiB native stacks, and use 164-byte TIB/TLS/thread-data spans. These
settings are not measured production budgets; native latency, fragmentation,
hardware allocation capacity and FPS remain unmeasured. No optimization claimed.

Thread creation now adds one native service thread (256 KiB explicit native
stack in tests) and bounded scalar packet/lease records. A synchronous create
introduces a native request/reply scheduling round trip; this is a safety design,
not a claimed speedup. There is no per-frame request, queue spin or packet heap
allocation. Parent test threads use platform-default native stacks, also not a
production budget. CPU dispatch/scheduling latency and real Vita memory/high-water
use remain unmeasured; 30/60 FPS targets remain entirely provisional.

## Event resource accounting (not a hardware benchmark)

Events share the existing bounded 32-cell registry with threads; no event heap
allocation or per-event native condition is introduced. Blocking waiters use
stack-local grant/deadline nodes (maximum 64 references per object) and the
existing monotonic condition. Broadcast wakes can cause unrelated waiters to
recheck predicates; no performance improvement is claimed. Real Vita memory,
contention cost, FPS and latency remain unmeasured.

Empty event-attribute decoding uses a fixed three-word stack snapshot; no
allocation or additional resident pool is introduced. Cost is not benchmarked.

## Allocation and layout accounting — no device measurements

The guest allocator reuses ceded RAM and static256-page owner/atomic-commit
metadata (maximum1MiB arena), with no call-time host allocation. The test cedes
256KiB; this is not a production guest heap budget. Permission filtering adds
page-state work only while configured; its CPU cost is unmeasured. The Xita BC
layout adapter copies compressed blocks without decoding pixels and allocates
no buffer itself; throughput and actual GPU correctness remain unmeasured.
No host/QEMU time is presented as Vita performance. The synthetic160MiB heap
and successful VPK cross-build still do not prove production fit or FPS.

Event publication adds64 static two-word native reference records and shared
ownership-lock operations. It introduces no host allocation or new RAM backing.
Reference and lock cost are not benchmarked; no performance gain is claimed.

Issue #2 adds two native token fields and a count to the existing per-native-thread
CallGuard; it reuses the same64-reference registry and ownership lock. No new
backing, request allocation or guard engine. Latency/memory effects and Vita FPS
remain unmeasured; this is a correctness change, not a claimed optimization.

Issue #3 extends the existing kernel output TLS record from one reference to two
and reuses the bounded64-reference registry. No allocator/backing/rollback-buffer
duplication. Publication cost and real Vita memory/frame-time effects remain
unmeasured. No optimization or hardware performance claim.
