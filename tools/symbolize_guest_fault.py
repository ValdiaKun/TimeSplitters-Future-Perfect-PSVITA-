#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded offline PC lookup. Local maps/hashes are not authenticated title data.

No names, bytes, paths, nearest-symbol guesses or stack inference are reported.
The CLI reads snapshots only; it never generates code, writes inputs or contacts
services. Exit 0=mapped, 2=unmapped, 3=rejected.
"""
import argparse
import bisect
import os
from pathlib import Path
import stat
import sys

# Reuse the established strict JSON/address grammar without preparing upstream.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_title_inputs import _address, decode_json, digest

MAX_FAULT_BYTES = 4096
MAX_FUNCTION_BYTES = 32 * 1024 * 1024
MAX_FUNCTIONS = 100000
UINT32_MAX = 0xffffffff
UINT64_MAX = 0xffffffffffffffff
FAULT_FIELDS = {'schema_version', 'kind', 'access', 'pc', 'address', 'detail',
                'thread_id', 'flags', 'flags_valid', 'width', 'registers'}
U32_FIELDS = ('pc', 'address', 'detail', 'thread_id', 'flags', 'flags_valid')


def _uint(value, maximum):
    return type(value) is int and 0 <= value <= maximum


def validate_fault(fault):
    if not isinstance(fault, dict) or set(fault) != FAULT_FIELDS:
        raise ValueError('invalid_fault_fields')
    if type(fault['schema_version']) is not int or fault['schema_version'] != 1:
        raise ValueError('unsupported_fault_schema')
    if not _uint(fault['kind'], 5) or fault['kind'] == 0:
        raise ValueError('invalid_fault_kind')
    if not _uint(fault['access'], 4):
        raise ValueError('invalid_fault_access')
    if any(not _uint(fault[key], UINT32_MAX) for key in U32_FIELDS):
        raise ValueError('invalid_fault_uint32')
    if not _uint(fault['width'], UINT64_MAX):
        raise ValueError('invalid_fault_width')
    registers = fault['registers']
    if not isinstance(registers, list) or len(registers) != 8 or any(
            not _uint(value, UINT32_MAX) for value in registers):
        raise ValueError('invalid_fault_registers')
    return fault['pc']


def function_ranges(functions):
    if not isinstance(functions, list) or not functions or len(functions) > MAX_FUNCTIONS:
        raise ValueError('invalid_function_list')
    ranges, starts = [], set()
    for index, record in enumerate(functions):
        if not isinstance(record, dict):
            raise ValueError('invalid_function_record')
        try:
            start, end = _address(record.get('start')), _address(record.get('end'))
        except ValueError:
            raise ValueError('invalid_function_address') from None
        if not start or not end or end <= start:
            raise ValueError('invalid_function_range')
        if start in starts:
            raise ValueError('duplicate_function_start')
        starts.add(start)
        ranges.append((start, end, index))
    ranges.sort()
    if any(previous[1] > current[0] for previous, current in zip(ranges, ranges[1:])):
        raise ValueError('overlapping_function_ranges')
    return ranges


def _report():
    return {'schema_version': 1, 'status': 'rejected', 'assurance': 'unverified_local_map',
            'lookup': 'pc_only_end_exclusive', 'index_semantics': 'original_zero_based_functions_list',
            'hash_scope': 'in_memory_objects', 'input_hashes': None, 'errors': []}


def symbolize(fault, functions):
    """Map exactly one validated PC into validated ranges, returning numeric fields.

    This API receives already-decoded objects and therefore does not claim file
    byte hashes. The CLI adds hashes of the exact bounded input snapshots. Extra
    function metadata is ignored, never copied into the report. A map can still
    be incorrect or belong to another title; structural validation proves neither.
    """
    report = _report()
    try:
        pc = validate_fault(fault)
        ranges = function_ranges(functions)
        report['pc'] = pc
        index = bisect.bisect_right([item[0] for item in ranges], pc) - 1
        if index >= 0 and pc < ranges[index][1]:
            start, _, original_index = ranges[index]
            report.update(status='mapped', function_start=start, offset=pc-start,
                          function_index=original_index)
        else:
            report['status'] = 'unmapped'
    except ValueError as error:
        # Only the controlled validation codes above enter reports.
        report['errors'].append(str(error))
    return report


def _snapshot(path, limit):
    # Nonblocking open permits rejecting FIFOs/devices without hanging. Regular
    # files ignore O_NONBLOCK. No private path or OS exception text is returned.
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0))
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError('input_not_regular_file')
        with os.fdopen(descriptor, 'rb') as stream:
            descriptor = None
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ValueError('input_size_limit')
        return data
    finally:
        if descriptor is not None:
            os.close(descriptor)


def inspect_files(fault_path, functions_path):
    report = _report()
    try:
        fault_bytes = _snapshot(fault_path, MAX_FAULT_BYTES)
        functions_bytes = _snapshot(functions_path, MAX_FUNCTION_BYTES)
        fault = decode_json(fault_bytes)
        functions = decode_json(functions_bytes)
        report = symbolize(fault, functions)
        report['hash_scope'] = 'exact_input_file_bytes'
        report['input_hashes'] = {'fault_sha256': digest(fault_bytes),
                                  'functions_sha256': digest(functions_bytes)}
    except (OSError, ValueError, RecursionError, RuntimeError):
        # Parser/OS exceptions may include file contents or private path text.
        report['errors'].append('input_read_or_json_rejected')
    return report


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        # argparse's normal error can echo arbitrary supplied private paths.
        import json
        report = _report()
        report['errors'] = ['invalid_arguments']
        print(json.dumps(report, sort_keys=True))
        raise SystemExit(3)


def main(argv=None):
    parser = _Parser(description=__doc__)
    parser.add_argument('--fault', required=True, type=Path)
    parser.add_argument('--functions', required=True, type=Path)
    args = parser.parse_args(argv)
    report = inspect_files(args.fault, args.functions)
    # Import only here to keep the in-memory mapping API lightweight.
    import json
    print(json.dumps(report, sort_keys=True))
    return {'mapped': 0, 'unmapped': 2, 'rejected': 3}[report['status']]


if __name__ == '__main__':
    raise SystemExit(main())
