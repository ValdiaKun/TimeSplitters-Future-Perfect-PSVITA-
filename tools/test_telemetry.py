#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded telemetry checks. Synthetic timestamps are not hardware measurements."""
import argparse
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    print('+', shlex.join([str(part) for part in command]), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--arm-cc')
    parser.add_argument('--qemu', default='qemu-arm')
    args = parser.parse_args()
    out = ROOT / 'build/telemetry-tests'
    out.mkdir(parents=True, exist_ok=True)
    common = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
              '-Iprofiling', 'profiling/telemetry.c', 'tests/test_telemetry.c']
    for name, flags in [('host', []), ('sanitized', ['-fsanitize=address,undefined',
                                                  '-fno-sanitize-recover=all'])]:
        binary = out / name
        run(shlex.split(args.cc) + common + flags + ['-o', str(binary)])
        run([str(binary)])
    if args.arm_cc:
        binary = out / 'arm'
        run(shlex.split(args.arm_cc) + common + ['-mcpu=cortex-a9', '-mfpu=vfpv3',
            '-mfloat-abi=hard', '-mno-unaligned-access', '-static', '-o', str(binary)])
        run(shlex.split(args.qemu) + ['-cpu', 'cortex-a9', str(binary)])
    else:
        print('ARMv7 execution: NOT RUN (supply --arm-cc and --qemu)')
    print('Device/game performance: NOT RUN (synthetic timestamp tests)')


if __name__ == '__main__':
    main()
