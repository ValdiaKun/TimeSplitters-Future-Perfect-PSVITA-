#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only, bounded local title intake. Never generates or executes retail code.

The supported XBE hash authenticates the approved build. A local provenance file
binds analysis bytes to that hash and pinned sources, but is not proof that the
analysis itself is correct. Reports omit observed private paths and file contents.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUPPORTED_SHA256 = '3cfd001a84fc3e08175d6c4b2e42ebf49577a089b5d0bd87ac724f61a41816bc'
TITLE_ID = 0x45410066
MAX_XBE = 256 * 1024 * 1024
MAX_JSON = 32 * 1024 * 1024
MAX_ANALYSIS = 128 * 1024 * 1024
FILES = {'functions': 'disasm/functions.json', 'labels': 'disasm/labels.json',
         'summary': 'disasm/summary.json', 'identified': 'func_id/identified_functions.json',
         'abi': 'abi/abi_functions.json'}
LOCATIONS = {'xbe': 'private/game/default.xbe', 'provenance': 'private/title-analysis/provenance.json',
             **{k: 'private/title-analysis/' + v for k, v in FILES.items()}}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def _pairs(items):
    out = {}
    for key, value in items:
        if key in out:
            raise ValueError('duplicate_json_key')
        out[key] = value
    return out


def _invalid_constant(value):
    raise ValueError('nonfinite_json_constant')


def decode_json(data):
    return json.loads(data.decode('utf-8'), object_pairs_hook=_pairs, parse_constant=_invalid_constant)


def _bytes(path, limit):
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('input_size_limit')
    return data


def _input_path(root, path):
    resolved = path.resolve()
    if not any(resolved.is_relative_to(root / folder) for folder in ('private', 'game-data')):
        raise ValueError('input_outside_ignored_data')
    return resolved


def source_identity(root, lock):
    pc = root / 'external/pc'
    revision = subprocess.check_output(['git', '-C', str(pc), 'rev-parse', 'HEAD'],
                                       text=True, stderr=subprocess.DEVNULL).strip()
    if revision != lock['pc']['revision']:
        return False
    if subprocess.check_output(['git', '-C', str(pc), 'status', '--porcelain',
                                '--untracked-files=normal'], text=True, stderr=subprocess.DEVNULL):
        return False
    return all(digest((pc / name).read_bytes()) == expected
               for name, expected in lock['pc']['sha256'].items())


def parse_xbe(root, data):
    """Reuse the pinned project reader on the exact already-hashed byte snapshot."""
    path = root / 'external/pc/tools/compiler/xbe_image.py'
    name = '_tsfp_verified_intake_xbe'
    spec = importlib.util.spec_from_file_location(name, path)
    if not spec or not spec.loader:
        raise ValueError('parser_unavailable')
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    old_bytecode = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = old_bytecode
    parser = module.XBEParser.__new__(module.XBEParser)
    parser.path = Path('default.xbe')
    parser.data = data
    parser.sections = []
    parser.header = None
    return parser.parse()


def _address(value):
    # Pinned BatchTranslator calls int(value,16), not an arbitrary host pointer.
    if not isinstance(value, str) or not re.fullmatch(r'(?:0x)?[0-9a-fA-F]{1,8}', value):
        raise ValueError('invalid_metadata_address')
    return int(value, 16)


def _within(xbe, address, size=1, executable=False, file_backed=True):
    if size <= 0 or address + size > 0x100000000:
        return False
    return any((not executable or s.flags & 4) and s.virtual_addr <= address and
               address + size <= s.virtual_addr + (min(s.virtual_size, s.raw_size) if file_backed else s.virtual_size)
               for s in xbe.sections)


def metadata_coverage(xbe, documents):
    functions = documents['functions']
    if not isinstance(functions, list) or not functions:
        raise ValueError('empty_or_invalid_functions')
    starts = set()
    for record in functions:
        if not isinstance(record, dict):
            raise ValueError('invalid_function_record')
        start, end = _address(record.get('start')), _address(record.get('end'))
        if start in starts or not _within(xbe, start, end - start, executable=True):
            raise ValueError('invalid_or_duplicate_function_extent')
        starts.add(start)
    if xbe.header.entry_point not in starts:
        raise ValueError('missing_entry_function')
    counts = {'functions': len(starts)}
    for key, field in (('labels', 'address'), ('identified', 'start'), ('abi', 'address')):
        records = documents[key]
        if not isinstance(records, list):
            raise ValueError('invalid_metadata_list')
        seen = set()
        for record in records:
            if not isinstance(record, dict):
                raise ValueError('invalid_metadata_record')
            address = _address(record.get(field))
            if address in seen or (not _within(xbe, address, file_backed=False) if key == 'labels' else address not in starts):
                raise ValueError('invalid_or_duplicate_metadata_reference')
            if key == 'labels' and (not isinstance(record.get('name'), str) or not record['name']):
                raise ValueError('invalid_label_name')
            if key == 'abi' and record.get('calling_convention') not in {
                    'cdecl', 'stdcall', 'fastcall', 'thiscall', 'thiscall_cdecl'}:
                raise ValueError('unsupported_abi_convention')
            seen.add(address)
        if key == 'abi' and xbe.header.entry_point not in seen:
            raise ValueError('missing_entry_abi')
        counts[key] = len(seen)
    summary = documents['summary']
    if not isinstance(summary, dict) or not isinstance(summary.get('binary'), str):
        raise ValueError('invalid_summary')
    # Windows paths are data here too; never expose their text in the report.
    if summary['binary'].replace('\\', '/').rsplit('/', 1)[-1].lower() != 'default.xbe':
        raise ValueError('summary_binary_mismatch')
    return counts


