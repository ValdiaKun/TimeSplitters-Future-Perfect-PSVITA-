# SPDX-License-Identifier: GPL-3.0-only
"""Source-pass regressions supplement actual translated x86 boundary fixtures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import sys
import unittest
from tools.guest_lowering import lower_function

ROOT = Path(__file__).resolve().parents[1]

class LoweringTests(unittest.TestCase):
    def test_compound_update_expression_and_hygiene(self):
        emitted = lower_function('''void emitted(void) {
            uint32_t p = 0;
            uint32_t _tsfp_memory_0 = 19;
            MEM32(p++) += 7;
            assert(p == 1 && MEM32(0) == 10);
            MEM32(16) = (MEM32(20) = _tsfp_memory_0);
            assert(MEM32(16) == 19 && MEM32(20) == 19);
            uint32_t old = MEM32(32)++;
            assert(old == 4 && MEM32(32) == 5);
            old = --MEM32(32);
            assert(old == 4 && MEM32(32) == 4);
            SMEM64(63) = -5;
            assert(SMEM64(63) == -5);
            MEMF(80) = -1.25f;
            MEMD(84) = -3.5;
            assert(MEMF(80) == -1.25f && MEMD(84) == -3.5);
        }''')
        source = '''#include "recomp_types.h"
#include "lowered_memory.h"
#include <assert.h>
#include <stdlib.h>
_Noreturn void tsfp_guest_memory_fault(uint32_t a, size_t n) { (void)a; (void)n; abort(); }
''' + emitted + '''
int main(void) { unsigned char storage[128] = {0}; tsfp_guest_bind(storage, sizeof(storage), 0);
tsfp_guest_store_u32(0,3); tsfp_guest_store_u32(32,4); emitted(); return 0; }
'''
        with tempfile.TemporaryDirectory(dir=ROOT / 'build/guest', prefix='lowering-test-') as temporary:
            path = Path(temporary)
            (path / 'test.c').write_text(source)
            subprocess.run(shlex.split(os.environ.get('CC','cc')) + ['-std=c11','-Wall','-Wextra','-Werror',
                '-DTSFP_PORTABLE_GUEST_MEMORY','-Iguest','-Ibuild/guest/include',str(path/'test.c'),
                'guest/memory.c','guest/address_space.c','-lm','-o',str(path/'test')],cwd=ROOT,check=True)
            subprocess.run([str(path/'test')],check=True)

    def test_unsupported_executable_rejected(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from guest_codegen import verify_input
        with tempfile.TemporaryDirectory(dir=ROOT / 'build/guest', prefix='input-test-') as temporary:
            path = Path(temporary) / 'synthetic-invalid.xbe'
            path.write_bytes(b'asset-free-invalid-executable')
            with self.assertRaisesRegex(ValueError, 'Unsupported Xbox executable'):
                verify_input(path)

    def test_unbounded_pointer_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unbounded guest pointer'):
            lower_function('void emitted(void) { helper(XBOX_PTR(ecx)); }')

    def test_opaque_memory_macro_rejected(self):
        with self.assertRaisesRegex(ValueError, 'opaque preprocessor'):
            lower_function('void emitted(void) {\n#define HIDDEN MEM32(ecx)\nhelper(HIDDEN);\n}')

    def test_unrecognized_borrow_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Borrowed guest lvalue'):
            lower_function('void emitted(void) { helper(&MEM32(ecx)); }')

    def test_atomic_remains_atomic(self):
        output = lower_function('void emitted(void) { eax = RECOMP_ATOMIC_ADD32(XBOX_PTR(ecx), 1); }')
        self.assertIn('RECOMP_ATOMIC_ADD32(TSFP_ATOMIC_POINTER', output)
        self.assertNotIn('TSFP_STORE', output)

    def test_directives_and_strings(self):
        output = lower_function('void emitted(void) {\n#define VALUE 3\nMEM32(ecx) = VALUE;\nhelper("/*string*/");\n#undef VALUE\n}')
        self.assertIn('#define VALUE 3',output)
        self.assertIn('"/*string*/"',output)
        self.assertIn('#undef VALUE',output)

if __name__ == '__main__':
    unittest.main()
