# Bounded telemetry — issue #6, partial

`profiling/telemetry.{h,c}` is an opt-in, caller-owned elapsed-time recorder.
It does not install runtime hooks, sample a clock, allocate memory, log during
recording, change clocks or open a network endpoint. Each owner thread needs
its own recorder; reads, reset and summary require that owner to be quiescent.
Do not share one across guest workers without external synchronization.
Non-null pointers and initialization by reset are API preconditions; callers
must not modify recorder fields. Do not put resource-owning instrumentation
across an unsafe guest fault/longjmp boundary.

The first 256 valid intervals are retained; later intervals increment a saturating
drop count. Reversed timestamps and invalid kinds increment a separate rejected
count. Reset clears all state. Counters saturate at UINT64_MAX and mark overflow;
interval subtraction cannot wrap because end must be >= begin. The caller must
use a single monotonic microsecond domain. Nested/overlapping intervals are valid
and must not be summed as independent CPU time. Clock discontinuities that remain
numerically increasing cannot be detected by this recorder.

Summary uses nearest-rank percentiles of retained samples only: rank ceil(p*N/100).
For even N, median is the lower middle. Empty categories return no summary,
not a zero-cost workload. Sorting occurs only at summary time, uses at most
256 uint64 values (2KiB scratch), and leaves the trace unchanged. The fixed
recorder holds 256 records plus counters; use static storage for small Vita stacks.
Truncated captures are **not** representative whole-run percentiles. No frame-time,
FPS, simulation-rate or GPU-time estimate is inferred automatically.

## Opt-in diagnostic wiring

Use the existing pinned SDK/build configuration:

```sh
cmake -S ports/vita -B build/vita-locked -DTSFP_DIAGNOSTIC_TELEMETRY=ON
cmake --build build/vita-locked
```

The default option is OFF: no recorder, extra timestamps or report is linked
into the diagnostic. ON records completed control/draw/swap-loop elapsed time
using sceKernelGetProcessTimeWide. It emits one bounded stderr summary on normal
Select+Start exit. Present submissions are counted separately; there are zero
guest simulation steps. Swap/vblank stalls are included in elapsed time. This
is not GPU duration, confirmed display completion, game timing or playable code.
No actual console log or performance baseline has been captured here. Normal
log transport and install/display/pad/exit still need physical Vita validation.

Guest/kernel/render/load interval categories and simulation/draw/upload/underrun
counters exist for future explicit integration; they are **not yet wired** into
the actual guest runtime. Memory high-water is an explicitly supplied observation,
not a system allocation probe. No GPU-completion measurement is implemented.

## Reproducible tests and device capture

`python3 tools/test_telemetry.py --arm-cc arm-linux-gnueabihf-gcc` runs synthetic
clock values under host, ASan/UBSan and Cortex-A9 QEMU; missing ARM flags are
reported as NOT RUN. CPU CI supplies ARM flags. Vita CI builds the diagnostic
with the option OFF and ON. These tests are neither console measurements nor
proof of physical clock/graphics behavior.

For an opt-in physical capture, use an accessible authorized console, keep
network debug endpoints disabled by default and preserve user saves. Record
normal stderr via the user's existing trusted local mechanism; no debugger
plugin is mandatory. Retain the source build and exact settings alongside the
log. A missing field is unavailable, never zero or inferred from another port.
Every benchmark record must include:

| Field | Required evidence |
| --- | --- |
| Scope | diagnostic, synthetic guest or actual title; host/QEMU/emulator/device |
| Build | commit, dirty state, source/SDK pins, compiler/version and flags, telemetry option |
| Device | model, firmware, homebrew conditions, measured CPU/GPU clocks |
| Workload | scene, renderer, internal/output resolution, warmup, thermal conditions |
| Capture | clock domain/unit, start/end, duration, repeats, retained/dropped/rejected counts, saturation mask |
| Timing | min/median/p95/p99/max, sample category, percentile definition, measured instrumentation overhead |
| Rates | simulation steps and present submissions separately; displayed frames only when independently observable |
| Resources | memory domain/high-water source, draw/upload counters, loading stalls, audio underruns |
| Limitations | unavailable GPU/display/input-latency metrics and incomplete/truncated capture |

Use controlled before/after runs and measure instrumentation overhead before
using telemetry for optimization. Hardware, retail inputs and an actual game
renderer remain unavailable. Issue #6 stays open for runtime hooks, fault-PC
symbolization, device capture automation and a genuine hardware baseline.


## Captured fault export and offline mapping — 2026-10-30

Added bounded integer-only TsfpFault JSON export after the existing recovery
boundary, with no guest reads or cleanup/recovery changes. The offline mapper
uses declared end-exclusive function ranges, reports unverified_local_map
assurance and never guesses nearest symbols or stack frames. See
[FAULT_TRACING.md](FAULT_TRACING.md) for schema, commands and limits. Real
translated fixture faults validate this path, not retail execution. Runtime
timing hooks, actual title inputs, full stacks/SEH and physical Vita captures
remain incomplete; issue #4's seven-file input gate is unchanged.
