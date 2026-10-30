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

## Bounded image contract

`guest/image.h` accepts normalized host descriptors, not arbitrary native pointers
from an XBE. The host caller provides accessible buffers for file-backed bytes,
a pure trusted compiled-function resolver, and exclusive/quiescent guest binding
ownership (including released borrowed pointers). There is one image per address
space. Limits are 16 sections and 1 MiB total initialized virtual size; these
are synthetic-test limits, not a retail title capacity claim.

Initialization validates nonempty sizes, source lengths/pointers, address overflow,
RAM classification, flags and canonical physical overlap, including mirror wraps.
Only low RAM and its explicit aliases are accepted; contiguous/tiled/kernel/device
placement is rejected. All sections must be readable; writable/executable mixtures
are rejected until self-modifying code semantics are established. File bytes are
copied and the rest zero-filled in one bounded temporary. Allocation has an
injectable paired platform interface; allocation failure changes no guest state.

Thread/TLS/object/stack ranges must be contained in writable declared sections;
imports may be in readable non-executable sections and are patched only before
final permissions publish. These destination ranges must be physically disjoint
and bounded; template sources must not overlap the
mutated regions. TIB/TLS/thread-object initialization follows existing PC layout.
Stack bytes are zeroed. Import markers are checked before commit; unknown bounded
ordinals may bind but execution produces the existing explicit import fault.
Entry must lie in file-backed executable bytes and resolve to trusted compiled C;
RAM bytes are never cast to native function pointers. All fallible validation and
allocation precede guest writes. Commit assumes stable bindings/exclusive ownership;
an unexpected internal inconsistency aborts rather than pretending to roll back.
Successful publication installs imports and the immutable image access policy.

Checked byte/scalar/atomic/string accesses enforce image read/write permissions
using physical offsets, so mirror aliases cannot bypass them. Borrowed lvalues
must have write permission because the adapter cannot know their use direction;
read-only data must use explicit read helpers. The original raw PC lvalue oracle
is intentionally not a software-permission implementation. Actual translated
read-only store assertions run portable/sanitized/ARM; common checked helper and
instruction-guard assertions run every mode. Software guards are not host MMU
permissions and cannot contain arbitrary malicious native adapters.

`tsfp_guest_instruction` records and checks each emitted instruction address.
Execute checks allow file-backed executable spans only, including direct-call
callees. Repeated comparisons restore Intel entry flags on both mapping and
software-permission faults. Physical range probes remain separate from permissions.
Bind/unbind is rejected while a policy is installed; teardown removes the policy
and import bindings without clearing RAM or touching files/saves.

`tsfp_image_run` attaches/reset the initial CPU context, pushes a root return
marker, enters the captured C execution scope, and detaches after return/event.
Entry is one-shot until reset/reinitialization; nested runs/reset are rejected.
The test image uses actual translated FS/TLS/import instructions and exits 37.
No XBE parsing/signature validation, production section capacity, native page
protection, retail dispatch catalog or complete Xbox title boot is implemented.

## Raw XBE structural normalization subset

`guest/xbe.{h,c}` consumes a bounded accessible host byte buffer and produces
borrowed normalized section views. It does not open files or authenticate a game.
Output remains byte-for-byte unchanged on failure; output/input overlap is rejected.
It checks magic/header/image sizes, 56-byte section records, section count/budget,
virtual and file extents/overlap, arithmetic overflow, supported flags and file
backing. Padded raw section data is clipped to virtual size, matching PC loading.
Only ordinary W/PRELOAD/X section flags are currently supported; RWX and additional
inserted/head/tail flags are explicitly rejected. Empty sections are unsupported.

Canonical retail/debug entry and thunk keys come from pinned PC xbe_image.py.
A key pair must produce a file-backed executable entry and a bounded, terminated,
valid ordinal table. The decoder reports TLS directory address and stack commit;
TLS directory words must be file-backed; their contents now drive the bounded
TLS initialization profile below. Caller supplies owned thread/stack/object
placement and per-thread-data capacity.

