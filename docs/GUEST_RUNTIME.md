# ARMv7 guest compatibility entry point

The current implementation executes authored synthetic x86 functions translated
by the pinned PC pipeline. It does not load an XBE, initialize Xbox services or
execute any retail game function. Generated C remains ignored under `build/`.

## Reproducible integration

```
git submodule update --init external/pc
python3 -m pip install capstone==5.0.9 pycparser==2.23
python3 tools/test_guest.py
python3 tools/test_guest.py --arm-cc arm-linux-gnueabihf-gcc --qemu qemu-arm
```

`external/pc` is pinned at `759f7a0ecebc2341cdcedf638fcea68c4a7fbf4e`;
`sources.lock.json` repeats the revision and selected input SHA-256s. Preparation
rejects changed tracked or untracked PC sources. The PC importer verifies its
bundled runtime snapshot at `d90c92988a9f99d0580d2ca3f7efd6ec3a7b7845`, reconstructs
its ignored checkout, and the authoritative patch loader applies the complete
PC patch stack. Local fixture generation only uses synthetic bytes and an
installed synthetic section layout. It does not require a game executable.

The ARM-specific patch applies to a build-local copy of `recomp_types.h`, with
zero patch fuzz; the pinned snapshot and patched PC checkout are not edited.
Update the submodule, lock hashes and patch together when intentionally updating
upstream. Never edit `build/guest/fixtures.c` or any generated game translation.

## Minimum source dependency for this subset

- Prepared PC `tools/recomp/{translator,lifter,disasm,config}.py` and their
  imports, bridged to PC `tools/compiler/translation_*.py`, decode/flow/catalog
  helpers and XBE layout/reader. Capstone is the only non-stdlib Python module
  needed by this synthetic translation route.
- Prepared `templates/runtime/recomp_types.h` and
  `src/kernel/recomp_icall_feedback.h`. These supply guest ABI, integer, scalar
  SSE and double-backed x87 helpers.
- `tests/guest_harness.c` supplies the referenced TLS register cells, a tiny
  deterministic function lookup, fault hooks and stack setup. It is fixture
  support, not a substitute implementation of Xbox kernel/HLE/dispatch.
- `guest/memory.{h,c}` supports bounded fixture mode and an explicitly classified Xbox address space in `guest/address_space.{h,c}`.
  All lanes retain little-endian layout. Packed `may_alias` volatile cells make
  upstream lvalue loads and stores valid at unaligned addresses on GCC/Clang.
  Width checks use subtraction, avoiding address/length overflow; invalid
  accesses call a nonreturning platform hook. No fast-math or FP contraction.

The full pinned PC source is retained as one submodule rather than inventing a
fragile partial engine copy. No SDL, renderer, retail assets or Xbox kernel are
needed for the tested CPU subset. PC dependencies not exercised here remain at
their authoritative pins in `external/pc/requirements.txt`; only the required host
build/decoder/parser tools are installed for Vita cross-build CI.

## Tested contracts and limits

35 authored byte sequences cover guest stack arguments and ret, carry, a
callee saving EBX, a direct call with the exact guest return VA, an indirect
lookup, unaligned integer memory, SSE load/add/store, x87 rounding and integer
division; a register-addressed load/add/store fixture exercises all RAM views and contiguous/tiled aliases. Assertions independently check expected results; pristine PC and
portable host/ARM runs must print identical deterministic transcripts.
Additional checks cover invalid/cross-boundary addresses, oversized bindings,
misalignment offsets 0–15, negative zero/unordered comparison helpers, aligned
atomic add/CAS, concurrent increments and per-thread EAX isolation. Portable-only
unaligned tests intentionally do not run through the original undefined typed
lvalues. Host portable tests also run with AddressSanitizer/UBSan. Assertions
remain enabled in Release VPKs.

`tools/test_guest.py` writes commands, compiler versions, exit codes, transcripts,
source revision/dirty state, manifest and generated hashes to ignored
`build/guest/report.json`. QEMU executes actual ARM32 instructions using
`-cpu cortex-a9`; compile-time checks require ARM and 32-bit pointers. This is
CPU emulation for testing an ARM executable, not an Xbox emulation game path.

