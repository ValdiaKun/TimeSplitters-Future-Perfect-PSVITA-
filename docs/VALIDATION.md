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

## Bounded synthetic image — 2026-10-29

Starting clean `1b656ed5d0ac87499ebb74fe6ac9118b1bc74c7e`, green push/PR CI,
unchanged PC/runtime/toolchain pins. Added one authored x86 entry wrapper (120
functions total) and generated asset-free byte arrays from the same fixture
specifications. Generated output remains ignored; no game data is used.

Full command (same extracted ARM toolchain and locked VitaSDK as above):

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

PC baseline, portable host, ASan/UBSan and Cortex-A9 QEMU passed with matching
transcripts, including all prior suites and 13 source-pass tests:

```
image-v1: rejected=27 rollback=pass aliases=pass permissions=pass entry=37 unsupported=pass
```

Adversarial cases cover count/size/flag/source/address errors, overflow/device and
contiguous placement, virtual and physical overlap, missing/non-executable/BSS
entry points, unsupported compiled entry, malformed imports, thread/TLS/stack
conflicts and forced allocation failure. Rejected initialization preserves every
original section's bytes, CPU snapshots and prior native import dispatch.
Normal-path tests cover staged file/BSS contents, mirror-edge splits, object/TLS
fields, import patching, read-only aliases, non-executable/unmapped targets,
actual translated entry status 37, unsupported import 511, one-shot entry and
reset preserving RAM while clearing policy/import bindings.

Actual translated permission-store assertions run portable/sanitized/ARM because
the native PC oracle intentionally uses raw scalar lvalues. Common checked
helper/execute-policy assertions run all modes. Actual translated REP comparisons
fault after two successful comparisons at a software-permission boundary and
retain index/count progress while restoring Intel entry flags. This guards the
interaction introduced by image permissions; the earlier native REP regression
remains strictly checked. Active-image unbind and nested teardown are rejected.

No assertions disabled or new warning suppressions. Strict input tests, Python
syntax, whitespace and staged-content checks pass; VitaSDK Release diagnostic and
guest VPK cross-build pass. No physical Vita, retail XBE/metadata, game boot,
GPU measurement or FPS result exists. Remote final-revision checks follow push.

## Raw-header follow-up

After `06f876b` passed push 40809921287 and PR 40809933198 CPU/Vita jobs,
added a bounded structural XBE decoder and loader support for read-only import
sections patched before publication. Authored byte buffers have synthetic empty
certificates and both canonical retail/debug encodings; no retail data is used.

The same complete host/ASan/UBSan/ARMv7 command and Vita Release cross-build pass.
New matching cross-target transcript:

```
xbe-v1: keys=retail,debug malformed=28 rollback=pass entry=37
```

`tools/verify_image_fixture.py` independently runs pinned PC XBEParser on the
same C-authored build-local files, comparing entry, key selection, six sections,
raw padding (68 raw bytes versus 64 virtual), read-only imports and ordinals
104/320/258. Oracle output: `pinned-pc-xbe-oracle: retail,debug entry/sections/imports=pass`.
This check runs automatically after the mapped-PC fixture execution in CI.

Malformed cases test magic, header/table/count bounds, overflow/raw extents,
virtual/raw overlap, bad encoded entry, stack metadata, invalid ordinals and TLS
pointers, plus truncated buffers and output aliasing. Failed normalization leaves
output unchanged. Both valid buffers execute the actual initialized entry, exit
37, and reject later writes to the patched read-only table. Prior CPU, native
REP, concurrency, 27 image rollback and permission tests remain enabled.

The normalizer does not verify signatures/certificates or authenticate retail
compatibility. TLS contents/index/callback initialization remain caller-supplied
rather than extracted. No physical Vita, real title or measured performance.

Parser review follow-up: the first C decoder synthesized zero bytes in virtual
BSS for metadata reads, whereas pinned XBEParser requires file-backed import/TLS
words. Tightened that rule and added two negative cases per encoding: an import
terminator only in BSS and a TLS directory only in BSS. Both parsers now reject
those authored files, while ordinary BSS section initialization remains supported.
This raises raw-header mutation coverage from 24 to 28. The independent PC oracle
also asserts both rejections (`malformed-metadata=pass`). No check was weakened.