Both encodings of an entirely authored XBE buffer now normalize and execute the
same translated entry to exit 37. The pinned PC parser independently checks their
entry, sections, key selection and imports. The synthetic files/certificates live
only under ignored build output; they are not retail images. Certificate/digest
verification, full XBE semantics and actual supported-title compatibility are not
established. Retail use still requires the existing supported SHA-256 gate and
matching ignored generated code/analysis metadata. Read-only import data is
patched during quiescent initialization, then retains read-only guest permissions.

## XBE TLS profile and index policy

`guest/tls.c` parses the six TLS words from declared file-backed sections and
checks raw end/start ordering, initialized backing, 64-bit rounding arithmetic,
bounded block size, writable aligned index destination and role overlaps through
RAM aliases. Directory/index/raw/callback overlaps fail closed. Metadata in XBE
headers (outside declared sections) remains unsupported by this image profile.
Nonzero characteristics are rejected pending an established alignment contract.

The pinned PC runtime computes `round_up(raw_size + zero_fill,16) + 4` bytes.
fs:[4] points to the end; the guest index is `-(block_size/4)`. Slot zero is
reserved and replaced with the pointer to zeroed per-thread data; remaining raw
bytes are preserved. Zero-fill, alignment padding and thread data are zeroed.
Object+0x28 links to the current thread's TLS block; fs:[0x28] links the object.
The existing synthetic/manual template-size path remains available without a
TLS directory. XBE-derived values are reparsed/validated in image preflight,
not trusted merely because a host caller supplied a TsfpTlsSpec.

Upstream deliberately leaves the index untouched because titles compute/store it.
`TSFP_TLS_TITLE_INDEX` preserves that behavior. `TSFP_TLS_WRITE_INDEX` explicitly
writes the same negative value in the validated staging phase, never by bypassing
final write permissions. Invalid index destinations or collisions abort before
publication even with title-owned indexing. The optional policy is a tested host
configuration, not a claim that all Xbox titles require loader-owned indexing.

Callbacks are accepted only as a null pointer or file-backed empty table. Any
non-empty table, even a valid compiled entry address, returns
`TSFP_TLS_CALLBACK_UNSUPPORTED`; no callbacks are looked up/executed. Detailed
normalization errors are exposed by tsfp_xbe_normalize_ex. Diagnostic output
storage is caller-owned, accessible and distinct from file/normalized output.
Full callback dispatch is a separate unimplemented lifecycle contract.

`tsfp_guest_tls_prepare_xbe` prepares a bounded thread block from a stable guest
raw template, using one temporary freed before execution. It validates complete
read/write spans before allocation and rejects source/destination physical alias
collisions. The caller owns disjoint storage and keeps templates/bindings stable;
workers are prepared while guest execution is stopped. No persistent template
cache or native-thread scheduler is introduced. The legacy preparer retains its
original already-sized-template contract and alias-safe staging behavior.

`tsfp_thread_object_prepare_tls` patches the worker's own object TlsData pointer
and identity; its PC-layout block must be tib+0x40. This deliberately avoids the
pinned worker clone inheriting the main object's TLS pointer. Initial PC layout
and worker isolation are checked separately; no byte-identical Win32 scheduler
claim is made. Thread entry checks permissions and active-image stack/TLS physical
alias conflicts before resetting CPU state. Native creation/join remains explicit
platform ownership, outside guest recovery scopes.

## Bounded thread ownership and close boundary

`guest/handles.c` owns 32 fixed records. Tokens use the PC bridge's opaque
0x48000000 namespace, a slot and monotonic generation; exhausted generations
retire rather than wrap. Object/id validation precedes locking. Duplicate IDs
and physically overlapping object storage (including mirrors) are rejected.
Caller-owned guest storage stays pinned until termination, handle close and
explicit owner release. Created-to-terminated cancellation permits native
startup rollback. Completion status is immutable after the first finish.

