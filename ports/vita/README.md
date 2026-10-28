# Vita diagnostic bootstrap

**Current status:** an isolated homebrew diagnostic, not a playable TimeSplitters port. It does not load the Xbox executable, run the recompiled guest C, call Xbox kernel services, load game data, or implement the game renderer. The native game port is tracked in [the porting plan](../../docs/PORTING_PLAN.md).

This source was developed in the [TimeSplitters PC experimental fork's Vita bootstrap branch](https://github.com/ValdiaKun/timesplitters-future-perfect-pc/tree/vita-port-bootstrap/ports/vita) and moved to the dedicated Vita work repository without linking its Windows/macOS engine.

## Requirements

- VitaSDK from https://vitasdk.org/, with `vita2d` and its dependencies installed
- CMake 3.16+, Ninja, and a Linux/WSL host
- A homebrew-enabled PS Vita/PS TV with VitaShell for hardware installation

No original Xbox files are required to run this diagnostic.

## Build

```sh
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S ports/vita -B build/vita-bootstrap -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build/vita-bootstrap
```

The expected output is `build/vita-bootstrap/tsfp-vita-bootstrap.vpk`. That artifact was cross-built with the locked VitaSDK snapshot; physical Vita execution is **unverified**. See [validation](../../docs/VALIDATION.md).

## Host-only test

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  ports/vita/src/input_map.c ports/vita/tests/test_input_map.c \
  -lm -o /tmp/tsfp-vita-input-test
/tmp/tsfp-vita-input-test
```

Mapping: left stick movement, right stick look, R fire, L aim, Cross use, Square reload, Circle crouch, Triangle and D-pad right next weapon, D-pad left previous weapon, Start pause. Select+Start exits. These assignments are **provisional**, not wired into game input. 16-count analog dead zones remove small hardware offsets.

The display shows movement axes, camera axes and actions. It uses `vita2d` for developer diagnostics; this is **not a choice of final renderer**. Native D3D8 semantics may require a dedicated vitaGL or libgxm backend, after research of [Halo CE Vita](https://github.com/BirchWoodGod/halo-ce-vita), [THUG Vita](https://github.com/iwannagooutside/thug-vita-port), and [MH3 Vita](https://github.com/Grarak/mh3-wii).

Do not add proprietary game images, assets, saves, generated engine binaries, or copyrighted third-party source to this repository. The [PC project's development rules](https://github.com/ValdiaKun/timesplitters-future-perfect-pc/blob/main/AGENTS.md) apply to changes made **in that PC repository**, and its tracked build/generator source remains authoritative there.

## Locked Linux toolchain and optional guest tests

Python 3.12+ and curl are required for the installer. Downloads are hash checked;
an existing unrecognized SDK directory is preserved and rejected.

```sh
git submodule update --init external/pc
python3 -m pip install capstone==5.0.9 cmake==4.4.4 ninja==1.13.2
python3 tools/fetch_vitasdk.py
python3 tools/generate_fixtures.py
export VITASDK="$PWD/.cache/toolchains/vitasdk"
export PATH="$VITASDK/bin:$PATH"
cmake -S ports/vita -B build/vita -G Ninja -DTSFP_BUILD_GUEST_TESTS=ON
cmake --build build/vita
```

This also creates `tsfp-guest-test.vpk`, a separate synthetic CPU fixture
application, not retail game execution. Its assertions execute even in Release.
Host and ARM Linux execution instructions are in [GUEST_RUNTIME.md](../../docs/GUEST_RUNTIME.md).
The diagnostic emits bounded transition/error events to stderr with process
timestamps; on-device capture/visibility is unverified. No log or save directory
is created or overwritten. Package URLs are recorded with exact hashes; an
upstream replacement fails checksum verification rather than silently upgrading.