At `9d26dd4`, push 40811013395 and PR 40811019637 passed CPU/ARM/Vita jobs;
input 40811019561 passed. The file-backed-metadata correction is validated by the
same complete local matrix and gets its own final CI evidence after push.

Final implementation `d1038cb`: [push 40811313846](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40811313846)
and [PR 40811318719](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40811318719)
both passed CPU/ARM and Vita jobs; input CI 40811318711 passed. PC, portable and
sanitizer executables each passed three repeated local runs; pinned PC parser
accept/reject comparison passed. Scope remains synthetic/structural and
cross-compilation, with no physical device or retail/performance evidence.

## XBE TLS profile — 2026-10-29

Started clean at `283db767620c2cdff8b6139cf7ba205bce8bdea0`, green push/PR CI,
unchanged source/runtime/toolchain pins. Two authored x86 functions bring the
suite to 122; 13 source-pass tests and all earlier string/atomic/image/XBE cases
remain enabled. Full local commands:

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

PC mapped oracle, portable host, ASan/UBSan and Cortex-A9 ARMv7 QEMU pass with
matching transcripts; Vita Release diagnostic/guest VPK cross-build passes:

```
tls-v1: malformed=38 rollback=pass index=title,loader zero-fill=pass workers=2 callbacks=reject
```

Coverage includes 19 malformed/conflicting TLS variants per encoding (38), raw
range reversal/overflow/truncation, zero-fill overflow, index mapping/alignment/
permissions/role aliases, malformed/non-empty callbacks, unsupported characteristics
and full RAM/CPU/prior-import preservation. Empty raw ranges with zero or nonzero
zero-fill execute correctly. Default index stays unchanged until actual translated
entry computes/stores it; explicit loader-write policy is asserted before entry.
Two native worker threads execute translated fs:[4]/negative-index and object
TlsData accesses, modify independent TLS words and preserve main/raw-template
state. Permission/alias-invalid thread entry and failed preparers preserve state.

The independent pinned PC parser validates TLS fields and malformed reversed/
truncated directories. Its raw template plus independent PC-layout arithmetic is
compared with emitted TIB/TLS/object state: fs:[4], slot zero, raw bytes after the
reserved slot, all zero padding/thread-data bytes, untouched initial index and
object links. Output: `pinned-pc-tls-oracle: layout/index/zero-fill=pass malformed=pass`.
This is not execution of Win32 kernel initialization or physical Xbox behavior.

Failure resolved: the first extended oracle script called `.at()` on the metadata
SimpleNamespace returned by parse(), causing AttributeError. Keep the parser
instance and use its checked reader; no expected state/assertion was weakened.
Review also closed permission/alias-invalid thread entry before CPU reset and
ensured each worker object points at its own TLS block.

Strict input tests, Python syntax, whitespace and staged-content checks pass.
No new warnings suppressed or assertions disabled. No hardware, supported retail
XBE/analysis, game execution or performance measurements; non-empty callbacks
are explicitly rejected. Final push and PR CI evidence follows the push.

## 2026-10-29 — thread-owner and NtClose subset

TLS source commit `75c88313980d040ccdc042f0028f7e85e51d3155` passed push
CPU/Vita run [40814706110](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40814706110),
PR CPU/Vita [40814711797](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40814711797),
and input [40814711841](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40814711841).

The following ownership change retains 122 actual translated functions, extending
the TLS entry with an ordinal-187 stdcall and a fourth import. Assertions cover
close during translated execution, exit 37, retained owner after close, blocked
teardown, stale generation, duplicate object/identity, physical alias collision,
32-record exhaustion without output mutation, startup cancellation, competing
close/finish, and two native joined TLS workers with owner records.

Local validation uses the exact ARM command above (`tools/test_guest.py`): pinned
PC shared-map reference, portable host, ASan/UBSan and static Cortex-A9 QEMU.
Release VitaSDK uses `cmake --build build/vita-locked`. Input tests use the strict
C11 command above; Python uses `python3 -m py_compile tools/*.py`; whitespace uses
`git diff --check`. Final command outcomes and CI links are appended after runs.
No native platform scheduler, Xbox waits, retail boot or physical device behavior
is established by this subset. Performance remains unavailable.

