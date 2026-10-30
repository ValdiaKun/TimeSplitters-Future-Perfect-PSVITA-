# SPDX-License-Identifier: GPL-3.0-only
"""Asset-free mip-planning regression tests."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('plan_bc_mips', Path(__file__).resolve().parents[1] / 'tools/plan_bc_mips.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MipsTest(unittest.TestCase):
    def test_minimum(self):
        for b in (8, 16):
            p = MODULE.plan_mips(1, 1, b)
            self.assertEqual(p['total_bytes'], b)
            self.assertEqual(p['levels'][0]['offset'], 0)

    def test_rectangular_and_odd_small_mips(self):
        p = MODULE.plan_mips(16, 8, 8)
        self.assertEqual([(x['width'], x['height'], x['size'], x['offset'])
                          for x in p['levels']],
                         [(16, 8, 64, 0), (8, 4, 16, 64),
                          (4, 2, 8, 80), (2, 1, 8, 88), (1, 1, 8, 96)])
        self.assertEqual(p['total_bytes'], 104)

    def test_all_power_two_dimensions_and_blocks(self):
        for b in (8, 16):
            for wexp in range(13):
                for hexp in range(13):
                    p = MODULE.plan_mips(1 << wexp, 1 << hexp, b)
                    self.assertEqual(len(p['levels']), max(wexp, hexp) + 1)
                    cursor = 0
                    for row in p['levels']:
                        self.assertEqual(row['offset'], cursor)
                        self.assertEqual(row['size'],
                                         max(1, (row['width'] + 3) // 4) *
                                         max(1, (row['height'] + 3) // 4) * b)
                        cursor += row['size']
                    self.assertEqual(cursor, p['total_bytes'])

    def test_partial_mips(self):
        p = MODULE.plan_mips(4096, 4096, 16, 1)
        self.assertEqual(p['total_bytes'], 16 * 1024 * 1024)
        self.assertEqual(len(p['levels']), 1)

    def test_invalid_type_and_profiles(self):
        cases = [(True, 4, 8, 0), (0, 4, 8, 0), (3, 4, 8, 0),
                 (8192, 4, 8, 0), (4, 4, 4, 0), (4, 4, 16, -1),
                 (4, 4, 16, 4), (4, 4, 16, 14), (4.0, 4, 8, 0)]
        for args in cases:
            with self.subTest(args=args), self.assertRaises(ValueError):
                MODULE.plan_mips(*args)


if __name__ == '__main__':
    unittest.main()
