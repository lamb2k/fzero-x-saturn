"""Synthetic native-adapter API recording; no SDK, hardware or retail assets."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
from test_countdown_pack import fixture, build

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
class CountdownNativeTests(unittest.TestCase):
    def test_adapter_and_failure_controls(self):
        metadata,_,_=build(*fixture())
        source=(ROOT/'saturn/countdown_draw_native.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            path=Path(td);pack=path/'pack';pack.write_bytes(metadata)
            def compile(text,cc):
                unit=path/'native.c';unit.write_text(text);exe=path/'native'
                result=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-Wconversion','-Wshadow','-Wpedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-DSH2Z_FORCE_PORTABLE=1','-I'+str(ROOT/'tests/countdown_native_stub'),
                    '-I'+str(ROOT/'saturn'),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),
                    '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),str(unit),
                    *[str(ROOT/p)for p in ('src/fzx_countdown_draw.c','src/fzx_countdown_pack.c','src/fzx_countdown.c',
                        'src/fzx_view16.c','src/fzx_view.c','tests/countdown_native_driver.c',
                        'third_party/SH2ZAM/saturn/src/sh2zam_view.c')],'-lm','-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return exe
            def run(exe):
                return subprocess.run([str(exe),str(pack),str(path/'capture')],capture_output=True,text=True)
            captures=[]
            for cc in ('gcc','clang'):
                result=run(compile(source,cc));self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                captures.append((path/'capture').read_bytes());print(result.stdout.strip())
            self.assertEqual(*captures)
            for old,new in (
                ('.trans_pixel_disable=0','.trans_pixel_disable=1'),
                ('base+q->texture','base'),
                ('*out=q->solid?solid:texture;','*out=texture;'),
                ('prepared>capacity-1','prepared>capacity')):
                self.assertEqual(source.count(old),1)
                result=run(compile(source.replace(old,new),'gcc'))
                self.assertNotEqual(result.returncode,0,'control passed: '+old)


if __name__=='__main__':
    unittest.main()
