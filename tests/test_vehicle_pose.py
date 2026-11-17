"""Synthetic source-machine pose adapter, portable and SH2ZAM contracts."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipIf(os.name=='nt','Run with Linux GCC and Clang')
class VehiclePoseTests(unittest.TestCase):
    def test_projection_eye_and_controls(self):
        source=(ROOT/'src/fzx_vehicle.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            def run(cc,text):
                path=directory/'vehicle.c';path.write_text(text)
                exe=directory/'gate'
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-DSH2Z_FORCE_PORTABLE=1',
                    '-DFZX_SOURCE_MACHINE=1','-DFZX_SOURCE_CAMERA=1',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),'-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(path),str(ROOT/'src/fzx_view16.c'),str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),
                    str(ROOT/'tests/vehicle_pose_driver.c'),'-lm','-o',str(exe)]
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                result=run(cc,source);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                outputs.append(result.stdout)
            self.assertEqual(*outputs)
            for before,after in [
                ('state->model_pos[i]-camera->eye[i]','state->model_pos[i]'),
                ('state->model_basis[2-j][i]*state->model_scale[2-j]','state->model_basis[j][i]*state->model_scale[j]'),
                ('next.eye[i]=(int32_t)eye;','next.eye[i]=(int32_t)eye+3;')]:
                self.assertEqual(source.count(before),1)
                self.assertNotEqual(run('gcc',source.replace(before,after)).returncode,0)
            print(outputs[0].strip())


if __name__=='__main__':unittest.main()