NtClose (187, one stdcall argument) invalidates guest access without terminating
native execution or dropping owner storage. Invalid/stale/double closes return
0xc0000008, using the PC file-kernel invalid-handle status; permissive bridge fake
handles are deliberately unsupported. This is only the managed-thread subset.
Imports prefetch checked stack arguments before any side effects. Mutex-protected
operations never access guest memory or invoke recovery while holding the lock.

Native owners create/join outside guest recovery, and finish only after execution
has stopped. They serialize image setup/reset with owner creation. Image teardown
checks all owner records, including closed guest handles. This does not enforce
OS joining automatically, create native threads, implement duplication/waits,
or adapt PsCreateSystemThread. Concurrent close/finish/query are supported.

### Nonblocking completion poll subset

Ordinal 233 `NtWaitForSingleObject` now accepts only a non-alertable wait with
a readable 64-bit timeout equal to zero, for registered thread handles. Running
and created records return STATUS_TIMEOUT (0x102); completed records return
SUCCESS repeatedly without consuming completion. Closed/stale tokens return
INVALID_HANDLE. The mutex orders polling with finish/close and no reference
escapes the lock. Stack/timeout reads precede registry access. Null/infinite,
nonzero relative/absolute and alertable modes raise an explicit import fault
without changing EAX or performing a wait. This is deliberately not a complete
Xbox wait implementation: blocking wait references, deadlines, APCs and events
remain unimplemented. No clock or pthread wait semantics are assumed.

## Blocking completion waits (bounded non-alertable profile)

NtWaitForSingleObject (233) accepts null/infinite and zero/negative relative
timeouts. Negative values are unsigned-decoded 100 ns magnitudes, including
INT64_MIN; positive absolute FILETIME deadlines and all alertable modes fault
explicitly pending guest wall-clock/APC contracts. Relative intervals retain
100 ns units rather than copying the PC host's millisecond quantization.
The authoritative source is pinned `kernel_sync.c` timeout/result mapping.

A monotonic pthread condition variable and registry mutex atomically acquire
a reference while the handle is open. Closing later prevents new waits but
does not cancel acquired waits. Completion broadcasts and remains signaled.
Owner release requires zero waiters; closed referenced objects continue to pin
the image. Predicate checks and wakeups share the mutex, avoiding lost wakeups;
completion observed under that mutex wins a simultaneous deadline boundary.
Spurious wakes recheck the predicate. Relative waits use a fixed monotonic
deadline; at most one-second native timed sleeps avoid time_t overflow on very
long waits, without polling/spinning. Up to 64 references per record are bounded;
exhaustion returns INSUFFICIENT_RESOURCES. Native clock/condition errors return
UNSUCCESSFUL after reference cleanup, never success. There is no per-wait heap
allocation. Failed monotonic-condition setup fails closed.

All guest argument/timeout reads occur before native references or locks. There
is no guest callback, longjmp or guest access while waiting. Native cancellation
is disabled while locks/references exist and restored only after cleanup; guest
termination is separate from pthread cancellation. Async signal-based recovery
and forced native thread killing are unsupported. Registry/image lifecycle
serialization and joined/stopped-owner rules remain required. This provides
completion waits, not a guest scheduler or event/APC implementation.

## Bounded native worker launch/join adapter

`guest/worker.c` reserves at most 32 caller-owned worker records with disjoint
low-RAM guest stack/TIB/object spans, checked permissions and matching identity
links. Range checks precede reservation; identity reads follow reservation to
avoid reading another active worker's private state. The trusted compiled entry
resolver never casts guest RAM to a native function. Callers prepare stable
private TLS using the existing helpers and retain all native/guest storage.
Other manually managed thread storage must also remain disjoint by caller contract.

`platform/threads_pthread.c` owns native creation/join via public pthread APIs,
including VitaSDK's implementation. Backend stacks are distinct from guest
stacks, explicitly sized between 64 KiB and 1 MiB, and backend-owned until join.
This cross-built backend is not a claim that Vita scheduling equals Windows.
Native create failure rolls back owner, native pin and reservation without
writing guest RAM. Backend error codes remain available in platform_error.

