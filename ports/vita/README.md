# PS Vita port — first milestone (diagnostic bootstrap only)

**Status: NOT a playable TimeSplitters port.** This isolated target is a VitaSDK
homebrew shell that draws a 960×544 controller diagnostic screen, polls the two
analog sticks and face/shoulder buttons, and exercises a host-testable input
translation layer. It does **not** include, run, or load the Xbox executable,
its generated C, the Xbox kernel implementation, audio, assets, or D3D8 renderer.
It must never be advertised or distributed as a game build.

This `ports/vita/` experiment does not change the pinned PC/Mac runtime,
patch stack, or generated guest code. `main` is unaffected until the PR is
reviewed and explicitly merged. No copyrighted game data is distributed.

## Build the standalone Vita diagnostic VPK

Install [VitaSDK](https://vitasdk.org/) and its `vita2d` library, and ensure
`$VITASDK/bin` is on `PATH`. On Linux/WSL, from the repository root:

```sh
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S ports/vita -B build/vita-bootstrap -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build/vita-bootstrap
```

Expected unverified build output: `build/vita-bootstrap/tsfp-vita-bootstrap.vpk`.
This directory is ignored by the existing repository build rules (check your
working tree before building). Install on a homebrew-enabled Vita via VitaShell.
Press **SELECT+START** to close the diagnostic. No game ISO is needed for this test.

The compiler and VitaSDK are not available in this development session; Vita
cross-compilation, installation, actual screen presentation and input capture
are **not verified**. A successful host test below proves only the pure mapping
logic.

## Test without VitaSDK

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  ports/vita/src/input_map.c ports/vita/tests/test_input_map.c \
  -lm -o /tmp/tsfp-vita-input-test
/tmp/tsfp-vita-input-test
```

Inputs: left stick = move, right stick = look; R = fire, L = aim,
Cross = use, Square = reload, Circle = crouch, Triangle / D-pad right =
next weapon, D-pad left = previous weapon, Start = pause. A 16-count dead
zone removes minor stick drift. The original game has no jump action.
These action flags are **provisional**, not yet connected to Xbox game input.

## Integration order and acceptance gates

1. **Vita toolchain and physical input (current milestone):** Build the VPK,
   verify the visual diagnostic, analog ranges, button edges and clean exit on
   an actual Vita. Check memory use and crash behavior.
2. **ARMv7 guest execution:** Add a separate 32-bit ARM build of the existing
   x86-to-C recompilation and Xbox runtime. Audit assumptions about x86/host
   ABI, x87/SSE, 32-bit pointers, guest virtual memory, atomics, TLS, and
   executable memory. Start with deterministic, asset-free CPU tests.
3. **Xbox system services:** Port XBE loading/guest memory, Xbox kernel HLE,
   file I/O, controller bridge, synchronization, clock, saves, audio and movies.
   Require title boot and menu simulation tests before claiming gameplay.
4. **Actual graphics backend:** Translate the project's D3D8/RHI draw contract
   to native `libgxm` frame packets with offline-compiled shader variants.
   `vita2d` is only a diagnostic convenience, **not** the production renderer.
   Implement texture conversion/caching, depth and blend state, render target
   feedback, animations and compressed texture formats.
5. **Optimize and prove behavior:** Profile CPU and GPU separately on hardware.
   Adopt the techniques in the MH3 reference where applicable: buffered draws,
   asset conversion at load, compressed textures, native hot paths, worker
   threads, frame pacing, visibility culling and finally PGO/ThinLTO. MH3's
   Wii GX/PowerPC-specific replacement code is NOT directly compatible with
   this game's Xbox D3D8/x86 runtime.
6. **Acceptance:** Game-owned original Xbox files are locally supplied and
   verified; title -> menu -> first controllable level -> save/load -> levels
   are successively tested with audio, stable rendering and measured frame
   times. A VPK that launches is not proof the game runs.

References:
- TimeSplitters PC/Mac architecture: `docs/development/shared-platform.md`
- Pinned runtime/build rules: `AGENTS.md` and `docs/development/building.md`
- MH3 optimization guide: https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/README.md
- MH3 Vita adapter: https://github.com/Grarak/mh3-wii/tree/main/runtime/vita
- VitaSDK samples: https://github.com/vitasdk/samples
