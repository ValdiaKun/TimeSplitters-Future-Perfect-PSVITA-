#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run hash-pinned xboxrecomp VM code against an asset-free bounded host shim.

The upstream implementation is MIT (2026 sp00nz); download and retain its LICENSE
alongside the source, never substitute an unpinned source. This exercises the
upstream page tracker, not Windows virtual-memory behavior or physical Vita.
An optional actual native-heap transcript makes this a differential check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import urllib.request

REVISION = 'f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b'
PROJECT = 'https://github.com/sp00nznet/xboxrecomp'
FILES = {
    'src/kernel/guest_vmem.c': '711b2368326ce1630bdd05ba7244e2ce6a42bbf253bfac8e7d9254be912a5262',
    'src/kernel/guest_vmem.h': 'eaacdcc7d600c5d188929ad2224b4f266286960615588e530e71acac85efad3c',
    'LICENSE': 'de6601cea3a6bf2d821b6b89f59b9d6e719cc017e530bcb4bbe2fcf7306e6dda',
}
# Same input sequence as tests/heap_suite.c. Upstream release reports the freed
# extent; the pinned PC contract instead clears both outputs. Normalize only
# that documented successful-release output, not statuses, faults or failures.
EXPECTED = [[0, 0x100000, 0x1000],
            [0xc0000018, 0x100000, 0x1000],
            [0, 0x100000, 0x1000],
            [0, 0x100000, 0x1000],
            [0, 0x100000, 0x1000],
            [0xc000000d, 0x100001, 0],
            [0, 0, 0]]

HEADER = r'''
#ifndef TSFP_VMEM_REFERENCE_SHIM_H
#define TSFP_VMEM_REFERENCE_SHIM_H
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
typedef pthread_mutex_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define AcquireSRWLockExclusive(lock) ((void)pthread_mutex_lock(lock))
#define ReleaseSRWLockExclusive(lock) ((void)pthread_mutex_unlock(lock))
typedef size_t SIZE_T;
typedef struct { void *BaseAddress; size_t RegionSize; unsigned State; } MEMORY_BASIC_INFORMATION;
#define MEM_FREE 0x10000u
#define MEM_RESERVE 0x2000u
#define MEM_COMMIT 0x1000u
#define MEM_RELEASE 0x8000u
#define PAGE_NOACCESS 1u
#define PAGE_READWRITE 4u
size_t VirtualQuery(const void *,MEMORY_BASIC_INFORMATION *,size_t);
void *VirtualAlloc(void *,size_t,unsigned,unsigned);
int VirtualFree(void *,size_t,unsigned);
unsigned long GetLastError(void);
#endif
'''
RUNNER = r'''
#define _POSIX_C_SOURCE 200809L
#include "xbox_memory_layout.h"
#include "guest_vmem.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char backing[0x10000],committed[16];
size_t VirtualQuery(const void *p,MEMORY_BASIC_INFORMATION *m,size_t bytes) {
    (void)bytes; uintptr_t a=(uintptr_t)p,lo=(uintptr_t)backing;
    *m=(MEMORY_BASIC_INFORMATION){(void *)lo,sizeof(backing),MEM_FREE};
    if (a!=lo) *m=(MEMORY_BASIC_INFORMATION){(void *)a,4096,MEM_RESERVE};
    return sizeof(*m);
}
void *VirtualAlloc(void *p,size_t bytes,unsigned type,unsigned protect) {
    (void)protect; uintptr_t a=(uintptr_t)p,lo=(uintptr_t)backing;
    if (a<lo || a-lo>=sizeof(backing) || !bytes || bytes>sizeof(backing)-(a-lo)) return NULL;
    if (type==MEM_RESERVE) return p;
    if (type!=MEM_COMMIT || ((a-lo)|bytes)&4095u) return NULL;
    for (size_t page=(a-lo)/4096;page<(a-lo+bytes)/4096;++page) {
        if (!committed[page]) memset(backing+page*4096,0,4096);
        committed[page]=1;
    }
    return p;
}
int VirtualFree(void *p,size_t bytes,unsigned type) {
    return p==backing && !bytes && type==MEM_RELEASE;
}
unsigned long GetLastError(void) { return 8; }
static int first=1;
static void row(uint32_t status,uint32_t base,uint32_t size,int release) {
    if (!status && release) base=size=0; /* Documented PC-output normalization. */
    printf("%s[%u,%u,%u]",first?"":",",status,base,size); first=0;
}
static void allocate(uint32_t base,uint32_t size,unsigned type) {
    uint32_t status=0xdeadbeef;
    assert(guest_vmem_allocate(&base,&size,type,4,&status)); row(status,base,size,0);
}
static void release(uint32_t base,uint32_t size,unsigned type) {
    uint32_t status=0xdeadbeef;
    assert(guest_vmem_free(&base,&size,type,&status)); row(status,base,size,type==MEM_RELEASE);
}
int main(void) {
    memset(backing,0x5a,sizeof(backing)); assert(!setenv("RECOMP_EXT_VMA","1",1));
    assert(guest_vmem_init((ptrdiff_t)((uintptr_t)backing-0x100000u),0x100000u,0x100000u));
    printf("vmem-trace: [");
    allocate(0x100001,1,0x3000); assert(backing[0]==0 && backing[4095]==0 && backing[4096]==0x5a);
    allocate(0x100000,4096,0x3000);
    backing[0]=0xa5; allocate(0x100000,4096,0x1000); assert(backing[0]==0xa5);
    release(0x100001,1,0x4000); assert(backing[0]==0);
    allocate(0x100000,4096,0x1000); assert(backing[0]==0);
    release(0x100001,0,0x8000);
    release(0x100000,0,0x8000);
    printf("]\n");
    /* A real upstream limitation, not a native-runtime expected success:
     * allocate_locked checks presence of reserve/commit but ignores extra bits.
     * This negative control keeps our stricter profile difference visible. */
    uint32_t base=0x100000,size=4096,status=0xdeadbeef;
    assert(guest_vmem_allocate(&base,&size,0x3001,4,&status) && status==0);
    printf("vmem-reference-limit: extra-allocation-bit=accepted\n");
    guest_vmem_shutdown(); return 0;
}
'''


