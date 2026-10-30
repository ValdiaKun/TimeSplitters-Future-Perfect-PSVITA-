These Cg sources are authored asset-free GPL-3.0-only project inputs, covered by
LICENSES/GPL-3.0.txt. They are not extracted game shaders or firmware code.

programs.lock.json binds source bytes, strict compile flags, expected GXP bytes
and compiled interfaces. The experimental source compiler currently supports
position/UV and sample2D(+uniform tint). Color-attribute output is an explicit
negative control, not implemented by a fallback. Float2 UV source is reflected
as a four-component padded attribute; native streams must honor that compiled ABI.

Generated GXP/headers belong only in ignored build/offline-shaders. Structural
checks and deterministic hashes do not establish real GXM execution or pixels.
