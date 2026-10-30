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


# Mode 0 is unprefixed, mode 1 F3/REP(E), mode 2 F2/REPNE.
STRING_FIXTURES = {}
address = 0x12400
for kind, opcodes in ((1,('a4','66a5','a5')), (2,('aa','66ab','ab')),
                      (3,('a6','66a7','a7')), (4,('ae','66af','af')),
                      (5,('ac','66ad','ad'))):
    for width, opcode in zip((1,2,4),opcodes):
        for mode, prefix in ((0,''),(1,'f3'),(2,'f2')):
            name = f'fixture_string_{kind}_{width}_{mode}'
            FIXTURES[address] = (name, prefix + opcode + 'c3')
            STRING_FIXTURES[(kind,width,mode)] = name
            address += 0x20
# A real incoming CMP followed by a zero-count compare and condition consumers.
FIXTURES[0x12a00] = ('fixture_string_seed', '39d8f3a69f0f90c2c3')
STRING_FLAG_FIXTURES = {}
address = 0x12b00
for kind, opcodes in ((3,('a6','66a7','a7')), (4,('ae','66af','af'))):
    for width, opcode in zip((1,2,4),opcodes):
        for mode, prefix in ((0,''),(1,'f3'),(2,'f2')):
            name = f'fixture_string_flags_{kind}_{width}_{mode}'
            FIXTURES[address] = (name,prefix+opcode+'9f0f90c2c3')
            STRING_FLAG_FIXTURES[(kind,width,mode)] = name
            address += 0x20
FIXTURES[0x13000] = ('fixture_string_branch', 'f3a67406bb01000000c3bb02000000c3')
FIXTURES[0x13020] = ('fixture_string_carry', 'f3a683d300c3')
FIXTURES[0x13040] = ('fixture_string_cld', 'fcf3a4c3')
FIXTURES[0x13060] = ('fixture_string_std', 'fdf3a4fcc3')
FIXTURES[0x13100] = ('fixture_xadd_flags', 'f00fc1199f0f90c2c3')
FIXTURES[0x13120] = ('fixture_cmpxchg_flags', 'f00fb1199f0f90c2c3')
FIXTURES[0x13140] = ('fixture_xadd_raw', 'f00fc119c3')
FIXTURES[0x13160] = ('fixture_cmpxchg_raw', 'f00fb119c3')
FIXTURES[0x13180] = ('fixture_xadd_alias', 'f00fc100c3')
FIXTURES[0x131a0] = ('fixture_xadd_carry', 'f00fc11983d200c3')
FIXTURES[0x131c0] = ('fixture_atomic_string', 'f00fc119f3a69f0f90c2c3')
FIXTURES[0x131e0] = ('fixture_cmpxchg_branch', 'f00fb1197506ba01000000c3ba02000000c3')

FIXTURES[0x13200] = ('fixture_thread_args', '8b4424048b542408c20800')
FIXTURES[0x13220] = ('fixture_flag_leaf', 'bb78563412c3')
FIXTURES[0x13240] = ('fixture_flag_call', 'a6e8daffffffc3')
FIXTURES[0x13260] = ('fixture_fault_load', '8b01c3')
FIXTURES[0x13280] = ('fixture_fault_store', '8902c3')
FIXTURES[0x132a0] = ('fixture_fault_divide', 'f7f1c3')

# Three real indirect kernel calls through guest import-table addresses in ESI.
FIXTURES[0x13300] = ('fixture_title_entry', 'ff1689c7648b1d2800000064a1040000008b0868100000006800000900ff56046825000000ff5608c3')
FIXTURES[0x13340] = ('fixture_kernel_call', 'ff16c3')

FIXTURES[0x13400] = ('fixture_image_entry', 'be00800800e9f6feffff')

FIXTURES[0x13500] = ('fixture_tls_entry', 'a144c008002b0540c00800030550c0080083c00f83e0f083c004c1e802f7d8a320c0080089c1648b15040000008b5c8a04891d200009008b5c8a14891d24000900648b1d280000008b5b28891d28000900be00800800ff3540000900ff560ca344000900e997feffff')

FIXTURES[0x13600] = ('fixture_tls_worker', '8b0d20c00800648b1504000000648b1d280000008b73288d3c8a8b832c01000089448a088b448a04c3')


# Authored PsCreateSystemThreadEx -> wait -> close, and its two-context worker.
FIXTURES[0x13900] = ('fixture_service_parent', 'bea000090068a03901006a006a006a166a0b68880009006a006a006a006884000900ff16a38c00090085c075206a006a00ff3584000900ff5604a394000900ff3584000900ff5608a398000900c3')
FIXTURES[0x13a00] = ('fixture_service_call', 'ff16c3')
FIXTURES[0x139a0] = ('fixture_service_child', '8b442404a3b00009008b5424088915b4000900648b0d280000008b812c010000a3b8000900c20800')

