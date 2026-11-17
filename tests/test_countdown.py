"""ROM-free countdown source snapshot and native view adapter controls."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import test_machine_state as machine_test

ROOT=Path(__file__).resolve().parents[1]


class CountdownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name=='nt':raise unittest.SkipTest('Use Linux host compilers')
        machine_test.MachineStateTests.setUpClass()
        cls.flags=machine_test.MachineStateTests.flags

    def test_snapshot_layout_and_nonmutation(self):
        source=(ROOT/'ref/countdown_bridge.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            d=Path(td)
            for cc in ('gcc','clang'):
                for opt in ('-O0','-O2'):
                    exe=d/'snapshot'
                    subprocess.run([cc,*self.flags,opt,'-fsanitize=undefined','-fno-sanitize-recover=all',
                        str(ROOT/'ref/countdown_bridge.c'),str(ROOT/'ref/countdown_probe.c'),
                        '-no-pie','-o',str(exe)],check=True,capture_output=True)
                    subprocess.run([str(exe)],check=True,capture_output=True)
            for before,after in [('out->timer=gRaceIntroTimer;','out->timer=gRaceIntroTimer+1;'),
                                 ('|\n                                (unsigned short)','+1u |\n                                (unsigned short)'),
                                 ('return 1;\n}', 'gRaceIntroTimer--;return 1;\n}')]:
                self.assertEqual(source.count(before),1)
                (d/'bad.c').write_text(source.replace(before,after))
                exe=d/'control'
                subprocess.run(['gcc',*self.flags,'-O2',str(d/'bad.c'),str(ROOT/'ref/countdown_probe.c'),
                                '-no-pie','-o',str(exe)],check=True,capture_output=True)
                self.assertNotEqual(subprocess.run([str(exe)],capture_output=True).returncode,0)

    def test_phase_and_view(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td)
            for cc in ('gcc','clang'):
                exe=d/cc
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                    '-DSH2Z_FORCE_PORTABLE=1','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),
                    '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),str(ROOT/'tests/countdown_driver.c'),
                    str(ROOT/'src/fzx_countdown.c'),str(ROOT/'src/fzx_view16.c'),
                    str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),'-lm','-o',str(exe)]
                build=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(build.returncode,0,build.stdout+build.stderr)
                result=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                print(cc,result.stdout.strip())
            source=(ROOT/'src/fzx_countdown.c').read_text()
            # Each control changes a distinct source contract: event boundary,
            # packed matrix orientation, or camera translation composition.
            for before,after in [('timer>160?1','timer>=160?1'),
                                 ('state->matrix[j*4+k]','state->matrix[k*4+j]'),
                                 ('+(int64_t)view->translation[i]*4','+0')]:
                self.assertEqual(source.count(before),1)
                bad=d/'bad.c';bad.write_text(source.replace(before,after))
                altered=[str(bad) if arg==str(ROOT/'src/fzx_countdown.c') else arg for arg in command]
                subprocess.run(altered,check=True,capture_output=True)
                self.assertNotEqual(subprocess.run([str(exe)],capture_output=True).returncode,0)


if __name__=='__main__':unittest.main()