Not covered: complete x86 flags/instructions, x87 80-bit precision or exception
masks, SSE MXCSR/denormal modes, SEH unwinding, longjmp integration, unaligned
LOCK operations, real guest threading, all indirect targets, device register behavior, demand-mapped RAM mirrors outside the explicit window, GPU memory coherence or game simulation. Upstream missing-target
behavior is counted and tested; this does not mean a retail target is resolved.
The mock divide handler longjmps for assertions, not guest exception delivery.
The memory adapter is a correctness prototype with a check on every access;
no overhead or speed claim is made.

## Xbox address-space adapter

`tsfp_guest_bind_xbox` borrows one exact 64 MiB RAM backing plus an optional,
disjoint exact 64 MiB contiguous backing. The caller owns their allocation,
initialization and lifetime. Failed bindings preserve the existing map.
`tsfp_guest_unbind` invalidates future accesses; borrowed pointers must already
be released and guest threads stopped. This baseline needs no fixed mappings,
kernel plugin or privileged API. It is a PC-compatibility map, not an Xbox MMU.

| Guest range (inclusive) | Classification and storage |
| --- | --- |
| `00000000–03ffffff` | Primary RAM |
| `04000000–73ffffff` | 28 additional views of RAM, modulo 64 MiB |
| `80000000–83ffffff` | Separate contiguous backing, never low-RAM alias |
| `80010000–80010fff` | Kernel-data classification within contiguous storage; no kernel image initialized |
| `f0000000–f3ffffff` | Alias of contiguous backing |
| `fd000000–fdffffff` | NV2A; rejected until a device-service adapter exists |
| `fe000000–fe7fffff` | Reserved synthetic kernel dispatch namespace; not readable storage or proof of a valid call target |
| `fe800000–feffffff` | MCPX devices; rejected |
| `ff000000–ffffffff` | Flash aperture; rejected |
| All other addresses | Invalid; rejected |

This follows the pinned PC `src/kernel/xbox_memory_layout.c` successful 64 MiB
mapping path: 28 low mirrors, separate contiguous window and tiled-to-contiguous
alias. `src/kernel/kernel.h` and `xbox_memory_layout.h` supply generated checked
constants for the test oracle. The PC deliberately separates contiguous and
low RAM to avoid title pools overwriting the image; substituting physical Xbox
aliasing would change its behavior. PC larger-memory modes, on-demand extra
mirrors and device-aperture emulation are not implemented here. Flash/MMIO are
classified but intentionally not backed with ordinary memory.

`tsfp_guest_read/write` validate the complete address range before copying,
support low-RAM wrap and preserve the destination when range validation fails.
They reject zero length, 32-bit address rollover, unbacked regions and host
buffers overlapping either guest backing. These transfers do not provide thread
atomicity. Host buffers must actually be accessible for the requested length.

`tsfp_guest_pointer` only returns spans contiguous in one backing. **Existing
MEM16/32/64/float/double lvalue lowering still faults on a scalar crossing a
physical RAM-wrap boundary**; it never returns an out-of-bounds pointer. Byte
transfers work across that boundary. This is an explicit generator integration
gap, so full memory/runtime acceptance remains incomplete. Classification
changes within contiguous storage, such as the kernel-data page, do not break
otherwise contiguous pointer spans.

`tests/pc_memory_oracle.c` builds a Linux x86_64 oracle with the unmodified PC
runtime header and shared OS mappings matching that policy. MAP_FIXED replaces
only the oracle's own 4 GiB PROT_NONE reservation; ordinary portable/Vita code
uses no such mappings. It verifies the same generated functions through all 29
RAM views, both directions of tiled/contiguous access, low/contiguous independence
and byte-wrap results. This is a host equivalent of the PC mapping policy,
**not execution of the Windows kernel initialization implementation**. The
sanitizer and ARM suites use the portable backings. Classification endpoints,
unbacked regions, invalid rebind preservation, nonmutating range failures and
translated MMIO/wrap faults are regression tested.

The guest-test VPK reserves a 160 MiB newlib heap for its two 64 MiB test backings
and overhead. This is a test allocation setting, not measured Vita memory use or
a production game budget. Allocation success and all device behavior remain
hardware-unverified.

## Next implementation

