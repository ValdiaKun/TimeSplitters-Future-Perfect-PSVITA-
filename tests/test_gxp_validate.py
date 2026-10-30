# SPDX-License-Identifier: GPL-3.0-only
"""Authored byte fixtures validate metadata only, never GPU execution."""
import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location('gxp_validate', Path(__file__).resolve().parents[1] / 'tools/gxp_validate.py')
GXP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GXP)


def fixture(stage='vertex', params=None):
    # Authored canonical v1.4 program: interface[0x98,0xb8), opaque one-word
    # primary stream[0xb8,0xc0), then parameter records and distinct names.
    params = params if params is not None else [('a_position', 0, 0, 4, 1, 0)]
    blob = bytearray(0xc0 + 16 * len(params))
    blob[:6] = b'GXP\0\1\4'
    def word(offset, value):
        struct.pack_into('<I', blob, offset, value)
    word(0x14, int(stage == 'fragment'))
    word(0x24, len(params)); word(0x28, 0xc0 - 0x28)
    word(0x2c, 0x98 - 0x2c); word(0x3c, 1); word(0x40, 0xb8 - 0x40)
    word(0x48, 0xb4 - 0x48); word(0x4c, 0xb4 - 0x4c)
    for index, (name, category, kind, components, count, resource) in enumerate(params):
        off = 0xc0 + index * 16
        struct.pack_into('<i', blob, off, len(blob) - off)
        blob[off + 4] = category | kind << 4
        blob[off + 5] = components
        word(off + 8, count); word(off + 12, resource)
        blob.extend(name.encode() + b'\0')
    word(8, len(blob)); blob.extend(b'\0' * (-len(blob) % 4))
    return blob


