#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/run the PC oracle and ARM adapter, compare deterministic transcripts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
from prepare_upstream import ROOT

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm-cc', help='ARMv7 Linux cross-compiler command')
    parser.add_argument('--qemu', default='qemu-arm', help='ARM userspace runner command, including optional -L sysroot')
    args = parser.parse_args()
    commands = []
    def run(command):
        print('+ ' + shlex.join(command), flush=True)
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        commands.append(dict(command=command, returncode=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
        if result.stdout: print(result.stdout, end='')
        if result.stderr: print(result.stderr, end='', file=sys.stderr)
        result.check_returncode()
        return result.stdout
    out = ROOT / 'build/guest'
    out.mkdir(parents=True, exist_ok=True)
    report = dict(source_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
                  manifest_sha256=hashlib.sha256((ROOT / 'sources.lock.json').read_bytes()).hexdigest(),
                  host=platform.platform(), scope='synthetic CPU fixtures, not game or Vita hardware', commands=commands)
    try:
        run([sys.executable, 'tools/generate_fixtures.py'])
        report['generated_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [out / 'fixtures.c', out / 'include/recomp_types.h', out / 'include/pc_memory_contract.h']}
        base = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-label',
                '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-fno-strict-aliasing',
                '-ffp-contract=off', '-Iguest', '-Itests']
        sources = ['tests/guest_harness.c', 'guest/memory.c', 'guest/address_space.c', 'tests/memory_suite.c', 'tests/pc_memory_oracle.c', 'build/guest/fixtures.c', '-lm', '-pthread']
        host_cc = shlex.split(os.environ.get('CC', 'cc'))
        run(host_cc + ['--version'])
        transcripts = {}
        for mode in ('pristine', 'portable', 'sanitized'):
            includes = ['-Ibuild/guest/pristine', '-DTSFP_PC_MAPPED_ORACLE'] if mode == 'pristine' else ['-Ibuild/guest/include', '-DTSFP_PORTABLE_GUEST_MEMORY']
            sanitizers = ['-fsanitize=undefined,address', '-fno-sanitize-recover=all'] if mode == 'sanitized' else []
            target = f'build/guest/host-{mode}'
            run(host_cc + base + includes + sanitizers + sources + ['-o', target])
            transcripts[mode] = run([str(ROOT / target)])
        if args.arm_cc:
            cc = shlex.split(args.arm_cc)
            run(cc + ['--version'])
            run(shlex.split(args.qemu) + ['--version'])
            target = 'build/guest/arm-portable'
            run(cc + base + ['-mcpu=cortex-a9', '-mfpu=vfpv3',
                '-mfloat-abi=hard', '-mno-unaligned-access', '-DTSFP_REQUIRE_ARMV7',
                '-DTSFP_PORTABLE_GUEST_MEMORY', '-Ibuild/guest/include', '-static'] + sources + ['-o', target])
            transcripts['armv7-qemu'] = run(shlex.split(args.qemu) + ['-cpu', 'cortex-a9', str(ROOT / target)])
        if len(set(transcripts.values())) != 1:
            raise RuntimeError('CPU fixture transcript mismatch')
        report.update(result='pass', transcripts=transcripts, arm_executed=bool(args.arm_cc))
    except Exception as error:
        report.update(result='fail', error=str(error))
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

if __name__ == '__main__':
    main()
