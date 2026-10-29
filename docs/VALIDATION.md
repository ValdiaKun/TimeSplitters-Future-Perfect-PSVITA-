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
cross-build passed. No new source dependency pin changed. Code commit `9a48db1da90aa4ad8d57000c8dea832da3ae55f2` passed:

- [CPU/ARM and Vita cross-build, PR run 40721699619](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40721699619).
- [Host input, PR run 40721699935](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40721699935).

Remote main remained `0322acbea29bffccc267f0e48893795a9f420efa` and PR #1
remained open/draft. Earlier CI links above refer to the earlier foundation.


## 2026-10-29: typed scalar accesses across 64 MiB RAM aliases

**Changed:** `guest/memory.{h,c}`, `tests/memory_suite.c`.
The prior 64 MiB RAM/28-mirror/contiguous/tiled/MMIO map is preserved.

The existing `tools/test_guest.py --arm-cc arm-linux-gnueabihf-gcc`
workflow compares identical transcripts from the pinned PC mapped oracle,
portable host implementation, AddressSanitizer/UndefinedBehaviorSanitizer,
and ARMv7 Cortex-A9 userspace under QEMU. Tests now additionally verify
all 28 adjacent low-view boundaries at every 64-bit split position,
native PC oracle cross-view writes and reads, unsigned and signed
16/32/64-bit accesses, preserved NaN payload and negative-zero bits,
valid contiguous-to-kernel-data boundaries, and rejected MMIO/flash/
unbacked and final-view accesses without partial mutation.

**GitHub Actions evidence (code commit `f778686d95db23509a8b5d7a72c26b752a4dd8cc`):**

- [CPU + Vita cross-build run 40722808711](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40722808711):
  `cpu` **success** and `vita` **success**.
- [Host input-mapping run 40722808716](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40722808716):
  **success**.

These are automated Linux runners, not locally executed host tools in this
conversation. The Vita build job cross-compiles diagnostics and synthetic
guest-test VPKs only; no physical console installed them. The generated
recompiler `MEM*` lvalues have not been switched to these scalar helpers.
No game boot, gameplay, GPU render, FPS or Vita heap availability was
measured. Do not infer performance gains from accessors or CI completion.

## Generated memory integration — 2026-10-29

Local environment remains Debian x86_64/GCC 14.2.0, ARM Linux GCC 14.2.0,
QEMU 10.0.13 Cortex-A9, VitaSDK GCC 15.2.0 and the same PC/runtime source pins.
New host-only parser dependency: pycparser 2.23, recorded in sources.lock.json
and CI with its BSD notice. The existing documented ARM runner and Release Vita
build commands pass. The runner now also executes seven source-pass regressions
and compares distinct unlowered PC versus lowered portable outputs for 35 x86
fixtures. The v3 transcripts match, ending `memory-map=pass`.

During development, the new MOVSD payload test failed against pinned PC output:
it stored `xmm0.f[0]` to a double destination. The emission adapter now copies
integer lanes for scalar SSE moves, preserving the original bits. A generated
RMW SETO test initially failed translation because the pinned compiler could not
form ADD overflow; the adapter now derives ADD/SUB overflow from captured result
and source snapshots. Both repairs are applied to the baseline and portable
emission, with explicit expected values. The first helper-header build also
caught an MMX type guard mismatch; MMX helpers are now conditional on the actual
runtime type and a real MMX fixture is compiled and executed.

Executed: `tools/test_guest.py --arm-cc ... --qemu ...`, Python syntax checks,
strict C11 controller tests, `guest_codegen.py --help`, and CMake Release Vita
cross-build. No warnings/checks were disabled to fix these failures. Unlowered
PC tests use the original memory header but the documented emission repairs;
this does not establish correctness of untouched historical generated code.

Coverage and remaining instruction limits are in GUEST_RUNTIME.md. Unsupported
input is rejected before code generation. A supported retail executable and
its analysis metadata are absent, so successful full-title generation/boot is
unavailable. Physical Vita tests and all performance measurements are unavailable.
Code commit `c3da8a11b365dc28e62d64aac88ad8b45840d915` passed
[CPU/ARM + Vita CI 40727117655](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40727117655)
and [host input CI 40727117635](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40727117635).
A follow-up adds independent-source 64-bit wrapped stores (MMX, MOVSD and x87
integer stores), failed 64-bit writes and unchanged x87 state on a rejected
source. The complete local PC/portable/sanitizer/ARM matrix and Vita cross-build
passed again with those assertions. The existing valid-wrap negative test was
replaced by positive generated wrap tests; invalid/MMIO/final-view rejection
assertions remain enabled.