class GxpValidation(unittest.TestCase):
    def rejected(self, blob, code=None, stage=None):
        with self.assertRaises(GXP.GxpError) as caught:
            GXP.parse_gxp(blob, stage)
        if code:
            self.assertIn(code, str(caught.exception))

    def test_authored_vertex(self):
        result = GXP.parse_gxp(fixture(), 'vertex')
        self.assertEqual(result['stage'], 'vertex')
        self.assertEqual(result['parameters'][0], dict(name='a_position', category='attribute',
            type='f32', components=4, array_size=1, resource_index=0, container_index=0,
            semantic=0, semantic_index=0))

    def test_sampler(self):
        result = GXP.parse_gxp(fixture('fragment', [('tex', 2, 0, 4, 1, 0)]), 'fragment')
        self.assertEqual(result['parameters'][0]['category'], 'sampler')

    def test_truncation_size_and_padding(self):
        original = fixture()
        for length in (0, 4, 0x97, len(original) - 4):
            self.rejected(original[:length])
        for declared in (0, 0x97, len(original) + 1, 0xffffffff):
            b = fixture(); struct.pack_into('<I', b, 8, declared); self.rejected(b)
        self.rejected(original + b'\0' * 4, 'physical_padding')
        self.rejected(bytes(GXP.MAX_SIZE + 1), 'size_limit')
        self.rejected(memoryview(bytes(GXP.MAX_SIZE + 4)).cast('I'), 'size_limit')

    def test_magic_revision_stage(self):
        b = fixture(); b[0] = 0; self.rejected(b, 'magic')
        for value in (0, 2, 255):
            b = fixture(); b[4] = value; self.rejected(b, 'revision')
        b = fixture(); b[5] = 6; self.rejected(b, 'revision')
        b = fixture(); b[0x14] = 1; self.rejected(b, 'stage_mismatch', 'vertex')

    def test_code_table_extents(self):
        for off in (0x28, 0x2c, 0x40, 0x48, 0x4c, 0x68, 0x74, 0x94):
            b = fixture(); struct.pack_into('<I', b, off, 0xffffffff); self.rejected(b)
        for off in (0x24, 0x3c, 0x44, 0x70, 0x90):
            b = fixture(); struct.pack_into('<I', b, off, 0xffffffff); self.rejected(b)
        b = fixture(); struct.pack_into('<I', b, 0x40, 0x98 - 0x40)
        self.rejected(b, 'overlapping_regions')
        b = fixture(); struct.pack_into('<I', b, 0x28, 0xc1 - 0x28)
        self.rejected(b, 'parameters_alignment')

    def test_names(self):
        for value in (0x80000000, 0x7fffffff, -0xc0):
            b = fixture(); struct.pack_into('<i', b, 0xc0, value if value < 0x80000000 else value - 0x100000000)
            self.rejected(b, 'name')
        b = fixture(); logical = struct.unpack_from('<I', b, 8)[0]; b[logical - 1] = 65
        self.rejected(b, 'termination')
        self.rejected(fixture(params=[('a', 0, 0, 4, 1, 0), ('a', 0, 0, 4, 1, 4)]), 'duplicate')
        b = fixture(); struct.pack_into('<i', b, 0xc0, -8); b[0xb8:0xbb] = b'xx\0'
        self.rejected(b, 'overlapping_regions')

    def test_literals_and_containers(self):
        b = fixture(params=[])
        # Distinct literal scalar registers and disjoint container records.
        b.extend(struct.pack('<4I', 0, 0x3f800000, 1, 0))
        b.extend(struct.pack('<8H', 14, 0, 0, 4, 19, 0, 4, 2))
        struct.pack_into('<I', b, 8, len(b))
        struct.pack_into('<I', b, 0x70, 2)
        struct.pack_into('<I', b, 0x74, 0xc0 - 0x74)
        struct.pack_into('<I', b, 0x90, 2)
        struct.pack_into('<I', b, 0x94, 0xd0 - 0x94)
        self.assertEqual(len(GXP.parse_gxp(b)['containers']), 2)
        duplicate = bytearray(b); struct.pack_into('<H', duplicate, 0xd8, 14)
        self.rejected(duplicate, 'container_fields')
        duplicate = bytearray(b); struct.pack_into('<I', duplicate, 0xc8, 0)
        self.rejected(duplicate, 'literal_resource')

    def test_auxiliary_overlap_and_bounds(self):
        # Reuse bytes already reserved for the authored parameter table to prove
        # a second independently in-bounds table is not silently accepted.
        b = fixture(); struct.pack_into('<I', b, 0x90, 1)
        struct.pack_into('<I', b, 0x94, 0xc0 - 0x94)
        self.rejected(b)
        b = fixture(); struct.pack_into('<I', b, 0x70, 1)
        struct.pack_into('<I', b, 0x74, 0xb8 - 0x74)
        self.rejected(b)
        # Sampler-query pointer exists only in the v1.5 header.
        b = fixture(); b[5] = 5
        struct.pack_into('<I', b, 0x2c, 0x9c - 0x2c)
        struct.pack_into('<I', b, 0x98, 0xffffffff)
        self.rejected(b, 'sampler_query_bounds')

    def test_parameter_fields(self):
        for category, kind, components, count, resource in (
                (3, 0, 4, 1, 0), (4, 0, 4, 1, 0), (0, 9, 4, 1, 0),
                (0, 0, 0, 1, 0), (0, 0, 5, 1, 0), (0, 0, 4, 0, 0),
                (0, 0, 4, 0xffffffff, 0), (0, 0, 4, 1, 65536),
                (2, 0, 4, 2, 0), (2, 0, 4, 1, 16)):
            self.rejected(fixture(params=[('param', category, kind, components, count, resource)]))
        self.rejected(fixture(params=[('a', 0, 0, 4, 1, 0), ('b', 0, 0, 4, 1, 2)]), 'resources')
        for off in (0x78, 0x80, 0x88):
            b = fixture(); struct.pack_into('<I', b, off, 1); self.rejected(b, 'unsupported')


if __name__ == '__main__':
    unittest.main()
