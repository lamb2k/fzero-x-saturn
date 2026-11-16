"""Native machine arithmetic against source material equations and known poses."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC and Clang required')
class MachineTests(unittest.TestCase):
    def test_projection_lighting_controls(self):
        source=(ROOT/'src/fzx_machine.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp)
            def run(text,compiler):
                (d/'machine.c').write_text(text)
                flags=shlex.split(os.environ.get('FZX_TEST_CFLAGS',''))
                subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                                *flags,'-I'+str(ROOT/'include'),str(d/'machine.c'),str(ROOT/'src/fzx_material.c'),
                                str(ROOT/'tests/machine_driver.c'),'-o',str(d/'gate')],check=True,capture_output=True)
                return subprocess.run([str(d/'gate')],capture_output=True)
            for compiler in ('gcc','clang'):
                r=run(source,compiler);self.assertEqual(r.returncode,0,r.stderr)
            for label,mutation in [
                ('reflection',source.replace('65535u-shade.a','65535u')),
                ('rotation',source.replace('pose->rotation[j][k]','pose->rotation[k][j]'))]:
                with self.subTest(control=label):
                    self.assertNotEqual(source,mutation)
                    self.assertNotEqual(run(mutation,'gcc').returncode,0)

if __name__=='__main__':unittest.main()
