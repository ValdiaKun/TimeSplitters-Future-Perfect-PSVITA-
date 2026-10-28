# TimeSplitters: Future Perfect — PS Vita Port

Experimental, independent **PS Vita homebrew porting project** for *TimeSplitters: Future Perfect* (original Xbox).

> **Status: early technical research. Not playable.** No Vita build has been verified on hardware. No game code or proprietary game assets are included in this repository yet.

## Project approach

The existing [TimeSplitters PC project](https://github.com/ValdiaKun/timesplitters-future-perfect-pc) provides the x86-to-C recompilation, Xbox guest runtime and common gameplay/rendering policies. This **separate repository** owns Vita-specific development, validation, and documentation. Integration with the PC codebase is planned; the PC project remains the authoritative source for shared behavior until actual integration is established.

Research references:
- [Halo CE Vita](https://github.com/BirchWoodGod/halo-ce-vita) — Xbox rendering and native ARM integration; its code is GPL-3.0-licensed.
- [THUG Vita](https://github.com/iwannagooutside/thug-vita-port) — Xbox game source adaptation, vitaGL, controls and performance research; verify individual file licenses before reuse.
- [Monster Hunter Tri Vita](https://github.com/Grarak/mh3-wii) — native Vita runtime, libgxm, frame-packet and optimization techniques; Wii PowerPC/GX translation is **not** a direct replacement for Xbox x86/D3D8.
- [MH3 optimization notes](https://github.com/Grarak/mh3-wii/blob/main/docs/optimizations/README.md).

## Development rules

1. Keep console-specific changes here and shared game logic in its upstream PC project; track the exact upstream commit when a bridge is implemented.
2. Use focused branches and draft pull requests. Do not merge a milestone without explicit review and verification.
3. Do not commit or distribute retail Xbox binaries, game-disc data, saves, copyrighted artwork, proprietary SDKs, or assets.
4. Test incremental goals independently: host-only tests, Vita toolchain build, hardware boot, guest execution, graphics, menus, then gameplay.
5. Clearly distinguish an experimental diagnostic VPK from an actual working game port.
6. Preserve attribution and original licenses; do not copy code from reference repositories without auditing its license and compatibility.

See `docs/PORTING_PLAN.md` on the bootstrap development branch for the initial architecture.
