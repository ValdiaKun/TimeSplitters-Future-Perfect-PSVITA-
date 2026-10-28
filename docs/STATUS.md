# Port status — 2026-10-28

**Not playable. No retail guest initialization, graphics, menu, audio, saves or
campaign execution. Physical Vita behavior is unverified.**

## Implemented

- Continued the clean `vita-bootstrap` branch and existing draft PR #1; no main
  merge. The original bootstrap CI run `40715066565` passed at `c787e0f`.
- Pinned PC submodule/source manifest, offline runtime reconstruction and full
  authoritative patch application, reproducible ARM header adaptation.
- Checked unaligned little-endian guest memory prototype and deterministic
  generation of nine synthetic x86 functions using the real PC translator.
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
cross-built. GitHub Actions CPU/Vita run `40718220983` and host input run `40718220954` passed at code commit `caff85b`; see the linked validation record. VPK creation
is not on-device execution. No hardware behavior or FPS result is confirmed.

## Partial and blocked

- A: reproducible cross-build and deterministic host tests implemented. Vita
  installation, display, input, lifecycle and diagnostic log delivery await hardware.
- B: narrow synthetic CPU subset passes ARM32 execution under QEMU; the real
  title memory layout, aliases, all flags/FP modes, exception delivery, real guest
  threads and full dispatch are incomplete. Acceptance is not complete.
- C: reviewed direct GXM versus vitaGL references; production backend undecided.
  No GPU benchmark/device, scene capture or working game renderer exists.
- D: no supported US Xbox data supplied. No title initialization/HLE/audio/video/
  saves integrated. Game tests must use isolated saves and verified user files.
- E: no working gameplay/hardware profile; performance optimization is premature.

## Handoff

Start with `python3 tools/test_guest.py --arm-cc arm-linux-gnueabihf-gcc` in a
checkout with the pinned submodule initialized. Read [GUEST_RUNTIME.md](GUEST_RUNTIME.md).
The next code task is tested 64 MiB backing/alias/MMIO classification without
kernel plugins, then generated flag/FP edge coverage. On physical Vita, separately
validate bootstrap installation, both sticks/buttons and Select+Start exit;
collect synthetic guest test results before connecting title boot. Never mark a
menu, diagnostic or synthetic CPU test as proof of gameplay.
