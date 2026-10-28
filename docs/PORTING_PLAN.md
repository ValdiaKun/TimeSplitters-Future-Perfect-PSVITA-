# PS Vita Porting Plan and Technical References

**Project:** `ValdiaKun/TimeSplitters-Future-Perfect-PSVITA-`  
**Source upstream:** [TimeSplitters PC](https://github.com/ValdiaKun/timesplitters-future-perfect-pc)  
**State:** native diagnostic and synthetic guest test VPKs cross-build; ten PC-translated synthetic x86 functions execute on ARMv7 under QEMU. **No retail game runtime, renderer or gameplay. Hardware unverified.** See [STATUS.md](STATUS.md) and [VALIDATION.md](VALIDATION.md).

## Reference projects and applicability

| Reference | Verified architecture / practice | Adaptation to TimeSplitters | IP caveat |
| --- | --- | --- | --- |
| [Halo CE Vita](https://github.com/BirchWoodGod/halo-ce-vita) | Native ARM Xbox decomp, Direct3D-style graphics replacement, Xbox data files loaded at runtime, Vita control mappings, performance tuning | Closest Xbox-era FPS reference: investigate runtime memory, texture format conversion, geometry semantics, frame pacing, HUD and lifecycle | GPL-3.0: do not paste its source into this repo without a conscious license decision and preservation of notices |
| [THUG Vita](https://github.com/iwannagooutside/thug-vita-port) | Builds a native ARM Vita port from Xbox-oriented engine source; vitaGL backend, targeted C++ compiler compatibility fixes and per-file optimization; original game data separately supplied | Good for incremental boot path, Vita I/O, controller translation, crashes/logs, profiling and asset staging | No root LICENSE file identified; inspect authorship and specific file permissions before copying any code |
| [MH3 Wii/Vita](https://github.com/Grarak/mh3-wii) | Static recompilation, frame-packet rendering, native libgxm, compressed GPU textures, render-thread scheduling, offline shader banks, PGO/ThinLTO and profiling | Good performance/porting playbook when our own Xbox renderer and ARM guest code exist | Wii PowerPC/GX code is not compatible with Xbox x86/D3D8; check its GPL license before any direct reuse |
| [TimeSplitters PC](https://github.com/ValdiaKun/timesplitters-future-perfect-pc) | Original Xbox x86-to-C pipeline, HLE guest services, shared gameplay/input policies, native PC render backends | The required game-specific upstream; design explicit host interfaces and pin commits for future integration | Preserve existing upstream attribution and patches; no retail game code/assets here |

See [MH3's optimization index](https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/README.md).

**Renderer decision is not made.** THUG demonstrates vitaGL; the inspected Halo revision uses direct GXM; MH3 demonstrates direct libgxm with offline shaders. We should benchmark an asset-free subset of TimeSplitters D3D8 operations, including render-target feedback and depth behavior, before committing to vitaGL versus direct libgxm. The earlier bootstrap's vita2d screen is intentionally not the final renderer.

## Work boundaries

- This new GitHub repository is the **port's work repository**. Its `main` is separate from the Windows/Mac PC project; use task branches and PRs.
- Prefer integrating the existing PC source as a **pinned dependency or reproducible source import**, not a copied diverging engine snapshot. Record upstream commit and license notices.
- Do not modify or ship `vendor/ts3FPrecomp` or retail Xbox files in this repository until source, license and build integration have been audited.
- Keep game-owned executable, extracted files, textures, video/audio, caches, local logs and saves outside of Git and outside of released VPKs.
- No external agent has unlimited or perpetual rights: repository changes occur through the connected account's authorized actions. **Merges and releases require explicit review**.

## Milestone acceptance

### M0 — standalone VitaSDK bootstrap (ongoing)
- [x] Isolated VitaSDK/vita2d diagnostic source, two analog sticks, provisional action flags.
- [x] Deterministic host tests and CI workflow authored.
- [x] Original input CI execution verified (run `40715066565`); new CI results tracked in PR #1.
- [x] VitaSDK SELF/VPK cross-build verified for diagnostic and synthetic guest harness.
- [ ] Physical Vita/PS TV boot, controls, readable UI and clean exit verified.

### M1 — ARMv7 compatibility harness
- [x] Identify and pin PC upstream commit and generated-code/build requirements (`sources.lock.json`, `external/pc`).
- [ ] Build *asset-free* CPU fixtures on ARMv7; check size/alignment, little-endian layout, x87/SSE lowering, flags, exceptions, atomics and guest pointer validity.
- [x] Prototype 64 MiB RAM views and separate contiguous/tiled backings without booting game; stack/TLS/atomics have synthetic coverage. Generated scalar RAM-wrap lowering and real guest thread entry remain open.
- [ ] Execute tests on an actual Vita or ARMv7 equivalent; don't treat host x86 test results as ARM validation.

### M2 — system services and title boot
- [ ] Loader/runtime bridge, file access, timing, Xbox kernel HLE, input delivery and save isolation.
- [ ] Game boot reaches a reliable title loop using user-supplied verified USA Xbox disc files.
- [ ] Produce bounded trace and crash reports; distinguish UI boot from playable controls.

### M3 — GPU/graphics
- [ ] Audit PC D3D8/RHI contracts, native texture/surface/vertex state and shader requirements.
- [ ] Pick vitaGL or libgxm based on measured constraints and correctness.
- [ ] Render basic triangle, textures, alpha/depth/blend, Xbox texture conversion, render target feedback, HUD then 3D model scenes.
- [ ] Batch draw packets and precompile required shader variants where possible.

### M4 — gameplay, correctness and performance
- [ ] Menu, first level movement and fire, load/save, audio/music, cinematics.
- [ ] Test later missions and multiplayer separately; publish only verified functions.
- [ ] Profile actual GPU/CPU times and memory; add worker threads, native hot functions, culling, streaming, compressed textures and LTO only after establishing baseline.

## Practical local build

For the diagnostic only (requires separately installed VitaSDK and vita2d):

```sh
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S ports/vita -B build/vita-bootstrap -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build/vita-bootstrap
```

Host input test (requires C compiler only):

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  ports/vita/src/input_map.c ports/vita/tests/test_input_map.c \
  -lm -o /tmp/tsfp-vita-input-test
/tmp/tsfp-vita-input-test
```

This bootstrap **does not run the game**. The final port cannot be declared runnable until native ARM guest execution and Xbox graphics/system services have been integrated and tested on hardware.

## Current foundation gates (A/B)

The older M0–M4 labels above remain roadmap groups. Milestone A's cross-build
and deterministic input gates pass; hardware platform validation remains open.
Milestone B has a passing ARMv7 synthetic subset, not full runtime acceptance.
RAM views and MMIO classification now have host/ARM coverage. Generated scalar
RAM-wrap accesses, device behavior, complete flags/FP behavior, exception
delivery, title thread entry and retail dispatch coverage remain open. The
renderer (C) requires device comparison of libgxm/vitaGL triangle, textures,
blend/depth and render-target tests before selecting a production backend.
Boot/services (D) and optimization (E) require verified user files and hardware.

Reviewed reference revisions and licensing constraints are in
[REFERENCE_AUDIT.md](REFERENCE_AUDIT.md). No baseline depends on kubridge,
CapUnlocker, clock changes or an on-device shader compiler.
