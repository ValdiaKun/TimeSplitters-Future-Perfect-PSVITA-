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

def verify_fault_record(transcript):
    from check_title_inputs import decode_json
    from symbolize_guest_fault import symbolize
    prefix = 'fault-report-fixture: '
    lines = [line[len(prefix):] for line in transcript.splitlines() if line.startswith(prefix)]
    if len(lines) != 1:
        raise RuntimeError('Expected exactly one captured translated fault record')
    fault = decode_json(lines[0].encode('utf-8'))
    mapped = symbolize(fault, [{'start': '0x132a0', 'end': '0x132a3'}])
    if (mapped.get('status') != 'mapped' or mapped.get('function_start') != 0x132a0 or
        mapped.get('offset') != 0 or fault['kind'] != 2 or fault['access'] != 0 or
        fault['pc'] != 0x132a0 or fault['address'] != 0x132a0 or fault['width'] != 0 or
        fault['detail'] != 0 or fault['thread_id'] != 7 or
        fault['registers'][0] != 10 or fault['registers'][4] != 0x70ff0 or fault['flags'] != 0x893):
        raise RuntimeError('Captured translated fault export/mapping mismatch')
    return fault

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
        run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_guest_lowering.py'])
        run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_vmem_reference.py'])
        report['generated_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [out / 'fixtures.c', out / 'include/recomp_types.h', out / 'include/pc_memory_contract.h', out / 'pristine/fixtures.c', out / 'string_table.c']}
        report['source_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for folder in ('guest', 'tools', 'tests') for p in sorted((ROOT / folder).glob('*'))
            if p.is_file() and p.suffix in ('.c', '.h', '.py')}
        base = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-label',
                '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-fno-strict-aliasing',
                '-ffp-contract=off', '-Iguest', '-Itests']
        sources = ['tests/guest_harness.c', 'guest/memory.c', 'guest/heap.c', 'tests/heap_suite.c', 'guest/execution.c', 'guest/fault_report.c', 'guest/kernel.c', 'tests/kernel_suite.c', 'tests/wait_suite.c', 'tests/event_suite.c', 'tests/worker_suite.c', 'tests/storage_suite.c', 'tests/thread_service_suite.c', 'guest/image.c', 'guest/xbe.c', 'guest/tls.c', 'guest/handles.c', 'guest/worker.c', 'guest/storage.c', 'guest/thread_service.c', 'platform/threads_pthread.c', 'tests/image_suite.c', 'tests/execution_suite.c', 'guest/address_space.c', 'guest/string_ops.c', 'tests/string_suite.c', 'tests/atomic_suite.c', 'tests/string_native_oracle.c', 'build/guest/string_table.c', 'tests/memory_suite.c', 'tests/generated_memory_suite.c', 'tests/mmx_state.c', 'tests/pc_memory_oracle.c', 'build/guest/fixtures.c', '-lm', '-pthread', '-Wl,--wrap=clock_gettime', '-Wl,--wrap=pthread_cond_timedwait', '-Wl,--wrap=__clock_gettime64', '-Wl,--wrap=__pthread_cond_timedwait64', '-Wl,--wrap=pthread_create', '-Wl,--wrap=pthread_join', '-Wl,--wrap=calloc', '-Wl,--wrap=pthread_cond_wait', '-Wl,--wrap=tsfp_event_create', '-Wl,--wrap=tsfp_thread_service_create', '-Wl,--wrap=tsfp_heap_allocate']
        host_cc = shlex.split(os.environ.get('CC', 'cc'))
        run(host_cc + ['--version'])
        run(host_cc + ['-std=c11','-O2','-Wall','-Wextra','-Werror','-DTSFP_PC_MAPPED_ORACLE',
            '-Itests','tests/rep_fault_probe.c','tests/string_native_oracle.c','-o','build/guest/rep-fault-probe'])
        run([str(ROOT / 'build/guest/rep-fault-probe')])
        transcripts = {}
        for mode in ('pristine', 'portable', 'sanitized'):
            includes = ['-Ibuild/guest/pristine', '-DTSFP_PC_MAPPED_ORACLE'] if mode == 'pristine' else ['-Ibuild/guest/include', '-DTSFP_PORTABLE_GUEST_MEMORY']
            sanitizers = ['-fsanitize=undefined,address', '-fno-sanitize-recover=all'] if mode == 'sanitized' else []
            target = f'build/guest/host-{mode}'
            mode_sources = [s.replace('build/guest/fixtures.c', 'build/guest/pristine/fixtures.c') for s in sources] if mode == 'pristine' else sources
            run(host_cc + base + includes + sanitizers + mode_sources + ['-o', target])
            transcripts[mode] = run([str(ROOT / target)])
            if mode == 'pristine': run([sys.executable, 'tools/verify_image_fixture.py'])
        if args.arm_cc:
            cc = shlex.split(args.arm_cc)
            run(cc + ['--version'])
            run(shlex.split(args.qemu) + ['--version'])
            target = 'build/guest/arm-portable'
            run(cc + base + ['-mcpu=cortex-a9', '-mfpu=vfpv3',
                '-mfloat-abi=hard', '-mno-unaligned-access', '-DTSFP_REQUIRE_ARMV7',
                '-DTSFP_PORTABLE_GUEST_MEMORY', '-Ibuild/guest/include', '-static'] + sources + ['-o', target])
            transcripts['armv7-qemu'] = run(shlex.split(args.qemu) + ['-cpu', 'cortex-a9', str(ROOT / target)])
        for transcript in transcripts.values():
            captured = verify_fault_record(transcript)
            # Negative controls prove absence, duplicates and wrong PCs cannot
            # pass merely because all modes share the same bad/empty output.
            captured['pc'] += 3
            for bad in ('', transcript + transcript,
                        'fault-report-fixture: ' + json.dumps(captured)):
                try:
                    verify_fault_record(bad)
                except RuntimeError:
                    pass
                else:
                    raise RuntimeError('Fault-record negative control failed')
        if len(set(transcripts.values())) != 1:
            raise RuntimeError('CPU fixture transcript mismatch')
        actual = out / 'vmem-actual.txt'
        actual.write_text(transcripts['pristine'])
        run([sys.executable, 'tools/verify_vmem_reference.py', '--fetch', '--actual-trace', str(actual)])
        report.update(result='pass', transcripts=transcripts, arm_executed=bool(args.arm_cc))
    except Exception as error:
        report.update(result='fail', error=str(error))
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

if __name__ == '__main__':
    main()
