#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the pinned submodule, reconstruct and patch its ignored runtime."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PC = ROOT / 'external/pc'
RUNTIME = PC / 'external/ts3FPrecomp'

def prepare():
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    revision = subprocess.check_output(['git', '-C', str(PC), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != lock['pc']['revision']:
        raise RuntimeError('PC source revision differs from sources.lock.json; initialize the pinned submodule')
    if subprocess.check_output(['git', '-C', str(PC), 'status', '--porcelain', '--untracked-files=normal'], text=True):
        raise RuntimeError('PC source has local edits; preserve them and use a clean pinned checkout')
    for path, expected in lock['pc']['sha256'].items():
        if hashlib.sha256((PC / path).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'PC source hash mismatch: {path}')
    for command in ([ 'tools/build/prepare-runtime.py'],
                    ['tools/build/apply-patches.py', str(RUNTIME), str(PC / 'patches')]):
        subprocess.run([__import__('sys').executable, str(PC / command[0]), *command[1:]], check=True)
    return RUNTIME

if __name__ == '__main__':
    prepare()
