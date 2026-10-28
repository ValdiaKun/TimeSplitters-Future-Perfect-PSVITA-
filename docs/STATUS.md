# Port status — 2026-10-29

**Not playable. No retail guest initialization, graphics, menu, audio, saves or
campaign execution. Physical Vita behavior is unverified.**

## Implemented

- Continued the clean `vita-bootstrap` branch and existing draft PR #1; no main
  merge. The original bootstrap CI run `40715066565` passed at `c787e0f`.
- Pinned PC submodule/source manifest, offline runtime reconstruction and full
  authoritative patch application, reproducible ARM header adaptation.
- Checked unaligned little-endian guest memory prototype and deterministic
  generation of ten synthetic x86 functions using the real PC translator.
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
- B: narrow synthetic CPU subset passes ARM32 execution under QEMU; generated scalar RAM-wrap loads/stores, complete
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
The new split-safe scalar load/store *accessors* are tested against the PC
shared-map oracle. The next code task is wiring them into the source generator
for actual translated load/store expressions at RAM-wrap boundaries, then
adding generated boundary fixtures and expanding flag/FP edge coverage. On physical Vita, separately
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
