# Port status — 2026-10-29

**Not playable. No retail guest initialization, graphics, menu, audio, saves or
campaign execution. Physical Vita behavior is unverified.**

## Implemented

- Continued the clean `vita-bootstrap` branch and existing draft PR #1; no main
  merge. The original bootstrap CI run `40715066565` passed at `c787e0f`.
- Pinned PC submodule/source manifest, offline runtime reconstruction and full
  authoritative patch application, reproducible ARM header adaptation.
- Checked unaligned little-endian guest memory prototype and deterministic
  generation of 103 synthetic x86 functions using the real PC translator.
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
Next: atomic XADD/CMPXCHG EFLAGS and exception/thread entry coverage. On physical Vita, separately
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
