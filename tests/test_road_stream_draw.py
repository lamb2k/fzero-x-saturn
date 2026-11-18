"""Bounded C3 command array; frozen renderer remains the HOST image oracle."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_road_bake import fixture
from test_road_tiles import synthetic
import road_bake
import road_pack
import road_tiles

ROOT=Path(__file__).resolve().parents[1]
LIB=Path(os.environ.get('FZX_TEST_SH2ZAM_ROOT',ROOT/'third_party/SH2ZAM'))


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'POSIX GCC/Clang required')
class RoadStreamDrawTests(unittest.TestCase):
    def test_commands_reference_edges_and_controls(self):
        self.assertIn('sh2z_view_clip_guard_divu',(LIB/'saturn/include/sh2zam/view_experimental.h').read_text())
        source=(ROOT/'src/fzx_road_stream_draw.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            def assets(shift=0):
                mesh=fixture();_,course=synthetic()
                for v in mesh['vertices']:v[4]+=shift
                compact,textures,_=road_bake.build(mesh,bytes(32),[252,192,253])
                original,info=road_pack.pack(mesh,course,True)
                old,_,_=road_tiles.convert(mesh,info['hash'],[252,192,253])
                paths=[]
                for name,data in [('compact',compact),('textures',textures),('original',original),('old',old)]:
                    path=directory/name;path.write_bytes(data);paths.append(str(path))
                return paths
            def compile(cc,text):
                unit=directory/'draw.c';unit.write_text(text);exe=directory/'gate'
                old_object=directory/'frozen.o'
                reference=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),'-c',str(ROOT/'src/fzx_road_tiles.c'),
                    '-o',str(old_object)],capture_output=True,text=True)
                self.assertEqual(reference.returncode,0,reference.stdout+reference.stderr)
                args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-DSH2Z_FORCE_PORTABLE=1',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),'-I'+str(LIB/'saturn/include'),str(unit),
                    *[str(ROOT/p) for p in ('src/fzx_road_stream.c','src/fzx_road_pack.c','src/fzx_view.c',
                        'src/fzx_view16.c','src/fzx_view16_center.c','tests/road_stream_draw_driver.c')],
                    str(old_object),str(LIB/'saturn/src/sh2zam_view.c'),
                    str(LIB/'saturn/src/sh2zam_view_round.c'),
                    str(LIB/'saturn/src/sh2zam_view_guard_src.c'),
                    str(LIB/'saturn/src/sh2zam_view_center.c'),
                    str(LIB/'saturn/src/sh2zam_view_guard_src_center.c'),
                    str(LIB/'saturn/src/sh2zam_view_merge_tier.c'),
                    str(LIB/'saturn/src/sh2zam_shade.c'),str(LIB/'saturn/src/sh2zam.c'),str(LIB/'saturn/src/sh2zam_shade_fused.c'),str(LIB/'saturn/src/sh2zam_view_rails.c'),'-lm','-o',str(exe)]
                result=subprocess.run(args,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return exe
            captures=[]
            for cc in ('gcc','clang'):
                exe=compile(cc,source);paths=assets();capture=directory/'capture'
                result=subprocess.run([str(exe),*paths,'regular',str(capture)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                captures.append(capture.read_bytes());print(result.stdout.strip())
                paths=assets(32)
                result=subprocess.run([str(exe),*paths,'shifted',str(capture)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(*captures)
            paths=assets()
            for before,after in [
                ('unit=period*32','unit=period*16'),
                ('d->stats->commands>=d->capacity','d->stats->commands>d->capacity'),
                ('sh2z_shade_ramp_offset(&w->ramp_offset,key,key+3,','sh2z_shade_ramp_offset(&w->ramp_offset,key,key,')]:
                self.assertEqual(source.count(before),1)
                altered=source.replace(before,after)
                exe=compile('gcc',altered)
                result=subprocess.run([str(exe),*paths,'regular',str(directory/'control')],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0,'control must fail: '+before)


if __name__=='__main__':unittest.main()