Continue thread-object/kernel import integration, then Xbox exception delivery.
The REP/string forms documented below now execute through a per-element runtime;
other unbounded guest-pointer escapes remain fatal at generation time. Full
instruction coverage, flags provenance at complex joins and actual title startup
remain separate acceptance gates.

User-supplied US XBE verification must precede retail generation:
SHA-256 `3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc`,
title ID `45410066`. No supported executable is available in this session.


## Split-safe scalar access foundation (2026-10-29)

The 64 MiB primary RAM and 28 mirrors, separate contiguous/kernel-data
backing, tiled alias, and MMIO/flash/kernel-thunk classification were already
implemented and validated before this change. This iteration adds explicit
`tsfp_guest_load_{u16,u32,u64,s16,s32,s64,f32,f64}` and corresponding
`tsfp_guest_store_*` functions to `guest/memory.{h,c}`.

These helpers use the existing *whole-range prevalidated* byte-transfer
engine. Valid scalar accesses straddling a physical 64 MiB RAM wrap split
across the same RAM backing without touching unmapped host memory. The
host PC mapped oracle checks 64-bit scalar accesses on either side of
each of the 28 low view transitions against its unchanged shared-mapping
`MEM64` lvalue. Signed integer and floating-point values are copied as bits:
NaN payloads and negative zero are preserved; no floating-point conversion,
precision or rounding changes are permitted here. Rejected final-view,
MMIO, flash, kernel-thunk and 32-bit-wrap accesses fault before mutation.

