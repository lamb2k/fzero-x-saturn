"""C3 road command budget guard (FZX_ROAD_STREAM_BUDGET): HOST checks on the synthetic road."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from test_road_bake import fixture
import road_bake

ROOT=Path(__file__).resolve().parents[1]
LIB=Path(os.environ.get('FZX_TEST_SH2ZAM_ROOT',ROOT/'third_party/SH2ZAM'))
SOURCES=('src/fzx_road_stream.c','src/fzx_road_pack.c','src/fzx_view.c','src/fzx_view16.c','src/fzx_view16_center.c')
KERNELS=('sh2zam_view.c','sh2zam_view_round.c','sh2zam_view_guard_src.c','sh2zam_view_center.c',
    'sh2zam_view_guard_src_center.c','sh2zam_view_merge_tier.c','sh2zam_shade.c','sh2zam.c',
    'sh2zam_shade_fused.c','sh2zam_view_rails.c')


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'POSIX GCC/Clang required')
class RoadStreamBudgetTests(unittest.TestCase):
    def build(self,directory,cc,source,budget=1):
        unit=directory/'draw.c';unit.write_text(source);exe=directory/('budget-'+cc)
        args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
            '-fsanitize=undefined','-fno-sanitize-recover=all','-DSH2Z_FORCE_PORTABLE=1',
            '-DFZX_ROAD_STREAM_BUDGET=%d'%budget,
            '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),'-I'+str(LIB/'saturn/include'),str(unit),
            *[str(ROOT/p) for p in SOURCES],str(ROOT/'tests/road_stream_budget_driver.c'),
            *[str(LIB/'saturn/src'/k) for k in KERNELS],'-lm','-o',str(exe)]
        return subprocess.run(args,capture_output=True,text=True),exe

    def test_guard_passes_and_controls(self):
        source=(ROOT/'src/fzx_road_stream_draw.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            compact,textures,_=road_bake.build(fixture(),bytes(32),[252,192,253])
            (directory/'compact').write_bytes(compact);(directory/'textures').write_bytes(textures)
            paths=[str(directory/'compact'),str(directory/'textures')]
            for cc in ('gcc','clang'):
                result,exe=self.build(directory,cc,source)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                run=subprocess.run([str(exe),*paths],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())
            # The guard entries exist only with the knob.
            result,_=self.build(directory,'gcc',source,budget=0)
            self.assertNotEqual(result.returncode,0)
            # Controls: each must make the driver fail.
            for before,after in [
                ('if(d->skip){--d->skip;++d->dropped;return false;}','if(d->skip){--d->skip;++d->dropped;return true;}'),
                ('++*d->over;return false;}','return true;}'),
                ('flags|FZX_RS_COARSE,commands,emit,budget,count,stats,over,NULL,&dropped','flags,commands,emit,budget,count,stats,over,NULL,&dropped'),
                ('if(front&&(d->flags&FZX_RS_COARSE)){','if(front&&0){')]:
                self.assertEqual(source.count(before),1,before)
                result,exe=self.build(directory,'gcc',source.replace(before,after))
                if result.returncode:continue
                run=subprocess.run([str(exe),*paths],capture_output=True,text=True)
                self.assertNotEqual(run.returncode,0,'control must fail: '+before)


if __name__=='__main__':unittest.main()