Follow-up code/test commit `bc7aa804297bd8f987e8f7454776a60fdc6fc49b`
passed [CPU/ARM + Vita CI 40727763246](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40727763246)
(`cpu` and `vita` both successful), plus
[host input CI 40727763221](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40727763221).
The working tree was clean after push; PR #1 remained draft/open and remote
main remained `0322acbea29bffccc267f0e48893795a9f420efa`. No game binaries,
VPKs, retail data, user settings or saves were committed or published.

## REP/string milestone — 2026-10-29

Confirmed starting HEAD `4e1d5509b269555b8470cdda661aaf796ad281ff`, clean
`vita-bootstrap`, open draft PR #1 and green starting CI before edits. PC/runtime,
Capstone, parser and toolchain pins remain unchanged. The same documented
`tools/test_guest.py --arm-cc ... --qemu ...` command now generates 103 functions,
runs 11 source-pass tests and executes 1,518 string data scenarios plus generated
flag consumers. PC baseline, portable, ASan/UBSan and ARMv7 QEMU transcripts match:

```
string-fixtures-v1: cases=1518 bytes=pass flags=pass progress=pass faults=pass
```

The existing v3 guest transcript also passes. Strict host input tests, Python
syntax, staged whitespace checks and VitaSDK Release cross-build passed locally.
The native PC string oracle executes actual host opcodes, installs a temporary
SIGSEGV handler on the single test thread and restores the prior handler. It
captures hardware registers/flags and compares the committed memory image.
It is separate from the independent byte reference and generated guest runtime;
ARM/Vita never execute this host assembly or signal handler.

Failures resolved during implementation:

- F2 MOVSD advanced once rather than repeating: Capstone preserved F2 in raw
  bytes but dropped it from its mnemonic/prefix metadata. Decode repetition from
  instruction bytes; all width/prefix combinations remain regression fixtures.
- A partial REPNE CMPS fault disagreed with an assumed entry-flag restoration.
  Native AMD SIGSEGV context showed last-comparison flags; the initial workaround
  treated that as the guest contract. This was incorrect for Intel. The later
  REP fault regression correction below supersedes that workaround and enforces
  Intel entry-flags restoration while retaining completed register/count progress.

No test assertions or warnings were disabled. Physical Vita execution, full
retail generation, title boot, GPU validation and performance remain unavailable.
Native x86 reference is Linux x86_64; guest address-register rollover tests use
explicit byte-model expectations rather than native 64-bit address rollover.
Remote CI evidence will be appended after the implementation push.

## Atomic flags follow-up (2026-10-29)

111 actual translated functions and 12 source-pass tests pass the same mapped PC,
portable host, ASan/UBSan and ARMv7 Cortex-A9 QEMU matrix. New transcript:

```
atomic-fixtures-v1: cases=294 flags=pass alias=pass fault=pass threads=pass
```

Native Linux x86_64 locked instructions independently check valid-operation
registers, flags and memory. Additional assertions cover memory address/source
aliasing, carry consumers, comparison branches, zero-count string flag retention,
nonmutating invalid/unaligned faults and 2,000 concurrent mirror increments.
Unsupported operand forms are source-pass rejection tests. The unchanged 1,518
string scenarios and existing v3 suite pass. VitaSDK Release VPK cross-build
passes; it supplies compilation evidence only. No on-device results or timings.

### Native REP alias-oracle portability finding

CI `40800163555` failed native-image comparison while the independent PR run
`40800171408` passed the identical `87f5de8` source. Diagnostic commit `fb86784`
identified REP MOVSB from `04040040` to physical synonym `00040041`, count 5:
byte-image offset 130 was `92` rather than sequential-model `85`. These distinct
virtual mappings physically overlap. Modern native bulk-string execution is not
used as the sequential-order reference across different mapped views after this
observed runner dependence. `string_native_oracle.c` explicitly executes actual
single-element MOVS and commits count between elements for cross-view references;
ordinary ranges continue to execute actual REP. All generated cross-view cases
still compare every byte and register against the independent sequential model,
and retain the pinned PC shared mappings. This is an oracle portability fix,
not a guest-runtime fast path, disabled assertion or Xbox hardware validation.

