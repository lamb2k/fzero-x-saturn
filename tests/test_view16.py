"""Source-camera adapter, independent old projection and failure controls."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'), 'GCC/Clang required')
class View16Tests(unittest.TestCase):
    def test_projection_and_controls(self):
        source = (ROOT/'src/fzx_view16.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            def run(text, cc):
                (d/'adapter.c').write_text(text)
                subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wconversion', '-Wshadow', '-Wpedantic', '-DSH2Z_FORCE_PORTABLE=1',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS', '')),
                    '-I'+str(ROOT/'include'), '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(d/'adapter.c'), str(ROOT/'src/fzx_view.c'),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),
                    str(ROOT/'tests/view16_driver.c'), '-lm', '-o', str(d/'test')],
                    check=True, capture_output=True)
                return subprocess.run([str(d/'test')], capture_output=True)
            for cc in ('gcc', 'clang'):
                got = run(source, cc)
                self.assertEqual(got.returncode, 0, got.stderr)
            for old, new in [('(cy-1)', 'cy'), ('next.clip[i]=(int16_t)(view->clip[i]-(i>=2))',
                                              'next.clip[i]=view->clip[i]'),
                             ('const sh2z_vec3_t position={eye[0],eye[1],eye[2]}',
                              'const sh2z_vec3_t position={eye[2],eye[1],eye[0]}')]:
                mutant = source.replace(old, new)
                self.assertNotEqual(mutant, source)
                self.assertNotEqual(run(mutant, 'gcc').returncode, 0)


if __name__ == '__main__':
    unittest.main()