The native entry disables forced cancellation, initializes per-thread CPU state,
pushes two start contexts and a root return marker (PC bridge convention), and
executes inside the existing C-only fault boundary. Normal return yields native
status zero as upstream does; CPU result registers are captured independently.
PsTerminateSystemThread yields its requested exit status. Other guest faults
remain captured with failing status, never successful termination. The thread
leaves guest state before publishing completion.

A separate owner native pin survives guest handle closure and completion until
a successful native join. Join failures retain the pin and permit retry; only
one concurrent joiner can succeed. Release requires join, guest handle close
and no wait references. Joined output is copied under the controller lock
before release is permitted. Image teardown remains blocked through all pins.
Launch/join/release reject guest recovery scopes; no resource-owning native
thread operation is called from translated callbacks by this adapter. Image
lifecycle remains externally serialized. The API does not allocate guest stacks
or implement PsCreateSystemThread, priorities, suspension or a title scheduler.

Next integration gate: bounded guest stack/TIB/object reservations with rollback,
then a safe kernel-service boundary for thread creation. Do not directly call
this host adapter from inside a translated recovery callback.

## Transactional guest thread storage

`guest/storage.c` configures an explicit, caller-ceded writable BSS pool with
up to 32 fixed-stride slots, never more than 1 MiB total. This is a bounded
synthetic image profile, not automatic scavenging of title BSS or a production
heap. The caller must know that the arena has no live title globals. Structural
checks reject initialized/executable sections, mirror self-wrap, initial stack/
TIB/object/imports/TLS metadata and template overlap. Configure requires no live
workers or handles and externally serialized image lifecycle. A configured pool
pins the image, even when all slots are free, until explicitly disabled.

A request supplies a unique nonzero thread ID, 16-byte-aligned guest stack size
and compiled entry/contexts. Stack, aligned TIB/TLS/thread data and 512-byte
object are laid out inside one slot with checked 64-bit arithmetic. Metadata
is built using existing TLS/object helpers. Only metadata is backed up; stack
bytes are neither copied nor cleared. Lease publication follows successful
initialization. Backup allocation, partial TLS initialization and native-create
failures restore all previous metadata and leave stack bytes unchanged. Cancel
restores pre-reservation contents; successful launch commits initialization and
frees the backup. Native error codes are available on failed launch.

Leases use a separate 0x53 generation/slot namespace; they are native control
tokens, not guest handles or pointers. Generations survive disable/reconfigure
and retire rather than wrap. Prepared/launching/running/joining/joined/releasing
states reject stale or conflicting operations. Managed owners retain their lease
identity: generic owner release cannot free a storage-managed record. Release
requires native join, guest-handle close and drained wait references. Launch/join
defer cancellation through outer lease-state publication as well as inner native
ownership transitions. Query retains the published handle across join states.

Raw workers, handles and pool reservations share one ownership mutex (before
the handle-registry mutex). Atomic checks prevent raw claims through RAM aliases
or duplicate IDs into reserved storage. Pool-owned worker operations use the
existing worker adapter and native pins; initialization holds no guest recovery
boundary. Bindings/permissions/templates remain stable while configured, and
allocator callbacks must be native, non-reentrant and non-unwinding. No title
thread-creation import or production allocator is implied by this host API.

## PsCreateSystemThreadEx controlled service boundary

Ordinal 255 decodes ten 32-bit stdcall arguments (40 bytes); the complete frame
is checked before any service ownership. ThreadExtraSize, CreateSuspended and
DebugStack must be zero. KernelStackSize zero selects an explicit host default;
positive sizes must be 16-byte aligned and fit the configured slot. TlsDataSize
zero selects the image profile; a nonzero value must match its thread-data size.
Unsupported modes return NOT_SUPPORTED, invalid entries/sizes return
INVALID_PARAMETER and exhausted/failed native resource creation returns a failing
status. The pinned bridge's fake handles/unconditional success are not copied.

