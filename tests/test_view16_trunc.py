"""Opt-in integral-centre adapter: frozen source oracle and negative controls."""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'), 'GCC/Clang required')
class View16TruncTests(unittest.TestCase):
    def test_exact_projection_and_controls(self):
        adapter = (ROOT/'src/fzx_view16.c').read_text()
        driver = (ROOT/'tests/view16_trunc_driver.c').read_text()
        center_adapter = (ROOT/'src/fzx_view16_center.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)

            def run(text, test, cc, center_text=center_adapter):
                (d/'adapter.c').write_text(text)
                (d/'center.c').write_text(center_text)
                (d/'driver.c').write_text(test)
                command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wconversion', '-Wshadow', '-Wpedantic', '-DSH2Z_FORCE_PORTABLE=1',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS', '')),
                    '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                    '-I'+str(ROOT/'include'), '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(d/'adapter.c'), str(d/'center.c'), str(ROOT/'src/fzx_view.c'),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view_round.c'),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view_center.c'),
                    str(d/'driver.c'), '-o', str(d/'test')]
                built = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                return subprocess.run([str(d/'test')], capture_output=True, text=True)

            for cc in ('gcc', 'clang'):
                got = run(adapter, driver, cc)
                self.assertEqual(got.returncode, 0, got.stderr)
                self.assertIn('9807 exact source-projection samples pass', got.stdout)
                self.assertIn('85050 exact fractional source-projection samples pass', got.stdout)

            rounded = center_adapter.replace('view->center[1]);',
                '(view->center[1]/65536)*65536);')
            self.assertNotEqual(rounded, center_adapter)
            got = run(adapter, driver, 'gcc', rounded)
            self.assertNotEqual(got.returncode, 0, 'rounded centre unexpectedly passed')
            self.assertIn('Assertion', got.stderr)

            # Mutate only the integral adapter. The original fractional-centre API
            # and its existing test remain independent and unchanged.
            prefix, marker, tail = adapter.partition('bool fzx_view16_from_source_trunc(')
            self.assertTrue(marker)
            biased, count = re.subn(
                r'next\.center_y\s*=\s*\(int16_t\)\(view->center\[1\]/65536\);',
                'next.center_y=(int16_t)(view->center[1]/65536-1);', tail)
            self.assertEqual(count, 1)
            fractional, count = re.subn(r'view->center\[([01])\]\s*%\s*65536',
                                       '0', tail)
            self.assertEqual(count, 2)
            old_round = driver.replace('sh2z_view_batch_trunc', 'sh2z_view_batch')
            old_round = old_round.replace('sh2z_view_project_trunc', 'sh2z_view_project')
            controls = [('old-rounding', adapter, old_round),
                        ('wrong-Y-bias', prefix+marker+biased, driver),
                        ('fractional-acceptance', prefix+marker+fractional, driver)]
            for name, text, test in controls:
                with self.subTest(control=name):
                    got = run(text, test, 'gcc')
                    self.assertNotEqual(got.returncode, 0, name+' unexpectedly passed')
                    self.assertIn('Assertion', got.stderr)


if __name__ == '__main__':
    unittest.main()