Local results: all four CPU modes and PC XBE/TLS parser comparison passed;
both Release VPKs cross-built. Strict input, Python and whitespace checks passed.
Pristine, portable and sanitized binaries each passed three additional runs.
The repeat command initially used nonexistent `pc-baseline`/`host-pc` names
(exit 127); corrected to the runner's `host-pristine`, then all repeats passed.
`handles-v1: close=pass ownership=pass stale=pass race=pass quota=32`.
Native creation/join remains explicit, and the test closes/releases only after
joining the two guest workers. No physical Vita or FPS measurement available.

Ownership commit `857e5002b542fef84b2fbcb6b8cb541416a2c4ca` passed push
CPU/Vita [40835387243](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40835387243),
PR CPU/Vita [40835393411](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40835393411),
and input [40835393354](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40835393354).

Subsequent poll tests reuse actual translated `fixture_kernel_call` (122
functions retained): running timeout, sticky completion, invalid closed handle,
stdcall stack cleanup, unsupported infinite/relative/absolute/alertable modes,
faulting timeout and faulting complete argument frame. Expected status and
zero-timeout contracts are audited in pinned `src/kernel/kernel_sync.c`
(`xbox_nt_timeout_to_ms`, `xbox_wait_result_to_ntstatus`); ordinal/argument
size is generator-guarded against `kernel_bridge.c`. This compares the supported
contract, not native Win32 scheduling or unsupported wait behavior.

Poll subset local matrix passed: pristine PC-header/shared-map, portable host,
ASan/UBSan, static ARMv7 Cortex-A9 QEMU, both VitaSDK Release VPK cross-builds,
strict input, Python and whitespace checks. All three host modes passed three
additional repeats. Output: `thread-poll-v1: running=timeout completion=sticky
closed=invalid unsupported=fault bounds=pass`. This remains synthetic execution,
not Xbox blocking synchronization or physical Vita validation.

## Blocking completion waits — 2026-10-29

Starting baseline `66a91d6` was clean; push CPU/Vita run 40835829120, PR
40835837474 and input 40835837578 passed. Re-ran the full documented ARM command
before edits: pristine/portable/ASan+UBSan/Cortex-A9 QEMU baseline passed.

New wait suite: relative tick decode including INT64_MIN, poll/finite timeout,
completed infinite wait, four referenced waiters acquired before close across
32 lifecycles, repeated/stale waits, wake-all completion, teardown pins, native
cancellation cleanup, 16 completion/deadline races and injected clock/condition
errors. Existing 32-handle exhaustion and all prior suites remain enabled.
Actual translated fixture_kernel_call now exercises a finite timeout and an
infinite wait completed by a separate native owner outside guest recovery;
stdcall, unsupported alertable/absolute modes and faulting metadata stay checked.
122 translated functions retained; no generated C was manually modified.

First ARM error-injection run failed: the test wrapper targeted legacy time32
symbols, while Debian ARM glibc headers redirected runtime calls to
__clock_gettime64/__pthread_cond_timedwait64. Disassembly and preprocessor
__USE_TIME64_REDIRECTS confirmed the mismatch. The wrapper now selects matching
symbols AND timespec ABI. Injection remains test-only linker wrapping; production
has no fault hooks. The corrected ARM run passed. VitaSDK cross-build passed
with monotonic condition APIs; physical Vita scheduling remains unverified.

Final local results: all four CPU modes and independent PC XBE/TLS parser passed;
both Release VPKs cross-built. Strict input, Python syntax and whitespace checks
passed. Pristine, portable and sanitized suites each passed three extra runs
(including wait stress/cancellation and historical string/atomic tests).
No physical Vita, retail executable/analysis data, or performance results exist.

CI correction: `e8a45b9` push 40844635544 and PR 40844642312 failed at the
Ubuntu ARM link (host tests and Vita job passed). Older glibc redirects time64
without defining newer __USE_TIME64_REDIRECTS, so the wrapper function was not
emitted. Detect the established __TIMESIZE==32 plus __USE_TIME_BITS64 instead;
this preserves the same ABI and all error-injection assertions. This is a test
link portability fix, not relaxed runtime/error expectations.