`guest/thread_service.c` is a bounded synchronous native request service. It
starts outside guest recovery and owns up to 32 created leases before replying.
Guest output pointers never enter the packet: only native scalar copies of
stack/TLS sizes, compiled entry and two opaque contexts are transferred. Actual
reservation, native creation and completed-worker join/release run on the
service thread's separate C stack. The translated caller waits on condition
predicates, retains its C continuation and resumes with exact stdcall cleanup.
No resource-owning creation/join frame can be unwound by that caller's longjmp.
One request executes at a time; clients/references are bounded to 32, no spin or
per-request packet allocation is introduced. The service uses public pthreads.

`tsfp_guest_scope_guard` provides one durable native bookkeeping guard per native
thread, associated with its exact C execution boundary. Contexts must be static/
TLS, not unwound callback locals. Registration lives in TLS outside setjmp's
automatic storage, avoiding indeterminate modified-local values after longjmp.
Cleanup runs once on scope exit after the guest callback has unwound. It cannot
raise faults, execute guest code or allocate/join native resources. Nested
boundaries do not run an outer guard; extra guard registration fails explicitly.
This is neither Xbox SEH nor general C++/pthread cancellation unwinding.

The creation call defers native cancellation and holds a guarded service
reference through output probes, request/reply, stores and CPU/stack publication.
Probe faults release that reference in scope cleanup; the reply packet and any
created worker remain service-owned. Both outputs are fully validated before
creation. Invalid memory faults; physical output overlap or writes into the
managed arena/templates are rejected. Checked stores occur only after successful
creation. Service/image pins keep mappings and permissions immutable throughout.
The guard is removed only after EAX/ESP and valid outputs are published.

The service pin prevents pool disable through server join. Normal reaping joins
only completed, closed guest handles and retains referenced objects. Explicit
native shutdown requires quiescent guest callers, refuses live/referenced
workers, then closes completed managed handles and joins/releases them. A failed
server join retains STOPPING/pins and can be retried. Shutdown is not a guest
thread-kill operation. Root/controller ownership and image lifecycle remain
explicit; no full Xbox scheduler or PsCreateSystemThread alternative is supplied.

## Typed events and completion waits

The same 32 generation-checked cells hold threads or events; thread-owner APIs
reject event cells. Manual-reset (type 0) set grants all registered waiters;
auto-reset (type 1) grants one, or retains one coalesced signal if none is waiting.
Clear resets the retained signal without revoking committed waiter grants.
NtSetEvent returns the actual prior retained state through its optional LONG
output; the pinned PC implementation writes a documented placeholder zero.

All predicates, grants, deadline checks and reference transitions use the registry
mutex. A shared monotonic condition broadcasts to avoid waking only a waiter on
another object. Spurious wakeups recheck the object's predicate. Relative negative
100-ns timeouts, zero polling and null/infinite waits retain existing semantics.
Set checks finite waiters' deadlines before granting; clock failure leaves state
unchanged. Waiters are bounded at 64 per object, with stack-local nodes only while
native cancellation is deferred. There are no guest callbacks or guest memory
accesses while nodes are linked under the registry lock. Closing a handle does
not cancel existing references; the final closed-event reference retires its cell.

Create/set output spans are checked before mutation and cannot target managed
storage/templates. Event exhaustion maps to insufficient resources, including
thread creation through the shared registry, with storage rollback. Nonempty
ObjectAttributes and unknown event types fail explicitly. Unsupported alertable,
absolute, named, pulse and multiple-object semantics remain outside this subset.

NtCreateEvent also accepts a non-null, readable 12-byte guest descriptor only
when RootDirectory, ObjectName and Attributes are all zero. The full descriptor
is read before output validation/allocation; unaligned and low-RAM mirror aliases
use checked transfers. Names, directories and all flags return NOT_SUPPORTED;
malformed descriptors fault with a 12-byte read and publish no object. Output may
alias an empty descriptor because the input snapshot precedes publication.

