#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the real PC recompiler with Vita memory emission and verified US input.

Synthetic fixtures use configure() too. Full retail generation remains untested
without a user-supplied supported executable and matching analysis metadata.
"""
import argparse
import contextlib
import hashlib
from pathlib import Path
import subprocess
import sys
from prepare_upstream import ROOT, prepare

SUPPORTED_SHA256 = '3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc'

def verify_input(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != SUPPORTED_SHA256:
        raise ValueError('Unsupported Xbox executable: expected documented US default.xbe SHA-256')

def configure(runtime):
    sys.path.insert(0, str(runtime))
    from tools.recomp.translator import FunctionTranslator
    from tools.recomp import config, lifter
    from guest_lowering import (install, install_overflow_conditions,
                                install_scalar_sse_moves, install_stack_memory)
    original = FunctionTranslator.translate_function
    if not getattr(FunctionTranslator, '_tsfp_configured', False):
        install_overflow_conditions(lifter)
        install_scalar_sse_moves(lifter.Lifter)
        install_stack_memory(lifter.Lifter)
        install(FunctionTranslator)
        FunctionTranslator._tsfp_original = original
        FunctionTranslator._tsfp_configured = True
    return FunctionTranslator, config, FunctionTranslator._tsfp_original

def write_headers(runtime, destination):
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'recomp_types.h').write_bytes((runtime / 'templates/runtime/recomp_types.h').read_bytes())
    (destination / 'recomp_icall_feedback.h').write_bytes((runtime / 'src/kernel/recomp_icall_feedback.h').read_bytes())
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(destination),
                    '-i', str(ROOT / 'patches/arm-guest-memory.patch')], check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xbe', type=Path)
    parser.add_argument('--destination', type=Path, default=ROOT / 'build/guest-retail')
    args, forwarded = parser.parse_known_args()
    destination = args.destination.resolve()
    if not destination.is_relative_to((ROOT / 'build').resolve()) or destination == (ROOT / 'build').resolve():
        parser.error('Destination must be a subdirectory of this checkout’s ignored build directory')
    if any(a in ('-o','--output-dir') or a.startswith('--output-dir=') or a.startswith('-o') for a in forwarded):
        parser.error('Use --destination; upstream output overrides are not allowed')
    verify_input(args.xbe)
    runtime = prepare()
    configure(runtime)
    from tools.recomp.__main__ import main as recompile
    saved = sys.argv
    try:
        sys.argv = ['tools.recomp', str(args.xbe.resolve()), '--output-dir', str(destination), *forwarded]
        destination.mkdir(parents=True, exist_ok=True)
        # Single-function upstream mode prints generated C; keep it local too.
        with (destination / 'compiler.stdout.txt').open('w') as log:
            with contextlib.redirect_stdout(log):
                recompile()
    finally:
        sys.argv = saved
    write_headers(runtime, destination)

if __name__ == '__main__':
    main()