Blocking wait portability fix `fa002b1` passed push CPU/Vita
[40845089499](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40845089499),
PR CPU/Vita [40845101037](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40845101037),
and input [40845101348](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40845101348).
Only after these gates passed did native worker adapter work begin.

Native worker tests reuse the existing 122 translated functions: authored XBE
now has ten sections, adding actual byte views for fixture_thread_args and
fixture_fault_divide. The independent PC parser checks both addresses and exact
file bytes; count/metadata expectations were updated with the fixture extension.
Normal return verifies both contexts and exact ret-8 stack cleanup; independent
worker exit verifies status 37 and its own TLS identity. Tests cover native pin
after guest close/completion, double/concurrent join, retry after injected join
error, injected create EAGAIN with guest-RAM rollback, unknown entry, invalid
stack/identity/overflow, mirror conflicts, recovery-scope rejection, sixteen
relaunches and captured divide failure. Source generator remains authoritative;
no generated C edits or new retail input were used.

An initial new fault test used a mapped contiguous address: the PC shared-map
header oracle permits that read while the portable image policy rejects it.
Replaced that adapter fault-capture case with the existing architectural divide
fixture, supported by both paths. Existing memory/permission tests are unchanged.
The PC-parser oracle initially retained the old eight-section expectation; it
now asserts ten and validates the added authored bytes explicitly. An authored
length typo (10 versus 11 bytes for ret-8 fixture) was corrected.

Final adapter results: pristine PC shared-map/reference, portable host,
ASan/UBSan, ARMv7 Cortex-A9 QEMU and the independent PC parser all passed the
documented full command. Both VitaSDK Release VPKs cross-built. Pristine,
portable and sanitized binaries each passed three extra full repeats; strict
input/Python/whitespace checks passed. Adapter exhaustion also rejects a launch
against 32 existing owner records and cleans its reservation. Native create
EAGAIN and join EINVAL remain visible; retry clears the backend error on success.
No device or retail-title test, hardware FPS or production stack budget exists.

## Transactional storage — 2026-10-29

Clean starting commit `bab27fc07f2d54b3e8ccfb5299b1b639e0a1f36d`; push
[40846789507](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40846789507),
PR [40846799063](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40846799063),
and input 40846799052 all passed. Re-ran the documented full local CPU/ARM
command before modifications; pristine/portable/sanitizer/QEMU baseline passed.

New tests retain 122 translated fixtures and the independent PC XBE/TLS parser.
Three reserved workers execute real argument, TLS and kernel-exit code; verifies
ID, FS/TLS, ret-8 and status 37. Tests cover invalid image roles/permissions,
address/size overflow, pool exhaustion, same-ID reservation races, generic handle
and raw mirror claims, stale lease after cancel/reconfigure, alias-addressed pool
execution, failed backup allocation, injected calloc failure after metadata was
zeroed, and native-create EAGAIN. Rejected/rolled-back cases compare the entire
15 KiB test arena byte-for-byte. Join EINVAL retains ownership for retry; generic
release is rejected even after join. Concurrent image teardown is rejected across
join/release; sixteen full creation cycles test reuse and balanced backup frees.

Forced calloc failure is test-only linker wrapping; production gains no failure
hooks. Pool allocator hooks are explicitly native, non-reentrant and changeable
only without a configured pool. No hardware scheduling or retail title behavior
is inferred from this profile. Exact full-matrix commands remain above.

Final storage matrix passed locally: pristine/shared-map PC and parser oracle,
portable host, ASan/UBSan, static Cortex-A9 QEMU, both VitaSDK Release VPKs,
strict input, Python and whitespace checks. All three host modes passed three
extra full repeats after final source changes. The calloc wrapper is armed only
after main and avoids test TLS access during static-libc bootstrap. No assertion
was disabled, no previous fixture dropped and no dependency pin changed.
Physical Vita/retail/FPS validation remains unavailable.

Storage commit `132f4d31f6e411c67d485af9f6606a700d1fd8c3` passed push CPU/Vita
[40863128451](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40863128451),
PR CPU/Vita [40863135107](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40863135107),
and input [40863135152](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40863135152).
The thread-creation import was implemented only after these checks passed.