## Bounded reserve/commit service profile

`guest/heap.{c,h}` adapts xboxrecomp's MIT page-owner/rounding algorithm at the
revision in ACCELERATION_AUDIT.md. It suballocates existing RAM only; no new VM
mappings/backings. A native owner explicitly cedes 64KiB-aligned writable image
BSS, at most 1MiB, while lifecycle is quiescent. Existing image arena validation
excludes initial stack/TIB/TLS/object/templates/import roles; the shared storage
ownership mutex excludes managed/raw worker claims. Configuration pins image
reset even when empty. Disable requires no allocations, owners or workers.

Ordinals184/199 decode 20/12-byte stdcall frames, snapshot/probe both in/out words
and reject physically overlapping outputs before mutation. Output pointers must
remain valid under existing quiescent image bindings and be outside the ceded
heap and managed thread pool/templates. The supported allocation flags are
MEM_RESERVE(0x2000), MEM_COMMIT(0x1000), or both, ZeroBits=0, PAGE_READWRITE(4).
Reservations round hints down to64KiB and extents up to4KiB; null hints choose a
free64KiB-aligned span. Commit requires every page to belong to one reservation.
Reserve-only, free and decommitted pages deny access through all RAM aliases;
committed pages allow RW, never execution. Newly committed pages zero; repeated
commit preserves data. Unsupported flags/protections/ZeroBits return NOT_SUPPORTED.
No large-reserve clamp or no-op commit is inherited.

MEM_DECOMMIT(0x4000) requires a positive fully-owned range, returns its rounded
extent and revokes access; recommit zeroes it. MEM_RELEASE(0x8000) requires exact
canonical reservation base and size0, frees the full reservation and clears both
outputs (pinned PC convention). Interior/unknown/double frees fail. No guest
access, allocation or callback occurs under the allocation mutation lock; a
validated native view is retained for zeroing while image/backing are pinned.
Raw native addresses never become guest addresses. Generation leases are not
pretended for plain VM addresses: a stale pointer after a released address is
reallocated cannot be distinguished from its new owner, as with raw guest VAs.
Callers must synchronize access/free and retain exclusive arena ownership.

Important bounded limitation: VM in/out words and thread-create outputs in this
heap remain rejected. Event output words now have lifetime pins, described below;
do not remove the other guards before their own publication lifetime is proven. General Xbox VM, guard/read-only/executable pages, MEM_TOP_DOWN,
allocation queries/protection changes and automatic retail heap discovery remain
unsupported. The raw PC-header oracle does not implement software page policy;
checked helper faults run in all modes, translated policy faults specifically
run in portable host/sanitizer/ARM modes. No retail compatibility is claimed.

### Pinned event outputs

NtCreateEvent handles and NtSetEvent previous-state words may now reside in a
committed heap allocation. After non-mutating guest probes, native pin acquisition
revalidates page ownership/permissions under the shared ownership mutex. It uses
64 bounded generation-checked native reference records; guest code never receives
these tokens. A stale/double unpin fails and exhausted generations never wrap.
Reference acquisition publishes no event on failure. Pins cover one allocation;
release/decommit of that allocation returns STATUS_UNSUCCESSFUL unchanged until
references are released. This conservative transient-failure profile is not a
claim about a complete Xbox VM locking implementation.

The existing C-only durable scope-guard contract is reused: one TLS event guard,
native cancellation deferred, no guest access or callbacks under ownership locks.
It spans probes, event state mutation, result writes and EAX/ESP publication.
Normal return unregisters then releases; a captured fault releases from the
execution boundary's cleanup path. No callback-stack local survives longjmp and
no resource-owning native thread creation is added. Setup/teardown still require
external quiescence and guest data/free races remain caller-owned. Thread-create
and VM output profiles retain their stricter checks pending separate integration.

## Heap-resident thread outputs (issue #2)

