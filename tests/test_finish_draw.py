"""ROM-free FINISH projection/culling tests with synthetic geometry."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from test_finish_pack import fixture
from finish_pack import build
from road_pack import fnv
from results_source import matrix_host_support,matrix_bridge,MATH


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'), 'Linux compilers required')
@unittest.skipUnless(MATH.is_file(),'pinned original matrix source required')
class FinishDrawTests(unittest.TestCase):
    def test_projection_order_and_controls(self):
        model,files=fixture()
        for i,v in enumerate(model['model']['vertices']):
            v[0]=i-128;v[1]=(i-128)*(i-128)//128-64;v[2]=0
        metadata,texture,_=build(model,files)
        with tempfile.TemporaryDirectory(prefix='fzx-finish-draw-')as td:
            tmp=Path(td);(tmp/'pack').write_bytes(metadata)
            (tmp/'matrix.c').write_bytes(matrix_host_support()+matrix_bridge().replace(b'#include "global.h"',b''))
            original=(ROOT/'src/fzx_finish_draw.c').read_text();outputs=[]
            for arm,cc in [('gcc','gcc'),('clang','clang'),('wrong-cull','gcc'),('wrong-y','gcc')]:
                text=original
                if arm=='wrong-cull':text=text.replace('if(area>=0)continue;','if(area<=0)continue;')
                if arm=='wrong-y':text=text.replace('v.center_y=111','v.center_y=115')
                (tmp/'draw.c').write_text(text);exe=tmp/arm
                compile=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow',
                    '-Wno-misleading-indentation','-ffp-contract=off','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(ROOT/'tests/finish_draw_driver.c'),str(tmp/'matrix.c'),str(tmp/'draw.c'),
                    str(ROOT/'src/fzx_finish_pack.c'),str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),
                    '-lm','-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(compile.returncode,0,compile.stdout+compile.stderr)
                run=subprocess.run([str(exe),str(tmp/'pack'),str(fnv(metadata)),str(len(texture)),str(fnv(texture))],capture_output=True,text=True)
                if arm in ('gcc','clang'):
                    self.assertEqual(run.returncode,0,run.stdout+run.stderr);outputs.append(run.stdout)
                else:self.assertNotEqual(run.returncode,0,arm+' control survived')
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())


if __name__=='__main__':unittest.main()