Three authored fixtures (parent/child/indirect service call) extend the pipeline
from 122 to 125 functions. Parent executes actual ordinal-255 creation followed
by ordinal-233 wait and ordinal-187 close. Child reads both start contexts and
its own fs:[0x28] thread identity. Two simultaneous native guest callers perform
eight request cycles with independent stacks/TLS/output words and preserved
flags; their children run real translated code. Kernel ordinal/40-byte ABI
lookup is guarded against the pinned bridge source. Authored XBE has twelve
sections; independent parser checks the added byte views. No retail input used.

Tests cover 44-byte full-frame faults, null/MMIO/readonly/cross-boundary output
probes, output alias/pool rejection, unsupported flags/TLS sizes, invalid entries/
stack sizes, native EAGAIN with byte-exact arena rollback, capacity exhaustion,
optional ThreadId, scope-guard cleanup on normal/fault/nested/unregister paths
and cancellation-state restoration. A test-only pthread-start gate holds a
real child live; shutdown refuses it without closing its handle, then succeeds
after actual translated execution. Failed server join EINVAL retains the pool
pin and a later stop joins successfully. Explicit shutdown closes a completed
open managed handle only after callers are quiescent.

Development failures were corrected without relaxing checks: -Werror rejected
misleading indentation; a pristine-header test used an unavailable internal
flags symbol (changed to public CPU capture). An added call fixture overlapped
the original section's padded raw extent; retained the overlap/padding tests and
placed a separately translated call fixture at 0x13a00. Header/parser assertions
now check the twelve-section authored layout. Pure C guards do not claim SEH.

Final creation-service local matrix passed: pristine PC-header/shared-map and
independent parser, portable host, ASan/UBSan, static ARMv7 Cortex-A9 QEMU,
both VitaSDK Release VPK cross-builds, strict input, Python and whitespace checks.
All three host executables passed three additional full repeats on final source.
`thread-service-v1: create=pass args=11,22 wait-close=pass faults=pass
rollback=pass clients=2 repeats=8 shutdown=pass`. No physical Vita, retail title,
gameplay, memory-capacity or FPS measurement is established.

## Event synchronization local validation — 2026-10-29

Baseline was clean 8ca662a676c2d9c9d17f88700dea4004af7b5327, with passing push
40870843472 and PR 40870851118 CPU/Vita runs. Event source adds authored function
0x13b00 (126 total) and the independently parsed 13-section synthetic XBE.
`tools/test_guest.py` passed pristine PC shared-memory/header execution, portable
host, ASan/UBSan and static Cortex-A9 QEMU. Event tests execute real translated
create/set/clear/wait/close calls, not only native handle helpers.

Coverage includes both reset types, BOOLEAN-width decoding, actual previous state,
multiple waiters and unrelated events sharing a condition, signal-before-wait,
clear-after-grant, close with references, image-reset refusal, stale/wrong-type
handles, zero/finite/infinite waits, clock/condition failure injection, full
32-cell exhaustion and transactional translated thread-create failure. Test-only
native-condition gates force races; controlled monotonic clock advancement tests
late signals without depending on scheduler latency. These are correctness tests,
not timing or performance measurements. All previous fixtures remain enabled.

Expected event line: `event-v1: manual=pass auto=pass grants=pass close-refs=pass
deadlines=pass types=pass quota=32 repeats=32`. Final CI evidence belongs to the
commit-specific draft PR checks; local testing alone is not remote verification.

Final event source also passed both hash-locked VitaSDK Release VPK cross-builds,
strict controller tests, Python compilation and git whitespace checks. Pristine,
portable and sanitized host executables each passed three additional full runs.

Event-core commit 86bfe314a504236618ace5de4b27d4dce5104034 passed both CPU/Vita
workflows: [push 40888112505](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40888112505)
and [PR 40888123844](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40888123844).
PR controller workflow 40888123741 passed.

