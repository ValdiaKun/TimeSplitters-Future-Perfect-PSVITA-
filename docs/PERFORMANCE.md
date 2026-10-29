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
