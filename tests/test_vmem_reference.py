# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed controls for the optional hash-pinned VM reference comparison."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('verify_vmem_reference', Path(__file__).resolve().parents[1] / 'tools/verify_vmem_reference.py')
VM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VM)


class ReferenceControls(unittest.TestCase):
    def transcript(self, rows=VM.EXPECTED):
        return 'other diagnostic\nvmem-trace: ' + json.dumps(rows) + '\n'

    def test_exact_trace(self):
        self.assertEqual(VM.compare_trace(self.transcript()), VM.EXPECTED)

    def test_no_missing_or_duplicate_execution(self):
        for text in ('', self.transcript() * 2, self.transcript(VM.EXPECTED[:-1]), self.transcript(VM.EXPECTED + [[0, 0, 0]])):
            with self.subTest(text=text), self.assertRaises(ValueError):
                VM.compare_trace(text)

    def test_defined_state_mismatch_fails(self):
        rows = [row.copy() for row in VM.EXPECTED]
        rows[1][0] = 0
        with self.assertRaisesRegex(ValueError, 'behavior mismatch'):
            VM.compare_trace(self.transcript(rows))

    def test_bad_guest_width_or_skips_fail(self):
        for row in ([0, -1, 4096], [0, 1 << 32, 4096], [False, 0, 0], ['skip', 0, 0], [0, 0]):
            rows = [r.copy() for r in VM.EXPECTED]; rows[0] = row
            with self.subTest(row=row), self.assertRaises(ValueError):
                VM.compare_trace(self.transcript(rows))

    def test_source_missing_or_modified_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, 'Missing pinned source'):
                VM.verified_sources(temp)
            source = Path(temp) / 'src/kernel/guest_vmem.c'
            source.parent.mkdir(parents=True); source.write_text('unverified source')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                VM.verified_sources(temp)


if __name__ == '__main__':
    unittest.main()