The follow-up attribute profile tests aligned, unaligned and mirrored empty
descriptors, input/output aliasing, each nonzero unsupported field, MMIO, 32-bit
overflow, truncated section extents and an unpermitted mirror-boundary span.
Failures preserve the output canary and registry. The generator checks the pinned
OBJECT_ATTRIBUTES field order; guest decoding never uses a host sizeof(struct).

Final attribute-source matrix passed PC-header/shared-map, portable host,
ASan/UBSan, Cortex-A9 QEMU, both VitaSDK Release VPK cross-builds, strict
controller, Python and whitespace checks. All three host executables passed
three additional repeats, including the complete event suite. The event output
now also reports `attributes=pass`. No test expectations were weakened.

## Acceleration/allocator validation — 2026-10-29

Clean start f95a4d87bd064a8c8abb2a291cca641fe1040999; push40888827153,
PR40888834656 and input40888834655 all passed. Re-ran the full existing
PC/portable/sanitizer/Cortex-A9 baseline before editing; all passed. PC pin and
remote HEAD759f7a0 are equal. Root/upstream rules and runtime guides inspected.

New tests:127 translated authored functions, fourteen-section synthetic XBE,
actual allocation/store/load/free and two concurrent translated API callers
(32cycles each). Reserve/commit/decommit/release, zero/recommit preservation,
canonical/mirror permissions, cross-page all-or-nothing stores, exact release,
double/interior free, flag rejection, invalid/aliased outputs, second-output
fault rollback, exhaustion/fragmentation/no-clamp, ownership conflicts in both
pool-configuration orders and image-reset pins are checked. Existing1518 string
scenarios and294 atomic cases remain enabled. Raw PC lvalues lack software-page
policy; generated policy-fault checks run under portable host/sanitizer/ARM.

`tools/verify_vmem_reference.py --fetch --actual-trace build/guest/vmem-actual.txt`
executes hash-pinned original MIT guest_vmem.c using a bounded host shim, then
compares seven actual API result rows. Only successful release outputs normalize
from freed extent to pinned-PC0/0. Five negative-control unit tests reject missing,
extra, altered or malformed traces and wrong source hashes. Upstream's acceptance
of an extra allocation bit is an executed control, not a behavior to copy.
The main guest runner invokes this gate and compares all CPU-mode transcripts.

`tools/test_graphics.py` runs162 independent BC-order cases, a fixed4x2 order,
span/overflow/overlap/unchanged-output checks on host/sanitizer/Cortex-A9. This
validates the adapted layout only; no shader, native GPU or device test occurred.
The CI adds this gate and strict Vita compilation of the layout adapter.
Exact local CPU/SDK commands remain the locked commands above; graphics uses
the same ARM compiler/sysroot/QEMU options. No assertion or existing test dropped.

A new translated cross-page store regression initially faulted on EXECUTE:
the synthetic XBE had not installed the existing0x13280 store fixture. Added
its authored bytes to the code section and independent parser check, retaining
the exact expected WRITE/width4/PC assertion and unchanged-byte check. No memory
check was relaxed to accommodate the fixture error.

Final local results: PC-header/shared-map, portable host, ASan/UBSan and static
Cortex-A9 QEMU all passed with matching transcripts; the original xboxrecomp
seven-operation differential gate passed. Both locked VitaSDK Release VPKs
cross-built. Graphics host/sanitizer/ARM tests and strict Vita layout compilation
passed. Strict controller, Python and whitespace checks passed. Each of the
three host guest executables passed three further complete runs. Independent
read-only review found no blocking ownership/arithmetic/recovery defect within
the explicit quiescent-lifecycle profile; its suggested cross-page, second-output
and cross-owner tests were added and passed. Hardware and FPS remain unavailable.

Reuse/allocation commit69bb224c3db4451f29afb60fa56664b28e3ece11 passed push
[40970033730](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40970033730)
and PR[40970041590](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40970041590),
both CPU/graphics and Vita jobs, before the output-reference follow-up.

Output-reference tests retain127 translated functions and the independent
seven-row VM comparison. Actual translated create/set publish through canonical
and mirrored heap outputs. A test-only event-create linker gate pauses after
reference acquisition, then translated release/decommit must return UNSUCCESSFUL
with unchanged in/out words. Both succeed after completion/cleanup. Native tests
cover64 pins, exhaustion, stale/double release and slot-generation reuse. Guest
metadata/output faults and an injected post-pin fault release bookkeeping; native
cancellation state is explicitly checked afterward. No production failure hooks.