Final implementation `180a0a7`: [CPU/ARM/Vita CI 40800471487](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40800471487)
passed both jobs; input CI 40800476515 passed. Includes the alias-oracle fix.
No physical Vita execution or performance measurement was performed.

## Execution/thread foundation — 2026-10-29

Started at clean `273a63f892202b373122464f6ebb9ffb55a91336`, unchanged PC/runtime
and SDK pins, open draft PR #1, green baseline CI. Re-ran the 111-function baseline
before editing; all four host/ARM modes passed. New generation emits 117 functions
and runs 13 source-pass tests. Full command in this environment:

```sh
LD_LIBRARY_PATH=/workspace/toolchains/arm/usr/lib/x86_64-linux-gnu \
PATH=/workspace/toolchains/arm/usr/bin:$PATH \
python3 tools/test_guest.py \
  --arm-cc '/workspace/toolchains/arm/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/workspace/toolchains/arm' \
  --qemu /workspace/toolchains/arm/usr/bin/qemu-arm
VITASDK="$PWD/.cache/toolchains/vitasdk" \
PATH="$PWD/.cache/toolchains/vitasdk/bin:$PATH" \
/home/agent/.local/bin/cmake --build build/vita-locked
```

Mapped-PC header baseline, portable host, ASan/UBSan and Cortex-A9 QEMU passed
with identical transcripts, including unchanged string/atomic/v3 assertions:

```
execution-v1: capture=pass divide=pass dispatch=pass nested=pass threads=2 tls=pass call-flags=pass
```

Coverage: actual translated divide-zero/overflow with unmodified EAX/EDX and
fault-site capture; portable translated MMIO load/store faults with read/write
metadata; unresolved and non-code translated indirect calls with pushed return
address preserved; nested fault followed by captured thread exit; repeated
translated divide faults (32 per worker), isolated CPU/TLS/FS and boundary state;
stdcall context arguments and stack return; direct compare/call/MOV/return flag
preservation. XMM NaN payload bits survive snapshot. Invalid thread/TLS setup
leaves state/destination unchanged. Native worker creation/join uses existing
pthread host/Vita test support, not a new Xbox scheduler.

The PC baseline executes the same adapted generated divide/dispatch functions;
invalid ordinary mapped memory is not recovered through native host signals.
Those translated memory-fault assertions execute portable/sanitized/ARM paths;
the existing native string fault reference remains unchanged. Snapshot flags have
an explicit validity mask; unknown arithmetic/call/join state is not advertised
as complete EFLAGS. Call coverage is a flag-preserving MOV callee, not arbitrary
interprocedural flag propagation.

VitaSDK diagnostic and synthetic guest VPK cross-build passed; strict controller
compile/run, Python syntax and whitespace checks passed. No assertions disabled
or new warning suppressions. Commands/revisions/hashes remain captured by the
existing ignored `build/guest/report.json`. No physical Vita, title boot, Xbox SEH,
GPU benchmark or measured performance. Remote CI evidence follows the push.

Execution implementation `a8e96ec`: [CPU/ARM/Vita CI 40802669954](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40802669954)
passed both jobs. Input CI 40802669848 passed. Push CI 40802663739 also passed.
Scope remains synthetic execution and cross-compilation; no hardware/game/FPS evidence.

## REP comparison fault-flags regression (2026-10-29)

Starting clean HEAD `60708abf`: PR CPU run 40802804271 failed while push run
40802799286 passed. The failure captured source `0004003f`, destination
`7fffffff`, count 2, actual EFLAGS `10e93` versus model `446`. The differing bits
include defined arithmetic flags; this is not undefined AF, RF or IF noise.
The former model incorrectly required the last completed comparison's flags.

Intel SDM, REP/REPE/REPZ/REPNE/REPNZ description, states: when a fault occurs
executing REPE/REPNE SCAS/CMPS, EFLAGS is restored to its state before the
instruction. Reference text: https://www.felixcloutier.com/x86/rep:repe:repz:repne:repnz
The Xbox uses an Intel CPU. Guest/runtime and the independent byte model now
restore entry flags on that fault path while preserving completed index/count
progress. Successful termination, zero count and non-comparison strings retain
strict prior checks. No flags are declared undefined or masked away to pass.

