"""The complete native scene leaves the old core polygon list empty."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
class NativeCoreEmptyTests(unittest.TestCase):
    def test_headers_and_follow_state(self):
        source = (ROOT/'src/fzx.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            def compile_run(cc, arm, text, stream=0):
                unit = tmp/'core.c';unit.write_text(text)
                exe = tmp/'check'
                build = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', '-DSH2Z_FORCE_PORTABLE=1', '-DFZX_SIM=2',
                    '-DFZX_SOURCE_CAMERA=1', '-DFZX_SOURCE_MACHINE=1', '-DFZX_SOURCE_ROAD_TILES=1',
                    '-DFZX_NATIVE_CORE_EMPTY='+str(arm), '-DFZX_SOURCE_ROAD_STREAM='+str(stream), '-I'+str(ROOT/'include'),
                    '-I'+str(ROOT/'src'), '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(unit),str(ROOT/'src/fzx_view.c'),str(ROOT/'tests/native_core_empty_driver.c'),
                    *map(str,(ROOT/'third_party/SH2ZAM/saturn/src').glob('*.c')),
                    '-lm','-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(build.returncode,0,build.stderr)
                return subprocess.run([str(exe)],capture_output=True)
            results=[]
            for cc in ('gcc','clang'):
                for arm in (0,1):
                    run=compile_run(cc,arm,source)
                    self.assertEqual(run.returncode,0,run.stderr.decode())
                    self.assertEqual(len(run.stdout),1024*20)
                    results.append(run.stdout)
            for cc in ('gcc','clang'):
                run=compile_run(cc,1,source,1)
                self.assertEqual(run.returncode,0,run.stderr.decode())
                results.append(run.stdout)
            self.assertTrue(all(x==results[0] for x in results))
            changed=source.replace('    frame.count=0;\n    frame.speed=pose_valid?',
                                   '    frame.count=1;\n    frame.speed=pose_valid?',1)
            self.assertNotEqual(changed,source)
            self.assertNotEqual(compile_run('gcc',1,changed).returncode,0)


if __name__ == '__main__':
    unittest.main()
