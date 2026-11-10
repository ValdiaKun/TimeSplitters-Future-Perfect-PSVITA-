# Experimental public-source offline shaders

The opt-in workflow compiles authored Cg into GXP without firmware modules or a
runtime compiler. This is an experimental structural profile, not a qualified
Xbox shader translator or proof that its USSE executes correctly on hardware.

```sh
python3 tools/build_offline_shaders.py --fetch
python3 tools/build_offline_shaders.py             # repeat using verified cache, no network
python3 tools/build_offline_shaders.py --verify-only
python3 -m unittest discover -s tests -p test_gxp_validate.py
python3 tools/test_offline_shaders.py
```

Host requirements: Linux amd64, Python 3.9+, C/C++17 toolchain, CMake, Ninja,
patch and dpkg-deb. CI pins CMake 4.4.4 and Ninja 1.13.2. Compiler sources and
five public Debian development packages are individually size/hash locked in
`tools/shader_compiler.lock.json` and `tools/shader_dependencies.lock.json`.
Packages are extracted locally without installing or running maintainer scripts.
The host compiler, standard library and Threads are platform dependencies;
this is not a fully hermetic host toolchain. No prebuilt GXP is fetched.

The MIT open-shacccg revision is
`26da8ebc44717147d002c60d4a80cd6604aace7f`. Only its allowlisted source files are
fetched. The compatibility patch adds a missing array include and exposes the
pinned SPIRV-Cross ParsedIR through a derived adapter. The project-owned strict
driver rejects every warning/error and sets optimization, fast math, fast
precision and fast integer modes to zero. It does not use upstream's CLI defaults.
See `LICENSES/open-shacccg-MIT.txt`; each dependency's copyright path is checked
in the extracted package. Host compiler binaries are not distributed.

Each positive source is compiled twice in separate processes. Exact golden bytes,
stage, reflected parameters and source hashes must match the program manifest.
The GXP parser checks bounded tables, offsets, names, types and disjoint regions;
it does not decode USSE or prove driver/GPU acceptance. Invalid syntax and an
unsupported COLOR varying are mandatory negative controls. Corruption, stage,
interface, header and stale-source controls use the actual validation path.
Generated programs/headers live only in ignored `build/offline-shaders`.
Publication replaces individual files and writes a final marker last; it is not
an atomic directory snapshot. Verification rejects inconsistent partial output.

## Supported authored profile

Position float4 and UV float2 source attributes, texture sampling at unit zero,
and a fragment float4 uniform tint. The compiler reflects UV as a padded
four-component attribute at resource index 4; position occupies resource 0.
The native fixture therefore uses a 32-byte vertex containing two float4 fields.
Vertex COLOR and multiple varyings are unsupported and rejected. Do not silently
substitute tint for per-vertex color in a future Xbox adapter.

Golden SHA-256 values (full interfaces also locked in the manifest):

| Program | Bytes | SHA-256 |
| --- | ---: | --- |
| stream_texture_v | 264 | d637d101ca4c35cfc0b0e0638ae0430489ada195f2daa9ab6f1999dba321377d |
| stream_texture_f | 256 | 51d23e0fd9b29171df9a71441aa9ac86d7fc65991017e77c922c08c9c6c5b28b |
| stream_tint_f | 320 | 68adc6812e55380da1b3ff314a2196bddd93397b39f13e4764b6cd632c85d2d3 |

## Native fixture

After the shader build and standard VitaSDK setup:

```sh
cmake -S ports/vita -B build/vita -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTSFP_BUILD_OFFLINE_SHADER_CONTRACT=ON -DTSFP_BUILD_GXM_STREAM=ON
cmake --build build/vita
```

`tsfp-gxm-stream.vpk` (TFPV00005) is an asset-free offscreen 64x64 test. It uses
real attribute streams, U16 indices, authored checker/white textures, point/clamp
sampling, depth rejection and source-alpha blending. It checks five GPU readback
samples after completion and logs actual/expected bytes. It does not present game
frames. No CPU-painted framebuffer or mocked GPU success is used. Bounded host
and USSE allocation, checked partial initialization and conservative teardown
preserve resources until process exit if native destruction is uncertain.

Physical execution is NOT verified. First run must establish GXP acceptance,
correct pixels, synchronization and cleanup on a legitimate Vita/PS TV. A GPU
hang at Finish needs external recovery; there is no claimed cancellation timeout.
This fixture is not a production D3D8 backend, title boot or FPS benchmark.
