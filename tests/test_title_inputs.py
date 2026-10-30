# SPDX-License-Identifier: GPL-3.0-only
"""Asset-free title intake checks. Test-only supported hash never reaches CLI."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('title_intake', ROOT / 'tools/check_title_inputs.py')
INTAKE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INTAKE)


def fixture_xbe():
    data = bytearray(0x1020)
    data[:4] = b'XBEH'
    def word(offset, value):
        struct.pack_into('<I', data, offset, value)
    for offset, value in [(0x104,0x10000),(0x108,0x1000),(0x10c,0x3000),(0x110,0x178),
                          (0x118,0x10500),(0x11c,2),(0x120,0x10200),
                          (0x128,0x11000^0xa8fc57ab),(0x158,0x12000^0x5b6d40b6),
                          (0x500,0x1d0),(0x508,INTAKE.TITLE_ID),(0x5a0,1)]:
        word(offset, value)
    data[0x300:0x306] = b'.text\0'
    data[0x310:0x316] = b'.data\0'
    for offset, flags, address, raw, name in [(0x200,4,0x11000,0x1000,0x10300),
                                              (0x238,1,0x12000,0x1010,0x10310)]:
        for member, value in [(0,flags),(4,address),(8,16),(12,raw),(16,16),(20,name)]:
            word(offset+member,value)
    data[0x1000]=0xc3  # Authored RET, not retail bytes.
    word(0x1010,0x80000068)
    return bytes(data)


class TitleIntake(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.lock = json.loads((ROOT/'sources.lock.json').read_text())
        (self.root/'sources.lock.json').write_text(json.dumps(self.lock))
        (self.root/'guest').mkdir()
        (self.root/'guest/kernel.c').write_text('case 104: return current_thread;')
        # Symlink only trusted parser source into the synthetic test checkout.
        (self.root/'external').mkdir()
        (self.root/'external/pc').symlink_to(ROOT/'external/pc',target_is_directory=True)
        self.xbe = self.root/'private/game/default.xbe'
        self.analysis = self.root/'private/title-analysis'
        self.xbe.parent.mkdir(parents=True)
        self.raw = fixture_xbe()
        self.xbe.write_bytes(self.raw)
        self.documents = {
            'functions':[{'start':'0x00011000','end':'0x00011001','name':'entry'}],
            'labels':[{'address':'0x00011000','name':'entry'}],
            'identified':[{'start':'0x00011000','name':'entry'}],
            'abi':[{'address':'0x00011000','calling_convention':'cdecl'}],
            'summary':{'binary':'default.xbe'},
        }
        self.hash_patch = mock.patch.object(INTAKE,'SUPPORTED_SHA256',hashlib.sha256(self.raw).hexdigest())
        self.hash_patch.start()
        self.identity_patch = mock.patch.object(INTAKE,'source_identity',return_value=True)
        self.identity_patch.start()
        self.write_metadata()

    def tearDown(self):
        self.identity_patch.stop(); self.hash_patch.stop(); self.temp.cleanup()

    def write_metadata(self):
        hashes={}
        for key,name in INTAKE.FILES.items():
            p=self.analysis/name;p.parent.mkdir(parents=True,exist_ok=True)
            p.write_text(json.dumps(self.documents[key]))
            hashes[name]=hashlib.sha256(p.read_bytes()).hexdigest()
        self.provenance={'version':1,'xbe_sha256':INTAKE.SUPPORTED_SHA256,
                         'pc_revision':self.lock['pc']['revision'],
                         'runtime_revision':self.lock['pc']['runtime_revision'],'files':hashes}
        (self.analysis/'provenance.json').write_text(json.dumps(self.provenance))

    def inspect(self,**kwargs):
        return INTAKE.inspect_inputs(root=self.root,**kwargs)

    def test_validated_snapshot_not_boot_or_generation(self):
        before={str(p.relative_to(self.root)):p.read_bytes() for p in (self.root/'private').rglob('*') if p.is_file()}
        r=self.inspect();self.assertEqual(r['status'],'validated_inputs',r)
        self.assertTrue(r['trace_unavailable']);self.assertEqual(r['retail_boot'],'not_run')
        self.assertEqual(r['kernel_import_coverage']['implemented_subset_ordinals'],[104])
        self.assertFalse((self.root/'build').exists())
        after={str(p.relative_to(self.root)):p.read_bytes() for p in (self.root/'private').rglob('*') if p.is_file()}
        self.assertEqual(before,after)

    def test_missing_input_manifest_and_no_private_paths(self):
        self.xbe.unlink();(self.analysis/'abi/abi_functions.json').unlink()
        r=self.inspect();self.assertEqual(r['status'],'missing_inputs')
        self.assertEqual({x['slot'] for x in r['required_inputs'] if x['state']=='missing'},{'xbe','abi'})
        self.assertNotIn(str(self.root),json.dumps(r))

    def test_bad_xbe_rejected_even_when_metadata_missing(self):
        self.xbe.write_bytes(b'unsupported user-owned data');(self.analysis/'provenance.json').unlink()
        r=self.inspect();self.assertEqual(r['errors'],['unsupported_xbe_sha256'])
        self.assertEqual(r['status'],'rejected')

    def test_bss_label_is_valid_but_bss_function_is_not(self):
        raw=bytearray(self.raw);struct.pack_into('<I',raw,0x240,32)
        self.xbe.write_bytes(raw)
        with mock.patch.object(INTAKE,'SUPPORTED_SHA256',hashlib.sha256(raw).hexdigest()):
            self.documents['labels'].append({'address':'0x12010','name':'bss_value'})
            self.write_metadata();self.assertEqual(self.inspect()['status'],'validated_inputs')
            self.documents['functions'].append({'start':'0x12010','end':'0x12011'})
            self.write_metadata();self.assertEqual(self.inspect()['errors'],['invalid_or_duplicate_function_extent'])

    def test_metadata_mutation_rejected(self):
        (self.analysis/INTAKE.FILES['functions']).write_text('[]')
        self.assertEqual(self.inspect()['errors'],['metadata_hash_mismatch'])

    def test_wrong_provenance_and_file_set(self):
        for key,value,error in [('xbe_sha256','0'*64,'provenance_source_mismatch'),
                                ('pc_revision','0'*40,'provenance_source_mismatch'),
                                ('runtime_revision','0'*40,'provenance_source_mismatch'),
                                ('version',True,'invalid_provenance'),('files',{},'provenance_file_set_mismatch')]:
            with self.subTest(key=key):
                p=copy.deepcopy(self.provenance);p[key]=value
                (self.analysis/'provenance.json').write_text(json.dumps(p))
                self.assertEqual(self.inspect()['errors'],[error])

    def test_bad_function_extent_entry_and_duplicates(self):
        for records,error in [([], 'empty_or_invalid_functions'),
                              ([{'start':'0x12000','end':'0x12001'}],'invalid_or_duplicate_function_extent'),
                              ([{'start':'0x11001','end':'0x11002'}],'missing_entry_function'),
                              ([{'start':'0x11000','end':'0xffffffff'}],'invalid_or_duplicate_function_extent'),
                              (self.documents['functions']*2,'invalid_or_duplicate_function_extent')]:
            with self.subTest(error=error):
                self.documents['functions']=records;self.write_metadata()
                self.assertEqual(self.inspect()['errors'],[error])

    def test_missing_entry_abi_and_bad_summary(self):
        self.documents['abi']=[];self.write_metadata()
        self.assertEqual(self.inspect()['errors'],['missing_entry_abi'])
        self.documents['abi']=[{'address':'0x11000','calling_convention':'cdecl'}]
        self.documents['summary']={'binary':'/private/person/another.xbe'};self.write_metadata()
        r=self.inspect();self.assertEqual(r['errors'],['summary_binary_mismatch'])
        self.assertNotIn('/private/person',json.dumps(r))

    def test_unsupported_import_is_reported_not_faked(self):
        (self.root/'guest/kernel.c').write_text('case 187: return close_handle;')
        r=self.inspect();self.assertEqual(r['status'],'validated_inputs')
        self.assertEqual(r['kernel_import_coverage']['unsupported_ordinals'],[104])
        self.assertTrue(r['trace_unavailable'])

    def test_bad_source_pin_and_destination(self):
        with mock.patch.object(INTAKE,'source_identity',return_value=False):
            self.assertEqual(self.inspect()['errors'],['source_pin_mismatch'])
        self.assertEqual(self.inspect(destination=self.root/'build')['errors'],['destination_outside_ignored_build'])
        self.assertEqual(self.inspect(destination=self.root/'private/generated')['errors'],['destination_outside_ignored_build'])

    def test_input_and_metadata_symlink_escape(self):
        outside=self.root/'outside.json';outside.write_text('{}')
        self.assertEqual(self.inspect(xbe=outside)['errors'],['input_outside_ignored_data'])
        target=self.analysis/INTAKE.FILES['abi'];target.unlink();target.symlink_to(outside)
        self.assertEqual(self.inspect()['errors'],['metadata_path_escape'])

    def test_duplicate_json_and_bounds_fail_closed(self):
        (self.analysis/'provenance.json').write_text('{"version":1,"version":1}')
        self.assertEqual(self.inspect()['errors'],['duplicate_json_key'])
        with mock.patch.object(INTAKE,'MAX_XBE',16):
            self.assertEqual(self.inspect()['errors'],['input_size_limit'])

    def test_title_region_and_debug_checks(self):
        parsed=INTAKE.parse_xbe(self.root,self.raw)
        for field,value in [('title_id',0),('game_region',0)]:
            with self.subTest(field=field):
                original=getattr(parsed.certificate,field);setattr(parsed.certificate,field,value)
                with mock.patch.object(INTAKE,'parse_xbe',return_value=parsed):
                    self.assertEqual(self.inspect()['errors'],['unsupported_title_region_or_debug'])
                setattr(parsed.certificate,field,original)
        parsed.header.is_debug=True
        with mock.patch.object(INTAKE,'parse_xbe',return_value=parsed):
            self.assertEqual(self.inspect()['errors'],['unsupported_title_region_or_debug'])


if __name__=='__main__':unittest.main()