def verified_sources(root, fetch=False):
    """Return source directory only after all hashes (including license) match."""
    root = Path(root)
    for name, expected in FILES.items():
        path = root / name
        if not path.is_file():
            if not fetch:
                raise ValueError(f'Missing pinned source {path}; pass --fetch explicitly')
            data = urllib.request.urlopen(
                f'https://raw.githubusercontent.com/sp00nznet/xboxrecomp/{REVISION}/{name}',
                timeout=60).read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f'Hash mismatch downloading {name}')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Pinned source hash mismatch: {name}')
    return root / 'src/kernel'


def parse_trace(text):
    matches = re.findall(r'^vmem-trace: (.*)$', text, flags=re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f'Expected exactly one vmem-trace, got {len(matches)}')
    trace = json.loads(matches[0])
    if not isinstance(trace, list) or len(trace) != len(EXPECTED):
        raise ValueError('Incomplete or extra VM trace operations')
    for row in trace:
        if not isinstance(row, list) or len(row) != 3 or any(type(v) is not int or not 0 <= v <= 0xffffffff for v in row):
            raise ValueError('Invalid uint32 VM trace row')
    return trace


def compare_trace(text, expected=EXPECTED):
    actual = parse_trace(text)
    if actual != expected:
        raise ValueError(f'VM behavior mismatch: expected {expected}, got {actual}')
    return actual


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path('.cache/references/xboxrecomp-vmem'))
    parser.add_argument('--build-dir', type=Path, default=Path('build/vmem-reference'))
    parser.add_argument('--fetch', action='store_true', help='download hash-pinned public MIT sources if missing')
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--actual-trace', type=Path, help='actual native heap suite stdout with exactly one vmem-trace line')
    args = parser.parse_args(argv)
    source = verified_sources(args.source_root, args.fetch).resolve()
    build = args.build_dir.resolve(); build.mkdir(parents=True, exist_ok=True)
    (build / 'xbox_memory_layout.h').write_text(HEADER)
    (build / 'runner.c').write_text(RUNNER)
    binary = build / 'vmem-reference'
    command = shlex.split(args.cc) + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I' + str(build), '-I' + str(source), str(source / 'guest_vmem.c'),
        str(build / 'runner.c'), '-pthread', '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    compare_trace(result.stdout)
    if 'vmem-reference-limit: extra-allocation-bit=accepted' not in result.stdout:
        raise ValueError('Reference unsupported-bit negative control did not execute')
    if args.actual_trace:
        compare_trace(args.actual_trace.read_text())
    print(result.stdout, end='')
    print(f'vmem-reference-v1: revision={REVISION} operations={len(EXPECTED)} '
          f'actual={"compared" if args.actual_trace else "unavailable"} scope=bounded-host-shim')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
