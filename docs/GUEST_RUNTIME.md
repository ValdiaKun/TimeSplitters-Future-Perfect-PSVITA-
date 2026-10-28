# ARMv7 guest compatibility entry point

The current implementation executes authored synthetic x86 functions translated
by the pinned PC pipeline. It does not load an XBE, initialize Xbox services or
execute any retail game function. Generated C remains ignored under `build/`.

## Reproducible integration

```
git submodule update --init external/pc
python3 -m pip install capstone==5.0.9
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
their authoritative pins in `external/pc/requirements.txt`; only the three host
build tools actually used here are installed for Vita cross-build CI.

## Tested contracts and limits

Ten authored byte sequences cover guest stack arguments and ret, carry, a
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

Replace pointer-only scalar memory lowering with explicit load/store helpers
that can split valid RAM-wrap accesses while preserving operand evaluation,
fault ordering, endian/FP bit patterns and the original PC oracle results.
Include generated unaligned loads/stores ending at each RAM-view boundary;
retain faults for final-view, MMIO and 32-bit-rollover crossings. Then expand
generated flag/FP edge fixtures and real TLS/thread entry contracts.
User-supplied US XBE verification must precede any retail generation:
SHA-256 `3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc`,
title ID `45410066`. Do not add executable bytes to synthetic fixtures.