The full PC oracle/portable host/ASan+UBSan/ARMv7 QEMU test matrix and
VitaSDK cross-build passed in GitHub Actions
[40722808711](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40722808711)
at commit `f778686d`, with the separate host input workflow
[40722808716](https://github.com/ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-/actions/runs/40722808716)
also passing. These are **source/host/ARM CPU/cross-build** results,
not tests on Vita hardware or proof of gameplay.

**Remaining integration gap:** the pinned recompiler's generated
`MEM16/32/64/MEMF/MEMD` expressions are still lvalues supplied by
`patches/arm-guest-memory.patch`; they deliberately fault for split spans.
The new accessors are available for future *source-generator* load/store
lowering but are not yet substituted into all generated guest instructions.
Do not claim that generated scalar wrap accesses are solved. LOCK/atomic
operations, guest exception delivery and full runtime mapping remain open.

## Generated memory emission integration (2026-10-29)

`tools/guest_codegen.py:configure` installs a process-local adapter around the
**real** pinned `FunctionTranslator.translate_function`, also used by the upstream
batch recompiler. `tools/generate_fixtures.py` uses this same route. The PC checkout,
pristine runtime snapshot and retail generated C are never edited. The adapter
injects `guest/lowered_memory.h` and parses emitted function bodies with pinned
BSD-licensed pycparser 2.23. Directives are preserved explicitly; unknown syntax,
unbounded `XBOX_PTR` escapes, unrecognized borrowed lvalues and hidden memory in
opaque preprocessor macros fail generation.

AST context distinguishes reads from assignments, compound assignments, prefix/
postfix updates, stack operations and atomic pointer arguments. Scalar stores use
GNU statement expressions to capture address/value once and preserve expression
results; fresh temporary names avoid collisions. Width/sign/FP type chooses the
existing typed accessor. PUSH commits ESP after a validated store; memory POP
samples the source and computes ESP-relative destinations using the post-pop
address, but commits ESP only after the destination store succeeds. Locked 32-bit
operations keep upstream atomic primitives and require a complete aligned span;
unaligned or split atomics fault explicitly rather than becoming ordinary stores.

MMX qword, SSE scalar, half-register and full 128-bit memory transfers are routed
to whole-range accessors. This prevents partial vector writes. MOVSS/MOVSD move
raw lanes, including NaN payloads and negative zero, without float conversion.
Regression tests exposed two pinned emission defects: MOVSD stores used `.f[0]`
instead of the double/bit lane, and ADD/SUB overflow conditions were unavailable
for SETO. Narrow source-emission adapters correct these; their opcode-level
expected results are independently asserted. Word PUSH and memory POP fault
ordering is also adapted. No unsafe FP options are enabled.

The mapped-PC baseline uses **unlowered function bodies** and the original PC
runtime memory header, with the same narrowly scoped SSE/overflow/stack emission
repairs. Portable/sanitized/ARM builds use separately generated lowered bodies.
Consequently this is not a claim that the untouched pinned PC compiler already
handled those cases. Linux shared mappings remain the memory-policy oracle;
actual Windows kernel initialization and physical Xbox faults are not executed.

35 authored x86 fixtures now cover MOV/MOVZX/MOVSX, signed x87 integer transfers,
bit-exact SSE/MMX moves, RMW ADD/NOT/XCHG, carry/zero/sign/overflow consumers,
indirect memory calls, effective-address rollover, PUSH/POP and aligned XADD.
All 28 RAM transitions are exercised; vector splits, invalid destinations,
nonmutating failed stores and architectural cells on fault are asserted.
Seven additional source-pass tests cover compound expressions, single evaluation,
temporary hygiene, macro/string preservation, rejected pointer escapes and input
hash rejection. This is tested instruction coverage, not complete x86/SEH/FP or
atomic EFLAGS validation. x87 still uses the inherited binary64 stack, so integer
fixtures use exactly representable values; no 80-bit precision claim is made.

For the guarded actual recompiler entry (requires matching PC analysis metadata):

```sh
python3 tools/guest_codegen.py --help
python3 tools/guest_codegen.py /local/user/default.xbe --destination build/guest-retail --all
```

The CLI checks the supported executable hash before preparation, forces output
under this checkout's ignored build directory, keeps single-function generated
stdout there, and prepares the patched runtime header. Its help and unsupported
input rejection are tested; **supported retail generation is unavailable and
untested without user files**. Compiling generated code requires `-Iguest`,
`TSFP_PORTABLE_GUEST_MEMORY`, and the memory/runtime adapters; this is not a full
title build or loader. `build/guest/report.json` fingerprints original/lowered
outputs, adapter sources and the source/dependency manifest.

## REP/string source integration (2026-10-29)

`tools/string_lowering.py` intercepts real decoded string instructions before the
scalar AST pass. Each emitted call to `tsfp_guest_string` operates on the live
per-thread ESI/EDI/ECX/EAX cells. It supports MOVSB/W/D, STOSB/W/D, CMPSB/W/D,
SCASB/W/D and LODSB/W/D, bare and F3/F2 repeated. SSE MOVSD/CMPSD are kept separate.
Repetition is decoded from preserved bytes: Capstone 5.0.9 drops F2 from MOVSD's
mnemonic/prefix metadata. Address-size 16, unsupported segment/LOCK prefixes and
ambiguous duplicate repetitions fail generation, including at zero count.

No multi-element memcpy/memmove/memset substitution is used. Each MOVS element
loads the whole source before validating/storing the whole destination; STOS uses
the accumulator's low element bits. Only after an element succeeds are pointers
advanced modulo 32 bits and, for repeated forms, ECX decremented. Non-REP forms
ignore ECX. REP zero count accesses no pointers and preserves arithmetic flags.
Repeated comparisons stop according to the newly completed comparison, regardless
of incoming ZF. Forward/backward and physical-alias overlap therefore preserve
x86 element order even when virtual address ranges appear disjoint. A failed
split store commits none of that element, but prior elements remain committed.
`tsfp_guest_range_valid` provides a nonfaulting whole-element probe, using the
existing binding/classification policy; bindings must remain quiescent/stable.

The TLS `g_tsfp_guest_flags` bridge (`g_tsfp_string_flags` compatibility alias) records CF/PF/AF/ZF/SF/OF at operand width,
preserves other caller bits, and tracks DF alongside `g_df`. CLD/STD update both.
Generated condition decoding, LAHF and carry consumers use this bridge after a
comparison. The pinned block's ZF-only seed is replaced with a checked Python
AST source adapter: incoming CMP/ADD/SUB/TEST/logic snapshots seed full flags.
Unknown provenance fails generation instead of inventing flags. Logical AF is
undefined on x86 and the adapter explicitly chooses zero; no hardware equality
claim is made for that undefined bit. Guest thread entry must initialize the
bridge; this is not a complete cross-call/cross-join EFLAGS implementation.

The AMD native host retains the last completed REP comparison flags, but the
Intel Xbox contract restores instruction-entry flags on a repeated comparison
fault. The runtime and independent guest model now enforce Intel behavior,
preserving completed pointer/count progress. Native reference expectations are
separate and strict by vendor; this is not physical Xbox/Vita validation. Native SIGSEGV
reports the first inaccessible byte; guest diagnostics report the requested
logical start and width. Full guest SEH dispatch/resume remains unimplemented.

`tests/string_suite.c` runs 1,518 deterministic data scenarios through actual
translated functions, plus comparison-consumer cases. The independent model
uses canonical byte arrays and separate subtraction/flag arithmetic. The PC
baseline accesses its OS shared mappings per element; additionally
`tests/string_native_oracle.c` executes real host x86 opcodes and captures
RSI/RDI/RCX/RAX/EFLAGS on both success and SIGSEGV. Host native reference is
Linux x86_64 only and is never linked into the Vita binary. Guest 32-bit register
rollover cases use the byte model, since a native 64-bit pointer does not have
32-bit rollover semantics. Sanitizer and ARM/Vita targets execute the generated
portable path. Tests compare bytes, full affected registers, arithmetic/DF bits,
count, fault progress and logical fault metadata, not just helper return values.

Coverage includes both directions, all 28 RAM transitions, virtual-disjoint
physical overlap, partial-element overlap, exact overlap, contiguous/tiled
aliases, early equality/mismatch, zero/huge counts, source/destination/device
faults, prior-element progress, and failed split writes. Native flag comparisons
cover AF/PF/SF/OF as well as carry/zero. Generated LAHF/SETO, conditional branches,
ADC, incoming CMP and CLD/STD consumers verify source-level flag integration.
The test heap/backing sizes are unchanged; no speed or actual device-allocation
claim follows from this implementation.

## Aligned atomic source integration (2026-10-29)

`tools/atomic_lowering.py` adapts actual XADD/CMPXCHG emission before AST memory
lowering. Only dword memory destinations and register sources are accepted;
other widths and register-only forms fail generation. Atomic pointer validation
still requires one aligned accessible span. This deliberately rejects unaligned
x86 atomics rather than claiming equivalent bus/locking behavior.

Address, source and compare values are captured before architectural writes.
XADD returns old memory in the source register and publishes addition flags;
CMPXCHG publishes accumulator-minus-old-memory flags, updates EAX only on failure,
and stores the source on success. The pinned atomic primitive remains the RMW;
no ordinary scalar load/store replaces it. Validation faults precede register or
flag changes. Native-x86 comparisons test all six arithmetic flags, DF preservation,
changed stores on successful comparisons, failures and arithmetic edges.

Consumers use the shared TLS flags bridge, including predecessor ZF publication;
this is not complete EFLAGS flow across arbitrary calls/joins. Two translated
workers increment through distinct RAM aliases and retain independent TLS cells.
No production guest scheduler, Xbox exception delivery or title thread entry is
implemented by these tests. PC/runtime pins and third-party provenance are unchanged.

Native reference caveat: for repeated MOVS between different mapped views, the
Linux oracle sequences actual single-element x86 instructions. Modern fast REP
engines produced runner-dependent results with physical synonyms; they are not
used to define the guest's sequential alias semantics. Ordinary native REP,
independent byte modeling and actual translated host/ARM checks remain enabled.

## Execution and thread contracts (2026-10-29)

`guest/execution.h` defines a snapshot ABI independent of register-name macros.
`tsfp_guest_run` installs a linked, thread-local C boundary and returns either
normal completion or a captured event. `tsfp_guest_raise` captures GPRs, PC,
FS/SEH state, x87 storage/control, MMX/XMM bits and flags plus a validity mask.
Arithmetic bridge bits not published by an adapted instruction are marked unknown;
this is not complete EFLAGS, x87 80-bit precision or MXCSR emulation. The generator
records source instruction addresses before emission; existing faults retain
register/memory progress, including pushes before unresolved indirect dispatch.
Read/write/atomic metadata is attached by checked memory entry points; borrowed
lvalues intentionally report unknown access direction. Binding errors are not
architectural instructions and have no guaranteed instruction site.

Recovery is C `setjmp`/`longjmp` with explicit constraints: callbacks may execute
only resource-free C translated frames/adapters. No held locks, VLAs, C++/foreign
frames, cleanup handlers or resources needing unwinding may be crossed. No async
signal or native exception recovery is installed. Nested scopes restore the
previous boundary; the frame's pointers are initialized before setjmp and not
modified thereafter. Events are captured into caller-owned storage. Without a
boundary, raising aborts. Platform fault/dispatch hooks must call this API; test
hooks do so inside a boundary and retain old oracle behavior outside one.

`tsfp_guest_thread_enter` validates an already-owned mapped stack and FS/TLS range,
resets guest CPU/vector/FP/flags state and attaches identity on the current native
thread. It refuses duplicate attach or attach inside a recovery scope. Leave is
explicit after boundary return; thread exit is a captured event with status, not
an abrupt native pthread exit. Ownership, native creation/join, scheduling and
protection from overlapping guest allocations remain platform responsibilities.

`tsfp_guest_tls_prepare` stages at most 64 KiB before publishing a whole validated
block: 0x40-byte minimal TIB, copied template and zeroed thread data. Own SEH head
is FFFFFFFF, fs:[4] and TLS slot zero point at thread data, following the pinned
PC `xbox_AllocThreadTib` contract. Input ranges and sizes are checked before
allocation; the binding must remain stable and destination exclusively owned.
Template aliasing is safe through staging. fs:[0x28] thread object, XBE template
extraction and complete Xbox TLS initialization remain unimplemented.

The translated fault/thread fixtures are independent of retail data. The
[boot dependency map](TITLE_BOOT_DEPENDENCIES.md) identifies integration order;
C recovery alone is neither Xbox SEH nor a completed title runtime.

REP fault correction: the earlier partial-comparison flag observation was from an
AMD native host and is not the Intel Xbox contract. Repeated CMPS/SCAS restore
entry EFLAGS on a fault, retaining successful iteration index/count progression
(Intel SDM REP description). The independent guest model now requires this.
Native comparison uses explicit host-vendor expectations, never normalizes actual
captured flags, and rejects unknown vendors. See VALIDATION.md and the standalone
`tests/rep_fault_probe.c`; native host behavior is distinct from guest semantics.

## Narrow kernel/thread-object bridge

`guest/kernel.{h,c}` adapts canonical function import slots to the pinned PC
namespace `FE000000 + slot*4`. Initialization reads up to 16 high-bit ordinal
markers, validates the entire input, stages slot VAs, then publishes guest/native
tables. Binding is allowed only with all guest execution stopped. Unknown function
ordinals have an explicit `TSFP_FAULT_IMPORT` event with ordinal/target, not a
successful fallback. Data exports, title-specific remaps and production import
capacity are not implemented. The generator verifies pinned ordinal, argument
byte-count and object-layout anchors before building tests.

Three adapters exist: KeGetCurrentThread (104), RtlZeroMemory (320), and
PsTerminateSystemThread (258). Arguments and return slot are read in one checked
frame before side effects. Successful returning adapters consume return plus
stdcall argument bytes. Zeroing validates the whole destination before writes,
uses bounded constant zero chunks, and ignores invalid destinations at zero
length. Termination captures exit status without returning into guest code;
platform cleanup must occur after the execution boundary returns.

Thread objects are 0x200 bytes with identity at +0x12c, following pinned
xbox_AllocThreadTib. Preparation stages a caller-supplied template, patches ID,
and installs fs:[0x28]. TIB/object physical alias overlap is rejected. The caller
owns all storage; no arbitrary default object fields or fake handles are invented.
Current-thread lookup checks mapping and identity against the attached context.
Object handles, synchronization, allocation ownership and native scheduler remain
open. Stack bounds still come from explicit thread-entry validation, not invented
fields inside the Xbox object.

The translated synthetic entry calls all three adapters via guest import slots,
observes its current object, zeroes guest RAM, and terminates with status 37.
This is an asset-free runtime integration test, not an XBE parser, section loader
or retail title entry. Read/write/execute section permissions remain unimplemented.

Supported import handlers are stable function pointers independent of subsequent
lookups; unsupported slots each have a fail-closed handler carrying their slot's
ordinal. Cached handlers must be discarded when the quiescent import table is
replaced/reset. `tsfp_kernel_imports_reset` removes bindings before image teardown.
The entry fixture also executes actual FS-relative x86 loads of fs:[0x28] and
fs:[4]/thread data; these pass through the real scalar lowering pipeline.
