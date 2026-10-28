# Validation record — 2026-10-28

Host: Debian 13 x86_64, GCC 14.2.0, Python 3.12.14. PC pin `759f7a0`;
runtime `d90c929`; Capstone 5.0.9; CMake 4.4.4; Ninja 1.13.2.
VitaSDK snapshot `sdk-snapshot-20261015.850.1`, GCC 15.2.0; archive and package
SHA-256s in `sources.lock.json`. SDK hash also matched its published SHA256SUMS.
ARM Linux GCC 14.2.0, QEMU 10.0.13, Cortex-A9 CPU model. No physical Vita.

## Successful commands

Run from repository root after initializing `external/pc`:

```sh
python3 tools/prepare_upstream.py
python3 tools/test_guest.py --arm-cc arm-linux-gnueabihf-gcc --qemu qemu-arm
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  ports/vita/src/input_map.c ports/vita/tests/test_input_map.c \
  -lm -o /tmp/tsfp-vita-input-test
/tmp/tsfp-vita-input-test
python3 tools/fetch_vitasdk.py
export VITASDK="$PWD/.cache/toolchains/vitasdk"
export PATH="$VITASDK/bin:$PATH"
cmake -S ports/vita -B build/vita-locked -G Ninja \
  -DTSFP_BUILD_GUEST_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita-locked
python3 -m py_compile tools/*.py
git diff --check
```

All passed locally. On this non-root host the ARM packages were downloaded with
APT into `/workspace/toolchains/apt` and extracted with `dpkg-deb -x`, rather
than installed globally. The exact CPU runner invocation was:

```sh
LD_LIBRARY_PATH=/workspace/toolchains/arm/usr/lib/x86_64-linux-gnu \
PATH=/workspace/toolchains/arm/usr/bin:$PATH \
python3 tools/test_guest.py \
  --arm-cc '/workspace/toolchains/arm/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/workspace/toolchains/arm' \
  --qemu /workspace/toolchains/arm/usr/bin/qemu-arm
```

PC pristine, portable, ASan/UBSan and ARMv7 transcripts matched:

```
guest-fixtures-v1: add=0 carry=1 call=48 indirect=48 sse=1,3,5,7 x87=2,2,3,2 div=3r1 faults=pass atomic=pass tls=pass
```

Nine translated synthetic functions plus memory offsets 0–15, bounds/overflow,
TLS isolation and concurrent atomic assertions execute; no game assets are read.
Full commands/output and generated fingerprints are in ignored
`build/guest/report.json`; CI uploads that CPU report. The original controller
workflow also passed remotely at `c787e0f`, run `40715066565`.

Code commit `caff85b825d8340bf696ea52a255b5820af4cbbd` passed GitHub Actions:

- PR CPU and Vita cross-build: [40718220983](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40718220983), both jobs successful.
- Push CPU and Vita cross-build: [40718211500](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40718211500), both jobs successful.
- PR host input: [40718220954](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40718220954), successful.

Remote `main` remained `0322acbea29bffccc267f0e48893795a9f420efa`; PR #1
remained open and draft. No merge, force push, release or binary publication.
Release guest ELF inspection also retained `__assert_func`, confirming the
acceptance assertions were compiled in despite `NDEBUG` from Release flags.

Both `build/vita-locked/tsfp-vita-bootstrap.vpk` and `tsfp-guest-test.vpk` were
created. Inspection confirmed `sce_sys/param.sfo` and `eboot.bin` in each archive;
ELF inspection confirmed ARM32 little-endian EABI5. The guest test is a separate
synthetic application with title ID `TFPV00002`, the diagnostic is `TFPV00001`.
Neither VPK is hardware-tested or published as a release.

## Failed commands and corrections

- `apt-get update -qq && apt-get install -y gcc-arm-linux-gnueabihf qemu-user cmake ninja-build`
  failed: non-root access to `/var/lib/apt/lists/partial`. Used read-only system
  sources, writable local lists/cache and package extraction; no system files changed.
- Initial fixture C compilation failed: individual upstream function translation
  expects batch-supplied register aliases. The fixture generator now supplies
  the upstream global GPR/XMM aliases; no generated output edited manually.
- Initial divide-zero assertion failed because upstream uses reason `0`.
  Corrected the expected reason, retained a distinct sentinel before invocation.
- ARM compilation with both `-march=armv7-a` and `-mcpu=cortex-a9` failed under
  GCC 14 `-Werror` due to conflicting effective feature switches. Kept the CPU
  selector and explicit VFPv3/hard-float flags; no warnings suppressed for this.
- First user-local ARM assembly failed to locate `libbfd-2.44-armhf.so`.
  Set the local host-library directory for the extracted binutils invocation.
- Initial second SELF packaging failed: VitaSDK's macro reused a semicolon-list
  flag variable, producing `vita-make-fself -c;-s ...`. Isolated each macro call
  in a CMake function; a clean Release configuration built both VPKs successfully.

## Unavailable validation

Physical boot/display/input/exit, on-device stderr delivery, Vita TLS/newlib
behavior, GPU rendering and timing, game boot/menu/gameplay/audio/save/load,
CPU/GPU clocks and performance measurements. Linux ARM QEMU does not test Vita
kernel calls, scheduling, GPU, memory availability or real-time performance.

## RAM-view and MMIO-classification extension

The expanded runner generates ten synthetic functions. The additional translated
load/add/store fixture executes through all 29 low RAM views and the separate
contiguous/tiled windows. PC baseline uses the original runtime header with Linux
shared-view mappings; portable, sanitizer and ARM runs use software translation.
The Linux oracle checks mapping policy, not the Windows kernel initialization.
The same documented `tools/test_guest.py --arm-cc ...` command passed locally
with this transcript:

```
guest-fixtures-v2: add=0 carry=1 call=48 indirect=48 sse=1,3,5,7 x87=2,2,3,2 div=3r1 faults=pass atomic=pass tls=pass memory-map=pass
```

Coverage includes each classification endpoint; low/contiguous independence;
tiled/kernel-page aliasing; byte transfers across RAM wrap; failed range checks
preserving destinations; rejected overlapping/undersized bindings preserving
prior mappings; optional absent contiguous storage; unbind; translated MMIO and
pointer-wrap faults. Scalar pointer lowering remains incomplete at valid RAM
wraps and is explicitly tested to fault safely, not silently corrupt memory.
The test VPK has a configured 160 MiB heap for the two 64 MiB backings; no real
Vita allocation or performance measurements are available.

Local strict host controller tests, Python syntax checks and Release Vita
cross-build passed. No new source dependency pin changed. New remote CI results
will be recorded after the implementation push; earlier CI links above refer
to the earlier foundation.
