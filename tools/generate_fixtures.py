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
}

def main():
    runtime = prepare()
    sys.path.insert(0, str(runtime))
    from tools.recomp.translator import FunctionTranslator
    from tools.recomp import config
    out = ROOT / 'build/guest'
    include = out / 'include'
    include.mkdir(parents=True, exist_ok=True)
    config._install([config.Section('.text', 0x10000, 0x1000, 0, 0x1000, True),
                     config.Section('.data', 0x18000, 0x8000, 0x1000, 0x8000, False)],
                    0x10000, 0, 'asset-free-vita-fixtures')
    image = bytearray(0x9000)
    db = {}
    for va, (name, hex_bytes) in FIXTURES.items():
        code = bytes.fromhex(hex_bytes)
        image[va - 0x10000:va - 0x10000 + len(code)] = code
        db[va] = dict(name=name, end=va + len(code), size=len(code), _addr=va)
    translator = FunctionTranslator(bytes(image), db, seh_prolog=[], seh_epilog=[])
    chunks = ['#include "recomp_types.h"\n']
    # Same global-register aliases supplied by upstream batch translation.
    chunks += [f'#define {reg} g_{reg}\n' for reg in
               ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'esp',
                *(f'xmm{i}' for i in range(8)))]
    chunks += [f'void {item[0]}(void);\n' for item in FIXTURES.values()]
    for va, info in db.items():
        body = translator.translate_function(va, info)
        if not body or translator.lifter.unimplemented:
            raise RuntimeError(f'Untranslated fixture: {info["name"]}: {translator.lifter.unimplemented}')
        chunks.append(body)
    (out / 'fixtures.c').write_text('\n'.join(chunks))
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
