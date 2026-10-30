#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pin and sanity-check the vendored precompiled GXP shader blobs.

The blobs in ports/vita/src/shaders/ are copied byte-for-byte from vitaGL
(see docs/REFERENCE_AUDIT.md). This test fails closed if a blob changes, so a
modified or stale shader cannot silently enter a build. It checks structure
only; it cannot prove the GPU executes the program (that needs a Vita).
"""
import hashlib
from pathlib import Path
import re
import sys
from gxp_validate import GxpError, parse_gxp

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / 'ports/vita/src/shaders'

# SHA-256 of the raw blob bytes (the C array contents, not the header text).
PINNED = {
    'clear_v': {
        'size': 408,
        'stage': 'vertex',
        'interface': [('position', 'uniform', 'f32', 4, 0), ('u_clear_depth', 'uniform', 'f32', 1, 4)],
        'sha256': 'db475602e9dbf4832615de06377e16d0d135ec5871708a11fb658f8d56758520',
        'names': [b'position\x00', b'u_clear_depth\x00'],
    },
    'clear_f': {
        'size': 236,
        'stage': 'fragment',
        'interface': [('u_clear_color', 'uniform', 'f32', 4, 0)],
        'sha256': '46514eb51a937118d740c9a1ac1825745892783b96c4b82bbfe0ca273785969f',
        'names': [b'u_clear_color\x00'],
    },
}


def load_blob(name):
    text = (SHADERS / f'{name}.h').read_text()
    array = re.search(r'unsigned char %s\[\][^{]*\{(.*?)\};' % name, text, re.S)
    declared = re.search(r'unsigned int size_%s = (\d+);' % name, text)
    if not array or not declared:
        raise AssertionError(f'{name}: array or size declaration not found')
    tokens = array.group(1).strip().rstrip(',').split(',')
    if any(not re.fullmatch(r'\s*0x[0-9a-fA-F]{2}\s*', token) for token in tokens):
        raise AssertionError(f'{name}: non-byte C initializer')
    data = bytes(int(token.strip(), 16) for token in tokens)
    return data, int(declared.group(1)), text


def check(name, expected):
    data, declared, text = load_blob(name)
    return check_blob(name, expected, data, declared, text)


def check_blob(name, expected, data, declared, text):
    problems = []
    try:
        reflected = parse_gxp(data, expected['stage'])
        interface = [(p['name'], p['category'], p['type'], p['components'], p['resource_index'])
                     for p in reflected['parameters']]
        if interface != expected['interface']:
            problems.append('unexpected shader interface')
    except GxpError as error:
        problems.append('GXP structure: ' + str(error))
    if data[:4] != b'GXP\x00':
        problems.append(f'bad magic {data[:4]!r}')
    if declared != expected['size'] or len(data) != expected['size']:
        problems.append(f'size declared={declared} actual={len(data)} pinned={expected["size"]}')
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected['sha256']:
        problems.append(f'sha256 {digest} != pinned {expected["sha256"]}')
    for needle in expected['names']:
        if needle not in data:
            problems.append(f'parameter name {needle!r} missing')
    for marker in ('Rinnegatamante/vitaGL', '3cb7872d6d5808fb1cddf4f643f29306cb53be4e', 'LGPL-3.0'):
        if marker not in text:
            problems.append(f'provenance marker {marker!r} missing from header')
    return problems


def main():
    failures = 0
    for name, expected in PINNED.items():
        problems = check(name, expected)
        if problems:
            failures += 1
            print(f'FAIL {name}:')
            for problem in problems:
                print('  -', problem)
        else:
            print(f'ok   {name} ({expected["size"]} bytes, sha256 pinned)')
    # Negative control: a corrupted blob must be detected by the same checks.
    data, declared, text = load_blob('clear_f')
    corrupted = bytearray(data); corrupted[0] ^= 0xFF
    if not check_blob('clear_f', PINNED['clear_f'], bytes(corrupted), declared, text):
        print('FAIL negative control: corruption not detected by validation')
        failures += 1
    else:
        print('ok   negative control (corrupted blob rejected by actual validation)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
