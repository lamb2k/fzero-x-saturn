"""Synthetic view-edges caller checks; also buildable against the old tag."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux GCC/Clang required')
class ViewEdgesAdoption(unittest.TestCase):
    def test_synthetic_contract_and_command_boundary(self):
        outputs=[]
        with tempfile.TemporaryDirectory() as td:
            source=(ROOT/'tests/view_edges_adoption.c').read_text()
            path=Path(td)/'caller.c'
            path.write_text(source)
            def compile_run(cc):
                exe=Path(td)/cc
                subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-DSH2Z_FORCE_PORTABLE=1',
                    '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),str(path),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),'-lm','-o',str(exe)],check=True,capture_output=True)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            for cc in ('gcc','clang'):
                p=compile_run(cc)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0],end='')
            for old,new in [('edge_out[3].x==0','edge_out[3].x==1'),
                            ('CHECK(n==expected[mask] && m==n', 'm=m>5?5:m; CHECK(n==expected[mask] && m==n')]:
                self.assertEqual(source.count(old),1)
                path.write_text(source.replace(old,new))
                self.assertNotEqual(compile_run('gcc').returncode,0)

if __name__=='__main__':unittest.main()