`tests/rep_fault_probe.c` is a minimal independent reproduction: two equal bytes,
REPE CMPSB count 2, first destination byte valid and second in a PROT_NONE page.
Its signal handler captures actual ucontext EFLAGS and index/count registers.
It asserts all arithmetic/DF bits and exact progress. Run:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -DTSFP_PC_MAPPED_ORACLE -Itests \
  tests/rep_fault_probe.c tests/string_native_oracle.c -o /tmp/rep-fault-probe
/tmp/rep-fault-probe
```

Local AMD EPYC 9V74 (`AuthenticAMD`) retains completed-comparison arithmetic
flags (`44`) instead of Intel's required entry flags (`891`). The same result
passes at -O0/-O2/-O3; changing C optimization is not the fix. The probe now logs
CPUID vendor/signature and hypervisor bit in CI to expose runner differences.
The native oracle checks AMD observations against a separate native expectation;
Intel observations against the documented Intel contract. Both compare every
previously checked defined flag, register and byte; unknown vendors abort.
The portable guest model always enforces Intel behavior independently of host.
AMD observations are host-reference evidence, not an AMD architectural guarantee
or physical Xbox testing. Any deviation from either explicit reference fails.

The probe runs separately from deterministic cross-target transcripts and is
recorded in `build/guest/report.json`. The full 117-function/1,518-string/294-atomic
matrix passes locally on PC, portable, ASan/UBSan and ARMv7 Cortex-A9 QEMU.
VitaSDK cross-build passes. No dependency/source pins changed. Physical Vita,
retail boot and performance remain unavailable. Remote trigger results follow.

REP correction `09a1431`: push CI 40804082165 and PR CI 40804088172 both passed
CPU and Vita jobs on the same revision; input CI 40804088185 passed. Push probe
reported AMD signature a00f11 with hypervisor bit set and flags 44. The failing
old run did not record CPUID; its entry-restored flags match Intel's specified
behavior, but its vendor cannot be established retrospectively from those logs.

## Synthetic thread-object/import entry subset

The same full `tools/test_guest.py --arm-cc ... --qemu ...` command documented
above now generates 119 functions and passes 13 source-pass tests. Existing native
probe, 1,518 strings, 294 atomic cases and execution suites pass unchanged.
New deterministic transcript on mapped PC, portable, ASan/UBSan and ARMv7 QEMU:

```
kernel-entry-v1: object=pass imports=3 stdcall=pass zero=pass exit=37 unsupported=pass
```

Actual translated entry calls through repaired guest import slots to current
thread, zero memory and termination services. Tests assert object identity/TLS,
unchanged template fields, physical-alias overlap rejection, all zeroed bytes,
stdcall ESP progression, status 37, unknown ordinal 511 fault, rejected import
rebinding preserving native metadata, complete argument-frame faults, zero-length
behavior and no partial destination mutation. Generator checks the pinned PC
object/ordinal/argument-byte anchors; no retail data or mocked success calls.

One initial test failed because it overwrote a guest thunk with zero to test
rejection, then expected that zero to dispatch through the previous native table.
The corrected test verifies the rejected guest input is unchanged, then restores
the prior slot VA before testing preserved native metadata. No check disabled.

VitaSDK Release diagnostic/guest VPK cross-build, strict input tests, syntax and
whitespace checks pass. No hardware or performance measurements. Handles/waits,
full Xbox imports, section loading/permissions, XBE/metadata integration and retail
entry remain unimplemented. Remote final-revision CI evidence follows.

Additional entry assertions exercise actual translated FS-relative object/TLS
loads. A cached RtlZeroMemory handler still zeroes memory after another ordinal
lookup, and explicit reset makes old slot addresses unresolved. Review replaced
an initial lookup-dependent generic handler with stable per-service/per-slot
handlers so future call-site caches cannot reroute an import.

Final runtime implementation `d5952d4`: push [40804967536](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40804967536)
and PR [40804973727](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40804973727)
both passed CPU/ARM and Vita jobs. Input CI 40804973827 passed. PC baseline,
portable and ASan/UBSan executables additionally passed three repeated local runs
each. Push probe identified AMD a10f11/hypervisor=1, flags44. No device measurements.
