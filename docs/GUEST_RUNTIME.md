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
- `guest/memory.{h,c}` provides a single checked, immutable-after-bind region.
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

Nine authored byte sequences cover guest stack arguments and ret, carry, a
callee saving EBX, a direct call with the exact guest return VA, an indirect
lookup, unaligned integer memory, SSE load/add/store, x87 rounding and integer
division. Assertions independently check expected results; pristine PC and
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
LOCK operations, real guest threading, all indirect targets, MMIO, full memory
mirrors, GPU memory coherence or game simulation. Upstream missing-target
behavior is counted and tested; this does not mean a retail target is resolved.
The mock divide handler longjmps for assertions, not guest exception delivery.
The memory adapter is a correctness prototype with a check on every access;
no overhead or speed claim is made.

## Next implementation

Extend the memory interface with an explicit 64 MiB Xbox RAM backing and tested
alias translation using ordinary user memory, without kubridge/fixed mappings.
Classify MMIO, synthetic kernel VAs, invalid pointers and accesses crossing the
RAM wrap separately. Add generated load/store fixtures at each boundary and
compare against PC memory semantics before using it for title initialization.
Then expand generated flag/FP edge fixtures and real TLS/thread entry contracts.
User-supplied US XBE verification must precede any retail generation:
SHA-256 `3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc`,
title ID `45410066`. Do not add executable bytes to synthetic fixtures.