FIXTURES[0x13b80] = ('fixture_allocation_sequence', 'bee00009006a04680030000068040109006a006800010900ff16a32001090085c075358b3d00010900c707785634128b0f890d24010900c7050401090000000000680080000068040109006800010900ff5604a328010900c3')
FIXTURES[0x13b00] = ('fixture_event_sequence', 'bee00009006a006a006a006800010900ff16a3200109006804010900ff3500010900ff5604a32401090068080109006a00ff3500010900ff560ca328010900ff3500010900ff5608a32c010900ff3500010900ff5610a330010900c3')

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
    table = ['#include "recomp_types.h"', 'typedef void (*TsfpStringFixture)(void);']
    for name in STRING_FIXTURES.values():
        table.append(f'void {name}(void);')
    table.append('TsfpStringFixture string_fixture(unsigned kind, unsigned width, unsigned mode) {')
    for (kind,width,mode), name in STRING_FIXTURES.items():
        table.append(f'if (kind == {kind} && width == {width} && mode == {mode}) return {name};')
    table.append('return 0; }')
    for name in STRING_FLAG_FIXTURES.values():
        table.append(f'void {name}(void);')
    table.append('TsfpStringFixture string_flag_fixture(unsigned kind, unsigned width, unsigned mode) {')
    for (kind,width,mode), name in STRING_FLAG_FIXTURES.items():
        table.append(f'if (kind == {kind} && width == {width} && mode == {mode}) return {name};')
    table.append('return 0; }')
    table.insert(0,'#include <stddef.h>')
    for address in (0x12660,0x13200,0x13280,0x132a0,0x13300,0x13400,0x13500,0x13600,0x13900,0x139a0,0x13a00,0x13b00,0x13b80):
        name,raw=FIXTURES[address]
        data=bytes.fromhex(raw)
        table.append(f'const unsigned char {name}_bytes[] = {{'+','.join(str(b) for b in data)+'};')
        table.append(f'const size_t {name}_size = {len(data)};')
    (out / 'string_table.c').write_text('\n'.join(table))
    (out / 'fixtures.c').write_text('\n'.join(chunks))
    import re
    kernel = (runtime / 'src/kernel/kernel.h').read_text()
    layout = (runtime / 'src/kernel/xbox_memory_layout.h').read_text()
    bridge = (runtime / 'src/kernel/kernel_bridge.c').read_text()
    if '#define BRIDGE_HANDLE_TAG  0x48000000u' not in bridge:
        raise RuntimeError('Pinned opaque handle tag changed')
    memory_layout = (runtime / 'src/kernel/xbox_memory_layout.c').read_text()
    if not re.search(r'typedef struct _XBOX_OBJECT_ATTRIBUTES\s*\{\s*HANDLE\s+RootDirectory;\s*PXBOX_ANSI_STRING\s+ObjectName;\s*ULONG\s+Attributes;\s*\}',kernel):
        raise RuntimeError('Pinned Xbox OBJECT_ATTRIBUTES field order changed')
    for name,value in (('XboxNotificationEvent',0),('XboxSynchronizationEvent',1)):
        if not re.search(rf'\b{name}\s*=\s*{value}\b',kernel):
            raise RuntimeError(f'Pinned event type changed: {name}')
    platform_types = (runtime / 'src/platform/xbox_winnt.h').read_text()
    if (not re.search(r'typedef\s+uint8_t\s+BOOLEAN\b', platform_types) or
        not re.search(r'xbox_NtWaitForSingleObject\(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout\)', kernel) or
        not re.search(r'handle,\s*\(BOOLEAN\)alertable,\s*XBOX_TO_NATIVE\(timeout_ptr\)', bridge)):
        raise RuntimeError('Pinned NtWaitForSingleObject BOOLEAN width/bridge changed')
    if not re.search(r'xbox_PsCreateSystemThreadEx\([^)]*\bBOOLEAN\s+CreateSuspended,\s*BOOLEAN\s+DebugStack,', kernel):
        raise RuntimeError('Pinned thread-creation BOOLEAN argument contract changed')
    for ordinal, size, name in ((184,20,'NtAllocateVirtualMemory'),(199,12,'NtFreeVirtualMemory'),(189,16,'NtCreateEvent'),(225,8,'NtSetEvent'),(186,4,'NtClearEvent'),(255,40,'PsCreateSystemThreadEx'),(233,12,'NtWaitForSingleObject'),(187,4,'NtClose'),(104,0,'KeGetCurrentThread'),(320,8,'RtlZeroMemory'),(258,4,'PsTerminateSystemThread')):
        if not re.search(rf'case {ordinal}: return\s+{size};',bridge) or not re.search(rf'case {ordinal}: return bridge_{name};',bridge):
            raise RuntimeError(f'Pinned kernel ordinal/ABI changed: {ordinal} {name}')
    for name,value in (('XBOX_THREAD_OBJ_SIZE','0x200u'),('XBOX_THREAD_ID_OFF','0x12Cu')):
        if not re.search(rf'#define {name}\s+{value}\b',memory_layout):
            raise RuntimeError(f'Pinned thread-object layout changed: {name}')
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