PsCreateSystemThreadEx reuses its existing CallGuard for up to two heap output
references. Each independent handle/optional ID span is probed, role-checked and
pinned before RPC. Physical aliases/partial overlap are rejected. The scalar
service packet never retains guest output pointers. Cleanup unpins before
decrementing active_calls and restoring cancellation, so shutdown cannot pass
through unfinished publication. References span result writes and EAX/ESP.
A second-probe fault, pin exhaustion, invalid request or native create error
releases every acquired reference without publishing outputs. No second scope
guard or native creation on a recovery callback stack is introduced. The storage
output helper is only a role predicate; lifetime is owned by CallGuard.

## VM in/out publication (issue #3)

The existing event guard is now the two-reference KernelOutputGuard for imports
184/199/189/225. Both VM words are probed, role-checked and pinned before either
input value is read. Physical overlap rejects before mutation. Native allocation/
free receives only snapshots; valid output spans and their permissions remain
stable through stores and EAX/ESP, followed by guard cleanup. No guest access
occurs under the ownership mutex. Failure-atomic means rejected/faulting probes
and requests do not mutate allocator state or output words; it does not promise
an atomic two-word snapshot to unsynchronized readers. Callers must synchronize
word contents and target lifetime. Stable image bindings remain required.

| Output locations / operation | Supported profile |
| --- | --- |
| Two image words; heap/image; image/heap; distinct heap allocations or disjoint words in one allocation | RW-valid, role-allowed, physically disjoint spans retained before reads |
| Reserve/commit with independent result storage | Existing exact flags, rounding, zeroing and no-clamp rules unchanged |
| Commit over already-committed output pages in the same reservation | Allowed; recommit preserves those bytes |
| Release/decommit any reservation containing either output word | Rejected UNSUCCESSFUL unchanged while pinned, including non-overlapping pages of that owner; relocate words and retry |
| Exact or partial physical output alias | INVALID_PARAMETER unchanged |
| Invalid/readonly/truncated pointer | Captured guest fault before mutation, with all earlier references cleaned |
| Reference exhaustion or unsupported modes | Existing explicit error, unchanged outputs/state |

Fault and error cleanup reuse the durable TLS scope guard and cancellation
deferral. There is no competing guard, native thread frame or rollback buffer.
Published stores cannot fault under the validated binding/permission/lifetime
contract. Concurrent guest changes to the words are outside that contract.
Managed thread-pool/template output roles remain excluded; neither this profile
nor the allocator is complete Xbox virtual-memory/scheduling support.


## 2026-10-30: wait-import BOOLEAN ABI correction (issue #7)

NtWaitForSingleObject Alertable now decodes only the low8 bits of its four-byte
stdcall slot, matching pinned BOOLEAN and bridge casts. High bits alone no
longer cause an unsupported-import fault. Every nonzero low byte still faults
before reading Timeout; alertable waits are not implemented. The existing real
translated CALL fixture now exercises512 high/low-byte combinations, completed
threads, exact EAX/ESP and invalid-timeout fault ordering. Source-generation
checks pin the authoritative type/signature/cast; source pins remain unchanged.
This fixes a demonstrated import defect, not full Xbox ABI/runtime compatibility.
Authentic game-entry tracing still requires the seven missing TITLE_INPUTS.md
inputs; no retail/hardware/performance behavior has been established.


## Captured fault export and offline mapping — 2026-10-30

Added bounded integer-only TsfpFault JSON export after the existing recovery
boundary, with no guest reads or cleanup/recovery changes. The offline mapper
uses declared end-exclusive function ranges, reports unverified_local_map
assurance and never guesses nearest symbols or stack frames. See
[FAULT_TRACING.md](FAULT_TRACING.md) for schema, commands and limits. Real
translated fixture faults validate this path, not retail execution. Runtime
timing hooks, actual title inputs, full stacks/SEH and physical Vita captures
remain incomplete; issue #4's seven-file input gate is unchanged.
