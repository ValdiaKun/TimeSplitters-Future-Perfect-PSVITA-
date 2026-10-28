#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Lift only authored, asset-free x86 bytes; never read a retail executable."""
from pathlib import Path
import sys
from prepare_upstream import prepare, ROOT

# Each function uses the real upstream translator and guest ABI.
# Args at [esp+4]; ret consumes the guest return address.
FIXTURES = {
    0x10000: ('fixture_add', '8b44240483c001c3'),
    0x10100: ('fixture_carry', 'b8ffffffff83c00183d000c3'),
    0x10200: ('fixture_callee', '5389c383c0075bc3'),
    0x10300: ('fixture_call', 'b829000000e8f6feffffc3'),
    0x10400: ('fixture_indirect', 'b829000000b900020100ffd1c3'),
    0x10500: ('fixture_unaligned', 'a10180010083c001a301800100c3'),
    0x10600: ('fixture_sse', '0f10010f58c00f1102c3'),
    0x10700: ('fixture_x87', 'dd01d9fcdd1ac3'),
    0x10800: ('fixture_div', '8b44240431d2f7f1c3'),
    0x10900: ('fixture_memory', '8b0183c0018902c3'),
    0x10a00: ('fixture_u16', '0fb701668902c3'),
    0x10b00: ('fixture_s16', '0fbf01668902c3'),
    0x10c00: ('fixture_f32', 'f30f1001f30f1102c3'),
    0x10d00: ('fixture_f64', 'f20f1001f20f1102c3'),
    0x10e00: ('fixture_i64', 'df29df3ac3'),
    0x11000: ('fixture_rmw', '8301010f92c00f94c30f90c20f98c6c3'),
    0x11100: ('fixture_rmw16', '668301010f92c00f94c30f90c20f98c6c3'),
    0x11200: ('fixture_not', 'f711c3'),
    0x11300: ('fixture_xchg', '8701c3'),
    0x11400: ('fixture_indirect_mem', 'ff11c3'),
    0x11500: ('fixture_address', '8b4401088902c3'),
    0x11600: ('fixture_push', 'ff31c3'),
    0x11700: ('fixture_pop', '58c3'),
    0x11800: ('fixture_atomic', 'f00fc101c3'),
    0x11900: ('fixture_mmx', '0f6f010f7f02c3'),
    0x11a00: ('fixture_half', '0f12010f1302c3'),
    0x11b00: ('fixture_s32', 'db01db1ac3'),
    0x11c00: ('fixture_push16', '66ff31c3'),
    0x11d00: ('fixture_pop_mem', '8f02c3'),
    0x11e00: ('fixture_pop16_mem', '668f02c3'),
    0x11f00: ('fixture_pop_esp_mem', '8f0424c3'),
    0x12000: ('fixture_sub', '8329010f92c00f94c30f90c20f98c6c3'),
    0x12100: ('fixture_rmw8', '8001010f92c00f94c30f90c20f98c6c3'),
    0x12200: ('fixture_ss_register', 'f30f10c1c3'),
    0x12300: ('fixture_sd_register', 'f20f10c1c3'),

}

def main():
    runtime = prepare()
    from guest_codegen import configure
    FunctionTranslator, config, original_translate = configure(runtime)
    out = ROOT / 'build/guest'
    include = out / 'include'
    include.mkdir(parents=True, exist_ok=True)
    config._install([config.Section('.text', 0x10000, 0x4000, 0, 0x4000, True),
                     config.Section('.data', 0x18000, 0x8000, 0x4000, 0x8000, False)],
                    0x10000, 0, 'asset-free-vita-fixtures')
    image = bytearray(0xc000)
    db = {}
    for va, (name, hex_bytes) in FIXTURES.items():
        code = bytes.fromhex(hex_bytes)
        image[va - 0x10000:va - 0x10000 + len(code)] = code
        db[va] = dict(name=name, end=va + len(code), size=len(code), _addr=va)
    translator = FunctionTranslator(bytes(image), db, seh_prolog=[], seh_epilog=[])
    chunks = ['#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n#include "lowered_memory.h"\n']
    # Same global-register aliases supplied by upstream batch translation.
    chunks += [f'#define {reg} g_{reg}\n' for reg in
               ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'esp',
                *(f'xmm{i}' for i in range(8)))]
    chunks += [f'void {item[0]}(void);\n' for item in FIXTURES.values()]
    baseline_chunks = list(chunks)
    for va, info in db.items():
        body = translator.translate_function(va, info)
        if not body or translator.lifter.unimplemented:
            raise RuntimeError(f'Untranslated fixture: {info["name"]}: {translator.lifter.unimplemented}')
        chunks.append(body)
        baseline_chunks.append(original_translate(translator, va, info))
    (out / 'fixtures.c').write_text('\n'.join(chunks))
    import re
    kernel = (runtime / 'src/kernel/kernel.h').read_text()
    layout = (runtime / 'src/kernel/xbox_memory_layout.h').read_text()
    checks = {
        'RAM_MIRRORS': (layout, r'#define XBOX_NUM_MIRRORS\s+(28)\b'),
        'CONTIG_BASE': (kernel, r'#define XBOX_CONTIG_BASE\s+(0x80000000)u'),
        'CONTIG_MIB': (kernel, r'#define XBOX_CONTIG_SIZE\s+\((64)u \* 1024u \* 1024u\)'),
        'TILED_BASE': (layout, r'#define XBOX_TILED_BASE\s+(0xF0000000)u'),
    }
    contract = []
    for name, (source, pattern) in checks.items():
        match = re.search(pattern, source)
        if not match:
            raise RuntimeError(f'Pinned PC mapping contract changed: {name}')
        contract.append(f'#define PC_{name} {match[1]}u\n')
    (include / 'pc_memory_contract.h').write_text(''.join(contract))
    (out / 'pristine').mkdir(exist_ok=True)
    (out / 'pristine/fixtures.c').write_text('\n'.join(baseline_chunks))
    (out / 'pristine/pc_memory_contract.h').write_text(''.join(contract))
    original = (runtime / 'templates/runtime/recomp_types.h').read_text()
    (include / 'recomp_types.h').write_text(original)
    (include / 'recomp_icall_feedback.h').write_bytes((runtime / 'src/kernel/recomp_icall_feedback.h').read_bytes())
    # Local ARM adapter is a reproducible patch to a build-local template copy.
    import subprocess
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(include),
                    '-i', str(ROOT / 'patches/arm-guest-memory.patch')], check=True)
    pristine = out / 'pristine'
    pristine.mkdir(exist_ok=True)
    (pristine / 'recomp_types.h').write_text(original)
    (pristine / 'recomp_icall_feedback.h').write_bytes((include / 'recomp_icall_feedback.h').read_bytes())
    print(f'Generated {len(db)} synthetic functions at {out}')

if __name__ == '__main__':
    main()
