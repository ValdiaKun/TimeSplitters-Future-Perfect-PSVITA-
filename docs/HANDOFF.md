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

Bounded allocation now lives in guest/heap.c. Configure before entering guest
code with an explicitly ceded canonical low-RAM64KiB-aligned BSS span (<=1MiB);
never infer unused retail BSS. Configuration pins the image. Both storage pool
configuration orders reject physical overlap. Reserve/commit flags are distinct;
only RW pages, ZeroBits0 and the documented exact flag combinations are supported.
Wait for callers to quiesce, release every reservation, then disable before
image reset. Plain guest VA reuse cannot detect a stale pointer after reallocation.

The main CPU runner now executes127 translated functions and an independent
hash-pinned xboxrecomp VM differential gate. Missing sources fail closed unless
--fetch explicitly allows verified public source downloads. tools/test_graphics.py
validates the isolated Xita BC layout adapter; no GPU or device run is implied.
See ACCELERATION_AUDIT.md and NOTICE for exact revisions/licenses.

Event outputs now use tsfp_heap_pin_output/unpin_output plus a durable TLS
scope guard in kernel.c. Their64 bounded generation records retain the allocation
through result and EAX/ESP publication, releasing on normal/fault paths. Native
cancellation is deferred. Release/decommit returns UNSUCCESSFUL unchanged while
an event output is pinned; retry after the call. Guest code cannot forge pins.

Thread-create outputs now reuse CallGuard's two retained heap references across
probes/RPC/publication/EAX/ESP and fault cleanup. Distinct allocations and mirrors
are tested; server packets still contain no guest output pointers. Do not call
the storage role predicate as though it retained lifetime.

VM imports184/199 now reuse a two-reference KernelOutputGuard, pinning both
words before reads and through stores/EAX/ESP. Independent heap/image and mirror
locations are supported. Release/decommit of an output owner rejects unchanged;
relocate result words and retry. This is failure-atomic publication, not a
concurrent two-word atomic snapshot. Callers synchronize word and target access.

Next: issue #4, authenticated user XBE plus bound analysis intake and actual
entry tracing. Use a read-only preflight/missing-input manifest; never infer
retail BSS ownership or reuse another game's metadata. Original files/metadata
remain absent, physical Vita unavailable and no title/gameplay/FPS verified.
Managed-pool output pointers and broader VM/services remain unsupported.