Final output-reference source passed PC-header/shared-map, portable host,
ASan/UBSan and Cortex-A9 QEMU with matching transcripts and the original VM
comparison. Both VitaSDK Release VPKs cross-built; strict controller, Python
and whitespace checks passed. Each host mode passed three further full repeats.
Read-only review found no blocking generation/lifetime/recovery defect under the
explicit profile; no general native cancellation unwinding or hardware claim.

## V4 issue #2 validation

Started clean at eccc03ec18b3f5d120ee8c76f6813d4cda9b8563 with passing push
40971056312, PR40971064091 and controller40971064058; full local baseline passed.
New translated thread callers use heap results, including separate handle/ID
allocations, mirror addresses, optional null ID and two parallel callers over
eight cycles. Wait and close now also run through actual translated imports.
A test-only RPC gate holds both references while translated release/decommit
attempts fail unchanged. Native EAGAIN compares pool bytes and output canaries;
second-pointer faults and injected post-pin faults release CallGuard references.
63/64 pre-held pins test failure of second/first acquisition; exact mirror and
partial output overlap reject before request submission. Cancellation is checked.

Development fixture failure: added imports initially occupied the child's result
word at0x900b0, producing the wrong call target in the race check. Moved canonical
imports to0x900e0 and copied the original three callable slots to their fixture
addresses; VM race output words are separate at0x90110/114. Exact return/stack/
unchanged-output checks remain enabled. No runtime semantic assertion weakened.

Issue #2 final local matrix passed: pristine PC-header/shared-map, portable,
ASan/UBSan, Cortex-A9 QEMU and pinned VM differential comparison; both locked
VitaSDK Release VPKs cross-built. Graphics host/sanitizer/ARM, strict controller,
Python and whitespace checks passed. Each host mode passed three more full runs.
Read-only review found no blocking guard/lock/lifetime issue. No device/FPS test.

## Issue #3 validation

Issue #2 commit684cbf58c4be1b92dcd30edba4059633c7f22b51 passed push
[40974650576](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40974650576)
and PR[40974655910](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40974655910)
CPU/Vita/graphics checks; controller40974655891 passed. Issue #2 was updated
and closed only after these gates, before issue #3 implementation.

New translated VM cases retain127 functions and the seven-row original-source
differential gate. Output matrices include two heap owners, both mixed directions,
mirror pointers, same-owner self-commit and rejected self-release/decommit. Actual
NtFree requests race a paused allocation callback with both result containers
pinned. Tests retain exact EAX/ESP/flags/FS, byte preservation, quota, malformed
second pointers, partial/exact aliases, ordinary exhaustion/flag errors, and
pre-mutation fault cleanup. Reference release and final heap teardown prove no
leaks; input requests and bindings are caller-synchronized.

The prior test rejecting a heap result pointer now expects CONFLICTING_ADDRESSES:
the pointer is valid under the new profile but contains an out-of-arena requested
base. The preserved data check and new successful heap-output cases distinguish
this intentional supported-behavior change from a weakened assertion. A new test
initially lacked its status-constant header; added that header, no warning/check
suppression. Read-only review found no blocking transaction/lifetime issue.

Issue #3 final local results: PC/shared-map, portable, ASan/UBSan, Cortex-A9
QEMU and original-source VM differential passed with matching transcripts. Both
locked VitaSDK VPKs cross-built; strict input/Python/whitespace passed. Three
additional full runs per host mode passed. No existing fixture/check removed.

Issue #3 commit a02872832fbbeeccf3a1e39eced80261ad2ecdf6 passed push
[40975623375](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40975623375)
and PR[40975628102](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40975628102),
both CPU/graphics/Vita jobs; controller40975628210 passed before intake integration.

## Issue #4 intake-tool subset