def inspect_inputs(root=ROOT, xbe=None, analysis=None, destination=None):
    root = root.resolve()
    report = {'schema_version': 1, 'status': 'rejected', 'trace_unavailable': True,
              'retail_boot': 'not_run', 'generation': 'not_run', 'errors': [],
              'expected_xbe_sha256': SUPPORTED_SHA256,
              'required_inputs': [], 'analysis_assurance': 'local_hash_bound_attestation_only'}
    try:
        lock = decode_json(_bytes(root / 'sources.lock.json', MAX_JSON))
        if not source_identity(root, lock):
            raise ValueError('source_pin_mismatch')
        report['source_pins'] = {key: lock['pc'][key] for key in ('revision', 'runtime_revision')}
        dest = (destination or root / 'build/guest-retail').resolve()
        if dest == root / 'build' or not dest.is_relative_to(root / 'build'):
            raise ValueError('destination_outside_ignored_build')
        xbe_path = _input_path(root, xbe or root / LOCATIONS['xbe'])
        analysis_dir = _input_path(root, analysis or root / 'private/title-analysis')
        paths = {'xbe': xbe_path, 'provenance': analysis_dir / 'provenance.json',
                 **{key: analysis_dir / name for key, name in FILES.items()}}
        for key, path in paths.items():
            if key != 'xbe' and not path.resolve().is_relative_to(analysis_dir):
                raise ValueError('metadata_path_escape')
            if not path.is_file() and path.exists():
                raise ValueError('input_not_regular_file')
            report['required_inputs'].append({'slot': key, 'expected_location': LOCATIONS[key],
                                             'state': 'present' if path.is_file() else 'missing'})
        # Authenticate any supplied XBE even if the analysis is still missing.
        image = None
        if xbe_path.is_file():
            raw = _bytes(xbe_path, MAX_XBE)
            if digest(raw) != SUPPORTED_SHA256:
                raise ValueError('unsupported_xbe_sha256')
            image = parse_xbe(root, raw)
            if image.certificate.title_id != TITLE_ID or not image.certificate.game_region & 1 or image.header.is_debug:
                raise ValueError('unsupported_title_region_or_debug')
            report['title'] = {'title_id': f'{TITLE_ID:08x}', 'region': 'US-compatible',
                               'certificate_version': image.certificate.version}
        if any(i['state'] == 'missing' for i in report['required_inputs']):
            report['status'] = 'missing_inputs'
            report['trace_unavailable_reason'] = 'supported_executable_and_bound_analysis_required'
            return report
        provenance = decode_json(_bytes(paths['provenance'], MAX_JSON))
        if not isinstance(provenance, dict) or type(provenance.get('version')) is not int or provenance['version'] != 1:
            raise ValueError('invalid_provenance')
        for key, expected in (('xbe_sha256', SUPPORTED_SHA256), ('pc_revision', lock['pc']['revision']),
                              ('runtime_revision', lock['pc']['runtime_revision'])):
            if provenance.get(key) != expected:
                raise ValueError('provenance_source_mismatch')
        hashes = provenance.get('files')
        if not isinstance(hashes, dict) or set(hashes) != set(FILES.values()):
            raise ValueError('provenance_file_set_mismatch')
        documents, total = {}, 0
        for key, name in FILES.items():
            data = _bytes(paths[key], MAX_JSON)
            total += len(data)
            if total > MAX_ANALYSIS:
                raise ValueError('analysis_size_limit')
            if not isinstance(hashes[name], str) or not re.fullmatch(r'[0-9a-f]{64}', hashes[name]) or digest(data) != hashes[name]:
                raise ValueError('metadata_hash_mismatch')
            documents[key] = decode_json(data)
        report['metadata_coverage'] = metadata_coverage(image, documents)
        supported = {int(n) for n in re.findall(r'case\s+(\d+):\s+return\s+\w+;',
                                               (root / 'guest/kernel.c').read_text())}
        ordinals = {i.ordinal for i in image.kernel_imports}
        report['kernel_import_coverage'] = {'total': len(ordinals),
            'implemented_subset_ordinals': sorted(ordinals & supported),
            'unsupported_ordinals': sorted(ordinals - supported),
            'assurance': 'dispatch_presence_only_not_argument_mode_or_boot_support'}
        report['status'] = 'validated_inputs'
        report['trace_unavailable_reason'] = 'generation_and_runtime_entry_not_executed'
    except (OSError, ValueError, KeyError, TypeError, RecursionError, RuntimeError, subprocess.SubprocessError, ImportError) as error:
        # Do not serialize exception text: paths, JSON contents and parser strings
        # can contain private data. Only controlled error codes are emitted.
        code = str(error)
        if type(error) is not ValueError or not re.fullmatch(r'[a-z_][a-z_0-9]*', code):
            code = 'input_validation_failed'
        report['errors'].append(code)
        report['trace_unavailable_reason'] = 'intake_rejected'
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, help='user-owned XBE under ignored private/ or game-data/')
    parser.add_argument('--analysis-dir', type=Path, help='hash-bound local analysis root under ignored input directories')
    parser.add_argument('--destination', type=Path, help='intended generated-code subdirectory of build/ (not created)')
    args = parser.parse_args(argv)
    report = inspect_inputs(xbe=args.xbe, analysis=args.analysis_dir, destination=args.destination)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report['status'] == 'validated_inputs' else 2 if report['status'] == 'missing_inputs' else 3


if __name__ == '__main__':
    raise SystemExit(main())
