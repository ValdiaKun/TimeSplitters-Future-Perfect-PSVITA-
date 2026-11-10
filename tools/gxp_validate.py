#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded structural/interface validation of the project's GXP shader profile.

Format facts: MIT open-shacccg, revision
26da8ebc44717147d002c60d4a80cd6604aace7f, src/gxp/gxp_{reader,writer}.{cpp,hpp}.
This independently authored byte parser does not decode USSE, establish hardware
acceptance, or accept every GXP extension. Unknown auxiliary tables fail closed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

MAX_SIZE = 1024 * 1024
CATEGORIES = {0: 'attribute', 1: 'uniform', 2: 'sampler'}
TYPES = {0: 'f32', 1: 'f16', 2: 'c10', 3: 'u32', 4: 's32',
         5: 'u16', 6: 's16', 7: 'u8', 8: 's8'}


class GxpError(ValueError):
    """Invalid or unsupported shader profile, with a bounded diagnostic code."""


def parse_gxp(data, expected_stage=None):
    """Return JSON-safe reflection; reject malformed/unsupported metadata.

    Input is a complete raw binary, including at most three zero alignment bytes.
    `expected_stage` must be vertex/fragment when provided, so a modified stage
    cannot silently satisfy a caller's authored interface. USSE semantics and the
    content of opaque varying records still need compiler/device validation.
    """
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise GxpError('input_type')
    byte_count = data.nbytes if isinstance(data, memoryview) else len(data)
    if byte_count > MAX_SIZE:
        raise GxpError('size_limit')
    data = bytes(data)
    if len(data) < 0x9c:
        raise GxpError('truncated_header')
    if data[:4] != b'GXP\0':
        raise GxpError('magic')
    major, minor = data[4:6]
    if (major, minor) not in ((1, 4), (1, 5)):
        raise GxpError('unsupported_revision')
    prefix = 0x98 if minor == 4 else 0x9c

    def u16(offset):
        return struct.unpack_from('<H', data, offset)[0]

    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]

    logical = u32(8)
    if logical < prefix + 32 or logical > len(data):
        raise GxpError('declared_size')
    if len(data) != ((logical + 3) & ~3) or any(data[logical:]):
        raise GxpError('physical_padding')
    stage = 'fragment' if u32(0x14) & 1 else 'vertex'
    if expected_stage is not None and expected_stage not in ('vertex', 'fragment'):
        raise GxpError('expected_stage')
    if expected_stage is not None and expected_stage != stage:
        raise GxpError('stage_mismatch')
    regions = [(0, prefix, 'header')]

    def relative(field, count, width, label, allow_zero=True):
        # Header offsets are unsigned and relative to the offset field itself.
        encoded = u32(field)
        if not count and not encoded and allow_zero:
            return field, field
        start = field + encoded
        length = count * width
        if start > logical or length > logical - start:
            raise GxpError(label + '_bounds')
        if length:
            alignment = 2 if label == 'sampler_query' else 4
            if start % alignment:
                raise GxpError(label + '_alignment')
            regions.append((start, start + length, label))
        return start, start + length

    interface_start, interface_end = relative(0x2c, 1, 32, 'interface')
    if interface_start != prefix:
        raise GxpError('unsupported_interface_layout')
    primary_count, secondary_count = u32(0x3c), u32(0x44)
    if not primary_count:
        raise GxpError('empty_primary')
    primary = relative(0x40, primary_count, 8, 'primary')
    secondary = relative(0x48, secondary_count, 8, 'secondary', False)
    secondary_end = 0x4c + u32(0x4c)
    if secondary_end > logical or secondary_end != secondary[1]:
        raise GxpError('secondary_end')
    if primary[0] & 3 or secondary[0] & 3:
        raise GxpError('code_alignment')
    # Named buffers below are register/external-resource counts, not inline
    # lengths. The writer exposes no inline table for them; bound the counters
    # and validate its literal-data anchor without pretending they are bytes.
    buffer_counts = {name: u32(offset) for name, offset in (
        ('scratch', 0x50), ('thread', 0x54), ('literal', 0x58),
        ('data', 0x5c), ('texture', 0x60), ('default_uniform', 0x64))}
    if any(value > 65536 for value in buffer_counts.values()):
        raise GxpError('buffer_count_limit')
    relative(0x68, 0, 1, 'literal_data')
    # Canonical compiler output supports literals/containers. Uniform-buffer,
    # dependent-sampler and texture-dependent-sampler tables are not emitted by
    # the audited writer and their nonempty extensions are deliberately refused.
    for count_field, offset_field, label in ((0x78, 0x7c, 'uniform_buffer'),
            (0x80, 0x84, 'dependent_sampler'), (0x88, 0x8c, 'texture_dependent_sampler')):
        if u32(count_field):
            raise GxpError('unsupported_' + label)
        relative(offset_field, 0, 1, label)
    literal_count = u32(0x70)
    literal_start, _ = relative(0x74, literal_count, 8, 'literals')
    literal_resources = set()
    for index in range(literal_count):
        resource = u32(literal_start + index * 8)
        if resource > 65535 or resource in literal_resources:
            raise GxpError('literal_resource')
        literal_resources.add(resource)
    container_count = u32(0x90)
    container_start, _ = relative(0x94, container_count, 8, 'containers')
    containers = {}
    for index in range(container_count):
        ident, reserved, base, size = struct.unpack_from('<4H', data, container_start + index * 8)
        if ident > 31 or ident in containers or base + size > 65536:
            raise GxpError('container_fields')
        containers[ident] = {'index': ident, 'base': base, 'size': size, 'reserved': reserved}
    if minor == 5 and u32(0x98):
        relative(0x98, 16, 2, 'sampler_query')
    parameter_count = u32(0x24)
    parameter_start, _ = relative(0x28, parameter_count, 16, 'parameters')
    parameters, names, bindings = [], set(), {}
    for index in range(parameter_count):
        offset = parameter_start + index * 16
        name_offset = offset + struct.unpack_from('<i', data, offset)[0]
        if not prefix <= name_offset < logical:
            raise GxpError('parameter_name_offset')
        end = data.find(b'\0', name_offset, min(logical, name_offset + 256))
        if end < 0:
            raise GxpError('parameter_name_termination')
        try:
            name = data[name_offset:end].decode('ascii')
        except UnicodeDecodeError as error:
            raise GxpError('parameter_name_encoding') from error
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.\[\]]{0,254}', name):
            raise GxpError('parameter_name')
        if name in names:
            raise GxpError('duplicate_parameter_name')
        names.add(name); regions.append((name_offset, end + 1, 'name:' + name))
        category_id, type_id = data[offset + 4] & 15, data[offset + 4] >> 4
        components, container = data[offset + 5] & 15, data[offset + 5] >> 4
        array_size, resource = u32(offset + 8), u32(offset + 12)
        if category_id not in CATEGORIES or type_id not in TYPES:
            raise GxpError('unsupported_parameter_type')
        if not 1 <= components <= 4 or not 1 <= array_size <= 65536:
            raise GxpError('parameter_shape')
        if components * array_size > 65536 or resource > 65535:
            raise GxpError('parameter_resource')
        if category_id == 0 and stage != 'vertex':
            raise GxpError('fragment_attribute')
        if category_id == 2:
            if array_size != 1 or resource > 15:
                raise GxpError('sampler_resource')
            span = 1
        else:
            span = components * array_size
            if resource + span > 65536:
                raise GxpError('parameter_resource')
        key = category_id, container
        bindings.setdefault(key, []).append((resource, resource + span))
        parameters.append({'name': name, 'category': CATEGORIES[category_id],
            'type': TYPES[type_id], 'components': components, 'array_size': array_size,
            'resource_index': resource, 'container_index': container,
            'semantic': data[offset + 6], 'semantic_index': data[offset + 7]})
    for spans in bindings.values():
        previous_end = -1
        for first, last in sorted(spans):
            if first < previous_end:
                raise GxpError('overlapping_parameter_resources')
            previous_end = last
    # Only known interface/code overlap forms are legal; all table/string/code
    # aliases are rejected, even if each individual pointer is in bounds.
    active = []
    for current in sorted(regions):
        start, end, label = current
        active = [old for old in active if old[1] > start]
        for other in active:
            pair = {label, other[2]}
            allowed = (stage == 'fragment' and pair == {'interface', 'secondary'} and
                       secondary[0] == interface_start + 20)
            allowed |= (stage == 'fragment' and not secondary_count and
                        pair == {'interface', 'primary'} and primary[0] == interface_start + 24)
            if not allowed:
                raise GxpError('overlapping_regions:' + other[2] + ':' + label)
        active.append(current)
    return {'stage': stage, 'version': {'major': major, 'minor': minor, 'sdk': u16(6)},
        'logical_size': logical, 'physical_size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'flags': u32(0x14), 'primary_instruction_count': primary_count,
        'secondary_instruction_count': secondary_count, 'parameters': parameters,
        'containers': list(containers.values()), 'buffer_counts': buffer_counts}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    parser.add_argument('--stage', choices=('vertex', 'fragment'))
    args = parser.parse_args(argv)
    try:
        with args.file.open('rb') as stream:
            data = stream.read(MAX_SIZE + 1)
    except OSError:
        parser.exit(2, 'invalid_gxp: input_io\n')
    try:
        result = parse_gxp(data, args.stage)
    except GxpError as error:
        parser.exit(2, 'invalid_gxp: ' + str(error) + '\n')
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
