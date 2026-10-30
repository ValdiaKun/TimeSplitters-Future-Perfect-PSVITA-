# Captured guest faults and offline PC mapping — issue #6, partial

The guest runtime already records translated instruction markers and captures
TsfpFault at its C-only execution boundary. `guest/fault_report.{h,c}` exports
an existing snapshot after recovery returns. It does not introduce an exception
handler, recovery scope, stack unwinder, guest memory read or network endpoint.
The boundary, durable cleanup guards and worker ownership remain unchanged.

The caller must own a stable snapshot/output buffer. With valid non-null storage,
`tsfp_fault_json` returns the JSON byte count excluding the NUL. The fixed640-byte
local scratch bounds formatting; the header constant also provides sufficient
output capacity. Zero means invalid event or insufficient storage, and leaves
the output unchanged. Caller errors such as an invalid native pointer cannot be
made safe by this interface. No formatting is performed in tsfp_guest_raise or
from guest memory fault callbacks. Emit/store diagnostics only from a quiescent
host owner after the outermost recovery boundary returns (or a worker joins),
using the application's private log transport. No automatic retail log upload.

Schema version1 contains numeric kind/access, captured PC, fault/target address,
access width, detail, thread identity, eight integer registers in EAX/ECX/EDX/EBX/
ESP/EBP/ESI/EDI order, EFLAGS and validity mask. It deliberately excludes instruction
bytes, FP/SIMD payloads, native pointers, filesystem paths and asset buffers.
It is not a full CPU save-state. Only bits marked valid in flags_valid may be
interpreted as known EFLAGS. THREAD_EXIT is a captured event, not a crash.

`pc` is the runtime's captured instruction marker, distinct from the target or
data `address`. Host-originated helpers can retain the last guest marker; neither
formatting nor mapping proves that a title reached that instruction. Do not
infer a reliable guest call stack from raw stack values. Complete guest call-stack
instrumentation and Xbox SEH delivery remain unimplemented.

## Offline mapping

```sh
python3 tools/symbolize_guest_fault.py \
  --fault private/traces/first-event.json \
  --functions private/title-analysis/disasm/functions.json
```

Inputs are read-only bounded local JSON snapshots, never executable code. The
mapper validates numeric schema fields, register count, hex function extents,
overlap and uniqueness before using end-exclusive containment. It reports a
function's start, offset and original metadata index, not the nearest preceding
function. Gaps and end addresses outside a declared range remain unmapped.
Names and game bytes are not emitted. Controlled errors do not echo private
paths or malformed input contents. The CLI reports exact-byte input hashes so
local captures can be paired reproducibly; hashes alone do not authenticate maps.

**Assurance is unverified_local_map.** A plausible result is not evidence that
analysis matches the running build. Authenticate original inputs with
check_title_inputs.py, pair analysis/capture with exact build provenance, then
review the mapped source locally. No supported original inputs exist here;
this tool never substitutes for the authenticated title-intake gate. Keep real
fault records/analysis/private hashes local and review before sharing diagnostics.
Exit0 means mapped,2 unmapped and3 rejected, not successful game execution.

## Evidence and remaining gates

The existing real translated divide/memory/dispatch fixtures and independent
native workers exercise formatting. The CPU runner extracts exactly one authored
divide-fault JSON record per PC/portable/sanitizer/ARM transcript and checks its
PC-to-function mapping. Removing the record or altering the expected PC fails
the gate. Authored Python fixtures cover malformed maps, schema/range boundaries
and controlled CLI failures. No new retail instruction bytes are committed.

Remaining: authentic title dispatch/image integration and the first actual entry
trace, runtime elapsed-time hooks, trustworthy guest call stacks, symbolized
physical-device captures, GPU/audio/device services and hardware measurements.
None of these are established by successful synthetic export/mapping or VPK builds.
