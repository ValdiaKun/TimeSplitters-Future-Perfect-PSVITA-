# Runtime handoff — 2026-10-29

Not playable; no retail boot, GPU/audio/saves, hardware execution or FPS. Verify
branch/HEAD/PR #1 before continuing. Source/toolchain pins remain unchanged.

The next entry points are guest/storage.c and guest/thread_service.c. A host
controller initializes the bounded XBE/image, cedes known-unused writable BSS to
storage_configure, starts the service with an explicit default guest stack, then
enters guest code. Do not infer unused retail BSS from zero initialization.
Register/control the initial root owner as the image tests do. Native creation
and join are performed by the service owner, not on a translated callback stack.
After guest callers quiesce, service_stop refuses live/referenced workers and
closes/joins completed workers; retry a failed native join before pool disable.
Then storage_disable and image_reset are permitted. Never forcibly kill native
workers or change bindings/permissions/templates while pinned.

Supported create profile: ordinal 255/40 bytes, no extra/suspended/debug modes;
TLS size zero or matching profile, stack zero (host default) or aligned/in-bounds,
trusted compiled start entry, two opaque contexts, checked physically disjoint
outputs outside managed arena/templates. Other modes fail explicitly.
The C-scope guard has one durable TLS/static-context slot per native thread and
is only bookkeeping cleanup; it is not Xbox SEH or a cancellation unwinder.

Reproduce with tools/test_guest.py using the exact ARM compiler/sysroot/QEMU
command in VALIDATION.md, then the hash-locked VitaSDK Release build. The new
thread_service_suite.c drives actual translated creation/wait/close; storage,
wait and worker suites retain all fault/concurrency/rollback regressions.
Final CI evidence and latest source revision are in draft PR #1.

Events now use typed cells in the same 32-record registry. tests/event_suite.c
drives translated 189/225/186/233/187, including grants, deadlines, reference
retention and mixed event/thread quota rollback. The thirteenth authored XBE
section adds function 0x13b00; there are 126 translated functions.

Empty unnamed ObjectAttributes are now supported through explicit checked
12-byte guest decoding. Nonzero names, root directories and flags remain errors.
All 126 translated functions and existing regression suites remain enabled.

Next: inspect pinned Xbox allocation entry paths in xbox_memory_layout.c and
kernel_bridge.c and define a bounded, explicitly ceded guest-heap service profile
with alias/permission checks and transactional allocation/free tests. Do not
allocate from arbitrary retail BSS or add success stubs. Supported user-owned
XBE and matching analysis metadata are still missing for an actual entry trace.
Physical Vita execution, gameplay and performance remain unverified.

Allocator inspection: pinned bridge ordinals 184 (20-byte allocate) and 199
(12-byte free) use in/out guest pointers. PC bridge uses an immediately-backed
heap, no-op commit/decommit and a large-reserve clamp; these are implementation
limitations, not Xbox VM semantics to silently copy. Establish exact supported
reserve/commit/release behavior, zeroing, returned rounded extents and output
rollback before binding imports. A reserve must not obtain fresh RAM by aliasing
a different live range. Keep unsupported modes explicit.