`python3 -m unittest discover -s tests -p test_title_inputs.py`:13 pass, using
authored XBE/RET and test-only supported-hash mocking. Production CLI has no hash
override. Tests exercise the real pinned parser, missing/unsupported inputs,
source/provenance/metadata hashes, duplicate JSON, size/path/symlink rejection,
entry/ABI and executable/BSS extent rules, title/region/debug checks, dispatch
coverage and non-mutating snapshots. CI runs this suite explicitly.
`python3 tools/check_title_inputs.py`: expected exit2 with missing_inputs, all
seven required logical slots missing, generation/retail_boot=not_run and
trace_unavailable=true. Nothing proprietary was fetched or executed.
Python compilation and whitespace checks pass. Native runtime source remains
unchanged from the fully verified issue #3 commit. No physical/Vita3K test or
retail trace is established. Metadata attestation is not proof of derivation.


## 2026-10-30: bounded diagnostic telemetry (issue #6, partial)

Added caller-owned fixed256-sample elapsed-time recording, drop/rejection
accounting, saturating typed counters and non-mutating nearest-rank summaries.
The diagnostic can opt in via TSFP_DIAGNOSTIC_TELEMETRY (default OFF); reports
include loop elapsed time and presentation submissions, never game/display FPS.
See [TELEMETRY.md](TELEMETRY.md) for exact contracts, build/test commands,
capture metadata and limits. Guest runtime hooks/device capture are still absent.
No retail inputs, physical console results or performance measurements exist.
Issue #4 still requires the seven inputs in TITLE_INPUTS.md before real tracing.

Local V5 results: the complete existing `tools/test_guest.py` PC/pristine,
portable, ASan/UBSan and Cortex-A9 comparison passed, preserving127 translated
fixtures,1518 string scenarios and294 atomic cases plus VM differential checks.
`tools/test_telemetry.py` passed host, ASan/UBSan and ARMv7 QEMU; direct `-DNDEBUG`
strict C11 compilation also passed with test assertions retained. Independent
review corrected elapsed-time labeling and padding-sensitive test snapshots.
`tools/test_graphics.py` passed all162 layout cases on host/sanitizers/ARM.
The13 title-intake tests, strict controller build/run, Python compilation and
whitespace checks passed. Live title intake still returns2/missing_inputs.

Commands use the documented local ARM environment:

```sh
LD_LIBRARY_PATH=/workspace/toolchains/arm/usr/lib/x86_64-linux-gnu \
PATH=/workspace/toolchains/arm/usr/bin:$PATH \
python3 tools/test_telemetry.py \
  --arm-cc '/workspace/toolchains/arm/usr/bin/arm-linux-gnueabihf-gcc --sysroot=/workspace/toolchains/arm' \
  --qemu /workspace/toolchains/arm/usr/bin/qemu-arm
```

Use the same arguments for test_guest.py and test_graphics.py. Both diagnostic
telemetry OFF and ON configurations cross-built with the existing hash-locked
SDK, `cmake -S ports/vita -B build/vita-locked` and `cmake --build build/vita-locked`.
Final commit/remote CI links are recorded in draft PR #1 after CI completion.
No physical Vita, game boot, GPU execution or device-performance result exists.


## V5 wait BOOLEAN reproduction and correction

Independent scratch reproduction changed only the existing first translated
poll argument from0 to0x100, reused the portable-host compile command and failed
its original TIMEOUT/status/ESP assertion. New tracked512-case tests also failed
on the old dispatcher in host-pristine (`tools/test_guest.py`, SIGABRT). The
runtime fix truncates the slot to uint8_t as the pinned PC bridge does, retaining
all real unsupported low-byte/fault checks. No expected result was weakened.
The source-contract generator additionally guards BOOLEAN=uint8_t, the formal
wait prototype and the existing bridge cast at unchanged runtime/source pins.
Exact existing ARM/sysroot/QEMU and locked SDK commands above apply unchanged.
Final local/CI results are recorded in PR #1 after validation completion.

Corrected dispatcher: complete PC/pristine, portable, ASan/UBSan and Cortex-A9
QEMU comparisons passed locally, followed by three additional complete runs of
each host mode. Both locked-SDK VPK targets cross-built successfully. The earlier
telemetry/layout/input/intake/Python gates remain passed; no unrelated source or
pins changed. Final remote push/PR evidence follows in the draft PR/issue report.
