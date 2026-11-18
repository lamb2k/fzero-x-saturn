"""Synthetic prepared native vehicle renderer and corruption controls."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from machine_rebake import build
from model_probe.make_fixture import fixture_data


@unittest.skipIf(os.name=='nt','Use Linux host compilers')
class VehicleDrawTests(unittest.TestCase):
    def test_renderer_and_controls(self):
        args=list(fixture_data());args[0][0]['materials'][0]['environment']='racer.bodyRGB'
        meta,_,_=build(*args,fixture=True)
        source=(ROOT/'src/fzx_vehicle_draw.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);pack=directory/'fixture.pak';pack.write_bytes(meta)
            def run(cc,text,extra=(),driver=None):
                path=directory/'vehicle_draw.c';path.write_text(text);exe=directory/'gate'
                driver_path=ROOT/'tests/vehicle_draw_driver.c'
                if driver is not None:driver_path=directory/'vehicle_draw_driver.c';driver_path.write_text(driver)
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-DSH2Z_FORCE_PORTABLE=1',*extra,
                    '-DFZX_SOURCE_MACHINE=1','-DFZX_SOURCE_CAMERA=1',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),'-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(path),str(ROOT/'src/fzx_vehicle.c'),str(ROOT/'src/fzx_view16.c'),str(ROOT/'src/fzx_view.c'),
                    *map(str,(ROOT/'third_party/SH2ZAM/saturn/src').glob('*.c')),
                    str(driver_path),'-lm','-o',str(exe)]
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return subprocess.run([str(exe),str(pack)],capture_output=True,text=True)
            results=[]
            for cc in ('gcc','clang'):
                result=run(cc,source);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                results.append(result.stdout)
            self.assertEqual(*results)
            # The default-off FAST arm must emit the same quads on both compilers.
            for cc in ('gcc','clang'):
                result=run(cc,source,('-DFZX_VEHICLE_FAST=1',));self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual(result.stdout,results[0])
            # Control: a wrong blend weight in the FAST Gouraud path must change the quads.
            before='const unsigned weight=255u-light[3];'
            self.assertEqual(source.count(before),1)
            result=run('gcc',source.replace(before,'const unsigned weight=light[3];'),('-DFZX_VEHICLE_FAST=1',))
            self.assertNotEqual(result.stdout,results[0])
            # The approximate Saturn-native arm (default off) must build cleanly under the same strict
            # flags and pass every synthetic scene check without sanitizer faults: exit status 0 and the
            # PASS line. Its quads intentionally differ from the exact arms.
            def passed(result):
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertNotIn('runtime error',result.stderr)
                self.assertIn(' PASS quads=',result.stdout)
            for cc in ('gcc','clang'):
                passed(run(cc,source,('-DFZX_VEHICLE_NATIVE=1',)))
            # Negative control: a driver whose scene check fails must fail the native gate.
            driver=(ROOT/'tests/vehicle_draw_driver.c').read_text()
            check='stats.draws!=8||emits!=8||'
            self.assertEqual(driver.count(check),1)
            result=run('gcc',source,('-DFZX_VEHICLE_NATIVE=1',),driver.replace(check,'stats.draws!=9||emits!=8||'))
            self.assertNotEqual(result.returncode,0)
            with self.assertRaises(AssertionError):passed(result)
            for before,after in [
                ('if(full!=expected||self!=u32(p+20))return false;','if(false)return false;'),
                ('work->palette_valid&&(!p[6]||!changed)','work->palette_valid'),
                ('stats->draws>=capacity','stats->draws>capacity'),
                ('q.color=solid_color(work->representative[q.palette],average,state->fog);',
                 'q.color=work->palette[q.palette][0];(void)solid_color;(void)average;')]:
                self.assertEqual(source.count(before),1)
                # Keep strict unused-variable diagnostics meaningful in controls.
                altered=source.replace(before,after)
                if before.startswith('if(full'):altered=altered.replace('if(false)return false;','(void)expected;if(false)return false;')
                if 'palette_valid&&' in before:altered=altered.replace('if(work->palette_valid)continue;','(void)changed;if(work->palette_valid)continue;')
                result=run('gcc',altered)
                self.assertNotEqual(result.returncode,0)
            print(results[0].strip())


if __name__=='__main__':unittest.main()
