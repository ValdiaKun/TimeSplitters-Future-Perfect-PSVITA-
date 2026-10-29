# Title-entry dependency map — 2026-10-29

PC pin `759f7a0ecebc2341cdcedf638fcea68c4a7fbf4e`, prepared runtime
`d90c92988a9f99d0580d2ca3f7efd6ec3a7b7845` plus its authoritative patch stack.
Paths below are in that prepared runtime. This is a source dependency map, not
an observed retail initialization trace: no supported XBE/analysis data is present.

| Dependency | Authoritative entry point / Vita work |
| --- | --- |
| Verify executable and generate | `tools/recomp/`, PC `tools/compiler/`; existing `tools/guest_codegen.py` verifies US SHA-256 before retail generation. Matching analysis metadata still required. |
| Image sections, initial memory, imports | `src/kernel/xbox_memory_layout.c`, `xbox_memory_layout.h`; adapt section loading and checked import patching, retaining the existing Vita RAM policy. Bounded structural normalization and synthetic initialization exist in guest/xbe.c and guest/image.c; retail integration remains unverified. |
| Register initialization, title entry | `templates/new-game/src/main.c`, generated dispatch; connect validated entry address only after imports and title state are initialized. |
| Threads, stack arguments, TLS | `src/kernel/kernel_bridge.c` `bridge_run_thread_inline`, `bridge_thread_main`, `bridge_PsCreateSystemThreadEx`; `xbox_memory_layout.c` `xbox_AllocThreadTib`. `guest/execution.c` now initializes caller-owned CPU/stack/FS state and a minimal TIB/TLS block. Xbox thread objects, template extraction, handles and native scheduling remain open. |
| Faults and exception delivery | Header divide hook and checked memory/dispatch callbacks; new C-only capture boundary does not walk fs:[0] or deliver SEH. Need validated Xbox exception frame layout and safe dispatch before recovery into title code. |
| Allocation and object lifetime | `xbox_memory_layout.c`, `kernel_bridge.c`; adapt bounded guest heap/stack/object allocators, then import services. The test's 160 MiB heap is not a production budget. |
| File access and saves | `kernel_bridge.c` file/import bridges; replace Win32 dependencies behind Vita storage interfaces. Require user data root and isolated save root. No file-service success stubs. |
| Timers and synchronization | `src/kernel/kernel_thread.c`, `kernel_bridge.c`; adapt public native waits/clock facilities preserving guest clock semantics and resource-safe fault boundaries. |
| Input | `src/input/recomp_pad.c`, `src/hle/hle_input.c`; connect native input mapping to the title-facing HLE state after import integration. |
| Graphics/audio/devices | `src/hle/`, `src/kernel/`; identify actual import/workload subset from a verified title trace. MMIO continues to fault. GPU backend remains unselected pending device correctness/measurement. |
| Indirect dispatch | Generated lookup plus manual/kernel tables; unresolved targets now produce captured faults inside the execution boundary. Full title dispatch tables are absent. |

Shortest next dependency: guest thread-object/TIB initialization and an explicit
kernel import dispatch boundary using the pinned contracts, with deterministic
fixtures and fail-closed unsupported ordinals. Do not expand GPU infrastructure
as a substitute for establishing that entry path. Real title validation requires
the supported user-provided executable, matching metadata, game data and Vita.

Current correctness gate: Intel REP comparison-fault EFLAGS restoration must pass
push and PR validation before extending this boot path. The native host oracle
has a separate vendor contract; see VALIDATION.md for the captured regression.

Implemented subset after the CI gate: template-based thread object at fs:[0x28],
canonical function-slot imports (16-slot bound), current-thread query, zeroing and
termination. A real translated synthetic entry exercises all three services and
TLS memory. This does not complete thread objects/handles/waits or a title loader.
Next concrete gate is bounded image section/permission/import/entry initialization
with synthetic input; the retail XBE and metadata are still unavailable.

Bounded synthetic image gate now implemented: normalized sections, physical alias
conflict checks, staged file/BSS state, initial thread/TLS/object/import setup,
software permissions and compiled entry resolution. One-shot entry and teardown
are tested through real translated code. Next dependency is bounded normalization
of authored XBE headers/section records into this descriptor. Retail hash checking
and matching generated analysis/dispatch data remain mandatory before real use.

Raw-header normalization subset is now implemented in guest/xbe.c and compared
with the pinned PC parser. Authored retail/debug headers both reach synthetic
entry through image initialization. The next concrete dependency is TLS-directory
extraction (raw data, zero fill, index writes and callback policy) into initial
thread storage. Caller-provided synthetic thread configuration remains explicit;
no authenticated retail image or complete XBE/title boot is claimed.

The bounded TLS profile now extracts initialized bytes/zero fill, validates index
and callback metadata, and stages main-thread TLS. Default indexing is title-owned
as in the PC runtime; an explicit loader-write policy is tested. Authored x86
entry and two workers execute negative-index fs:[4] accesses and private object
TlsData links. Next dependency: ownership-safe guest thread handles/lifecycle,
then validated wait/thread imports. Non-empty callbacks, full TLS characteristics,
retail metadata and physical hardware remain separate gates.
