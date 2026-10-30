# SPDX-License-Identifier: GPL-3.0-only
"""Authored data only; no title image, names or instruction bytes are needed."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('fault_symbolizer', ROOT/'tools/symbolize_guest_fault.py')
SYMBOLS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SYMBOLS)


def fault(pc=0x132a0):
    return {'schema_version': 1, 'kind': 2, 'access': 0, 'pc': pc, 'address': 0,
            'detail': 0, 'thread_id': 41, 'flags': 2, 'flags_valid': 0xcd5,
            'width': 0, 'registers': [0]*8}


def functions():
    return [{'start': '0x132a0', 'end': '0x132a3'}]


class FaultSymbolizer(unittest.TestCase):
    def test_real_fault_fixture_shape_and_mutated_controls(self):
        report = SYMBOLS.symbolize(fault(), functions())
        self.assertEqual((report['status'], report['function_start'], report['offset']),
                         ('mapped', 0x132a0, 0))
        missing = fault(); del missing['registers']
        self.assertEqual(SYMBOLS.symbolize(missing, functions())['status'], 'rejected')
        # A valid, changed PC must not silently inherit the earlier mapping.
        self.assertEqual(SYMBOLS.symbolize(fault(0x132a3), functions())['status'], 'unmapped')
        changed = fault(); changed['pc'] = '0x132a0'
        self.assertEqual(SYMBOLS.symbolize(changed, functions())['status'], 'rejected')

    def test_end_exclusive_exact_bounds_and_gaps(self):
        for pc, status, offset in [(0x1329f, 'unmapped', None), (0x132a0, 'mapped', 0),
                                   (0x132a2, 'mapped', 2), (0x132a3, 'unmapped', None),
                                   (0, 'unmapped', None), (0xffffffff, 'unmapped', None)]:
            with self.subTest(pc=pc):
                r = SYMBOLS.symbolize(fault(pc), functions())
                self.assertEqual(r['status'], status)
                self.assertEqual(r.get('offset'), offset)
                if status == 'unmapped':
                    self.assertNotIn('function_start', r)

    def test_sorted_lookup_reports_original_index_not_name(self):
        records = [{'start': '0x2000', 'end': '0x2003', 'name': '/private/person/SECRET'},
                   {'start': '0x1000', 'end': '0x1003', 'bytes': 'PROPRIETARY_BYTES'}]
        r = SYMBOLS.symbolize(fault(0x1001), records)
        self.assertEqual((r['function_start'], r['offset'], r['function_index']), (0x1000, 1, 1))
        self.assertEqual(r['assurance'], 'unverified_local_map')
        text = json.dumps(r)
        for value in ('SECRET', '/private', 'PROPRIETARY_BYTES', 'name', 'bytes'):
            self.assertNotIn(value, text)

    def test_adjacent_ranges_and_largest_legal_end(self):
        r = SYMBOLS.symbolize(fault(0x1003), [{'start':'0x1000','end':'0x1003'},
                                            {'start':'0x1003','end':'0x1004'}])
        self.assertEqual(r['function_index'], 1)
        maximum = [{'start':'0xfffffffe','end':'0xffffffff'}]
        self.assertEqual(SYMBOLS.symbolize(fault(0xfffffffe), maximum)['status'], 'mapped')
        self.assertEqual(SYMBOLS.symbolize(fault(0xffffffff), maximum)['status'], 'unmapped')

    def test_invalid_ranges_duplicates_overlap_and_limits(self):
        cases = [[], {}, [None], [{'start':'0','end':'1'}],
                 [{'start':'0x100','end':'0x100'}], [{'start':'0x101','end':'0x100'}],
                 [{'start':'0xfffffffe','end':'0x100000000'}],
                 [{'start':True,'end':'0x100'}], [{'start':'-1','end':'0x100'}],
                 functions()*2,
                 [{'start':'0x100','end':'0x110'}, {'start':'0x105','end':'0x120'}]]
        for records in cases:
            with self.subTest(records=records):
                self.assertEqual(SYMBOLS.symbolize(fault(), records)['status'], 'rejected')
        with mock.patch.object(SYMBOLS, 'MAX_FUNCTIONS', 1):
            r = SYMBOLS.symbolize(fault(), [{'start':'0x100','end':'0x101'},
                                          {'start':'0x200','end':'0x201'}])
            self.assertEqual(r['errors'], ['invalid_function_list'])

    def test_fault_schema_and_integer_types_are_strict(self):
        for field in fault():
            values = [True, False, None, -1, '0', 0.0]
            if field == 'registers':
                values += [[], [0]*7, [0]*9, [0]*7+[True], [0]*7+[0x100000000]]
            elif field == 'schema_version':
                values += [0, 2]
            elif field == 'kind':
                values += [0, 6]
            elif field == 'access':
                values += [5]
            elif field == 'width':
                values += [0x10000000000000000]
            else:
                values += [0x100000000]
            for value in values:
                with self.subTest(field=field, value=value):
                    record = fault(); record[field] = value
                    self.assertEqual(SYMBOLS.symbolize(record, functions())['status'], 'rejected')
        record = fault(); record['extra'] = '/private/person'
        self.assertEqual(SYMBOLS.symbolize(record, functions())['errors'], ['invalid_fault_fields'])
        record = fault(); record['width'] = 0xffffffffffffffff
        for field in SYMBOLS.U32_FIELDS: record[field] = 0xffffffff
        self.assertEqual(SYMBOLS.symbolize(record, functions())['status'], 'unmapped')
        for kind in range(1,6):
            for access in range(5):
                record = fault(); record.update(kind=kind, access=access)
                self.assertEqual(SYMBOLS.symbolize(record, functions())['status'], 'mapped')

    def test_exact_file_hashes_and_no_input_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp)/'fault.json', Path(tmp)/'functions.json'
            ab = json.dumps(fault(), indent=2).encode(); bb = json.dumps(functions()).encode()
            a.write_bytes(ab); b.write_bytes(bb)
            report = SYMBOLS.inspect_files(a,b)
            self.assertEqual(report['status'], 'mapped')
            self.assertEqual(report['input_hashes'], {'fault_sha256':hashlib.sha256(ab).hexdigest(),
                                                      'functions_sha256':hashlib.sha256(bb).hexdigest()})
            self.assertEqual(report['hash_scope'], 'exact_input_file_bytes')
            self.assertEqual(a.read_bytes(), ab); self.assertEqual(b.read_bytes(), bb)

    def test_private_errors_duplicate_json_nonfinite_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp)/'SECRET-private-fault.json', Path(tmp)/'functions.json'
            b.write_text(json.dumps(functions()))
            r = SYMBOLS.inspect_files(a,b)
            self.assertEqual(r['status'], 'rejected')
            for text in (tmp, 'SECRET', 'No such file'): self.assertNotIn(text,json.dumps(r))
            for data in ('{"pc":1,"pc":2}', 'NaN', '"SECRET-private-content"',
                         '{"secret": "PRIVATE_PARSE_ERROR",'):
                a.write_text(data)
                r = SYMBOLS.inspect_files(a,b)
                self.assertEqual(r['status'], 'rejected')
                self.assertNotIn('PRIVATE_PARSE_ERROR',json.dumps(r))
            a.write_text(json.dumps(fault()))
            with mock.patch.object(SYMBOLS,'MAX_FAULT_BYTES',1):
                self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'rejected')
            with mock.patch.object(SYMBOLS,'MAX_FUNCTION_BYTES',1):
                self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'rejected')
            if hasattr(os,'mkfifo'):
                a.unlink(); os.mkfifo(a)
                self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'rejected')

    def test_exact_byte_bounds_and_argument_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b=Path(tmp)/'fault.json',Path(tmp)/'functions.json'
            data=json.dumps(fault()).encode();a.write_bytes(data+b' '*(4096-len(data)))
            bb=json.dumps(functions()).encode();b.write_bytes(bb)
            self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'mapped')
            a.write_bytes(a.read_bytes()+b' ')
            self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'rejected')
            a.write_bytes(data)
            with mock.patch.object(SYMBOLS,'MAX_FUNCTION_BYTES',len(bb)):
                self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'mapped')
                b.write_bytes(bb+b' ')
                self.assertEqual(SYMBOLS.inspect_files(a,b)['status'],'rejected')
            for arguments in [[],['--fault'],['--unknown','/PRIVATE/SECRET']]:
                result=subprocess.run([sys.executable,str(ROOT/'tools/symbolize_guest_fault.py'),*arguments],
                                      capture_output=True,text=True)
                self.assertEqual(result.returncode,3)
                self.assertEqual(json.loads(result.stdout)['errors'],['invalid_arguments'])
                self.assertEqual(result.stderr,'')
                self.assertNotIn('SECRET',result.stdout)

    def test_cli_exit_codes(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b=Path(tmp)/'fault.json',Path(tmp)/'functions.json'
            b.write_text(json.dumps(functions()))
            for pc,expected,status in [(0x132a0,0,'mapped'),(0,2,'unmapped')]:
                a.write_text(json.dumps(fault(pc)))
                result=subprocess.run([sys.executable,str(ROOT/'tools/symbolize_guest_fault.py'),
                                       '--fault',str(a),'--functions',str(b)],check=False,capture_output=True,text=True)
                self.assertEqual(result.returncode,expected)
                self.assertEqual(json.loads(result.stdout)['status'],status)
                self.assertEqual(result.stderr,'')
            a.write_text('{}')
            result=subprocess.run([sys.executable,str(ROOT/'tools/symbolize_guest_fault.py'),
                                   '--fault',str(a),'--functions',str(b)],capture_output=True,text=True)
            self.assertEqual(result.returncode,3)
            self.assertEqual(json.loads(result.stdout)['status'],'rejected')
            self.assertNotIn(tmp,result.stdout+result.stderr)


if __name__ == '__main__': unittest.main()
