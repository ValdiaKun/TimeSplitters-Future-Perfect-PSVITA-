# Title-entry dependency map — 2026-10-29

PC pin `759f7a0ecebc2341cdcedf638fcea68c4a7fbf4e`, prepared runtime
`d90c92988a9f99d0580d2ca3f7efd6ec3a7b7845` plus its authoritative patch stack.
Paths below are in that prepared runtime. This is a source dependency map, not
an observed retail initialization trace: no supported XBE/analysis data is present.

| Dependency | Authoritative entry point / Vita work |
| --- | --- |
| Verify executable and generate | `tools/recomp/`, PC `tools/compiler/`; existing `tools/guest_codegen.py` verifies US SHA-256 before retail generation. Matching analysis metadata still required. |
| Image sections, initial memory, imports | `src/kernel/xbox_memory_layout.c`, `xbox_memory_layout.h`; adapt section loading and checked import patching, retaining the existing Vita RAM policy. No XBE loader integrated here. |
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
