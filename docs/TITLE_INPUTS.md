# Local title intake — issue #4

`python3 tools/check_title_inputs.py` validates locally supplied inputs without
preparing the runtime, generating translated code, launching a title, changing
save data, or writing into the input/build directories. It uses the pinned PC
project's bounded `tools/compiler/xbe_image.py` parser on the exact hashed byte
snapshot, not a second XBE implementation. PC source identity/hash checks run
first; the runtime pin in the report is an expected provenance pin, not evidence
that a runtime was executed. Source/toolchain pins are not updated.

## Expected ignored installation layout

- `private/game/default.xbe`: user-owned supported US Xbox executable, title ID
  `45410066`, SHA-256
  `3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc`.
- `private/title-analysis/disasm/functions.json`
- `private/title-analysis/disasm/labels.json`
- `private/title-analysis/disasm/summary.json`
- `private/title-analysis/func_id/identified_functions.json`
- `private/title-analysis/abi/abi_functions.json`
- `private/title-analysis/provenance.json`

These five analysis filenames are the actual inputs consumed by pinned
`tools/recomp/__main__.py::find_data_files` and `BatchTranslator`. The directory
layout is an explicit local intake convention: the original generator requires
`--disasm-dir`, `--func-id-dir` and `--abi-dir` overrides to select them. It must
not silently use another game's in-tree output. Existing upstream validation
checks only the summary's binary basename, which cannot distinguish two builds
both named `default.xbe`.

After producing analysis from this exact executable with the pinned pipeline,
retain a local provenance document with this structure (placeholders do not pass):

```json
{
  "version": 1,
  "xbe_sha256": "SUPPORTED_EXECUTABLE_SHA256",
  "pc_revision": "PIN_FROM_sources.lock.json",
  "runtime_revision": "PIN_FROM_sources.lock.json",
  "files": {
    "disasm/functions.json": "SHA256_OF_EXACT_FILE_BYTES",
    "disasm/labels.json": "SHA256_OF_EXACT_FILE_BYTES",
    "disasm/summary.json": "SHA256_OF_EXACT_FILE_BYTES",
    "func_id/identified_functions.json": "SHA256_OF_EXACT_FILE_BYTES",
    "abi/abi_functions.json": "SHA256_OF_EXACT_FILE_BYTES"
  }
}
```

This is a **local hash-bound attestation**, not a cryptographic proof that the
analysis was derived correctly. Do not fabricate it for unrelated/stale analysis.
The approved XBE hash authenticates the supported build; structural certificate
checks also require the documented title ID, a US region bit and retail entry
keys. No unsupported executable/hash override exists in the production CLI.

## Support and failure contracts

The bounded profile requires nonempty function records with hexadecimal `start`
and `end`, distinct starts and complete extents in file-backed executable
sections. The authenticated entry must have both a function record and ABI
record. Labels may reference declared virtual image sections, including BSS; identification/ABI
records must reference known functions. Unknown conventions, duplicates,
non-finite JSON values, malformed addresses and foreign summary basenames fail
closed. Each file is parsed from the same byte snapshot whose hash is verified.
Metadata is limited to 32 MiB per file / 128 MiB total and XBE input to the pinned
reader's 256 MiB limit. These are host-tool limits, not Vita memory budgets.

Inputs must resolve inside this checkout's ignored `private/` or `game-data/`;
metadata cannot escape its analysis root via symlinks. Intended generation output
must resolve to a proper subdirectory of ignored `build/`; no directory is created.
Stable local input/source files are required during the preflight. The tool never
executes metadata or downloads game data. Reports omit executable bytes, label or
function names, observed metadata hashes and actual private filesystem paths.

Exit codes:

- `0`: `validated_inputs`, generation/boot still `not_run` and trace unavailable.
- `2`: `missing_inputs`, with all missing logical slots and expected setup paths.
- `3`: `rejected`, with controlled diagnostic codes, never private exception text.

Any supplied wrong-hash XBE is rejected even when analysis files are missing.
The import coverage report compares declared ordinals with current Vita dispatch
entries only. It does **not** establish support for argument modes, title boot,
function correctness or a working renderer. Unsupported ordinals are reported;
they are never converted into fake-success imports. Generation, dispatch-table
integration, safe retail image ownership and first real fault trace remain
separate gates.

## Validation

`python3 -m unittest discover -s tests -p test_title_inputs.py` uses authored XBE
headers and a single authored RET, with a test-only hash mock. It exercises the
real pinned parser without proprietary data. Tests cover validated snapshot /
no input writes, missing manifests, wrong binary hashes, stale metadata and
source pins, provenance file sets, executable extents and entry/ABI coverage,
unknown imports, symlink/path rejection, duplicate JSON keys, file bounds and
certificate region/title/debug rejection. The normal CLI on this environment
reports missing XBE plus all six provenance/analysis slots, exit `2`; no retail
execution or trace is claimed.
