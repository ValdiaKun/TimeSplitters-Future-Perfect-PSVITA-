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

Next: inspect the pinned guest OBJECT_ATTRIBUTES layout and accept validated
empty unnamed event descriptors. Do not infer guest widths from native host
structs. Names/root directories/unsupported flags must fail explicitly. A
supported user-owned XBE plus matching analysis metadata is still missing for
a real entry trace. No physical Vita or performance measurements are available.
