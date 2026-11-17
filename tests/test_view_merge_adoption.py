"""Synthetic-only checks for the port's bounded merge caller policy."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIB = Path(os.environ.get('FZX_TEST_SH2ZAM_ROOT', ROOT/'third_party/SH2ZAM'))


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'POSIX GCC/Clang required')
class MergeAdoption(unittest.TestCase):
    def test_caller_policy_and_control(self):
        source = (ROOT/'tests/view_merge_adoption.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            def run(cc, text):
                unit = directory/'caller.c'; unit.write_text(text)
                exe = directory/'caller'
                subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wconversion', '-Wshadow', '-Wpedantic', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', '-DSH2Z_FORCE_PORTABLE=1',
                    '-I'+str(LIB/'saturn/include'), str(unit),
                    str(LIB/'saturn/src/sh2zam_view.c'), '-lm', '-o', str(exe)],
                    check=True, capture_output=True)
                return subprocess.run([str(exe)], capture_output=True, text=True)
            results = [run(cc, source) for cc in ('gcc', 'clang')]
            for result in results:
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(results[0].stdout, results[1].stdout)
            print(results[0].stdout, end='')
            for old, new in [('left[4].outcode|=0x80;', 'left[4].outcode|=0;'),
                             ('partial[j-run]%32u==0', 'partial[j-run]%32u!=0')]:
                self.assertEqual(source.count(old), 1)
                self.assertNotEqual(run('gcc', source.replace(old,new)).returncode, 0)


if __name__ == '__main__':
    unittest.main()
