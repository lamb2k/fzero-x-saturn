"""Point-chart source colors, opaque indices and UV corruption control."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_chart_bake import HEADER

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('gcc'),'GCC required')
class IndexedChartTests(unittest.TestCase):
    def test_source_colors_and_control(self):
        header=HEADER.replace('FZX_MODEL_VIEW_COUNT 1u','FZX_MODEL_VIEW_COUNT 8u')
        record='{vertices,triangles,materials,3,1,1}'
        header=header.replace(record,','.join([record]*8))
        original=(ROOT/'tools/palette_probe/bake.c').read_text().replace(
            '"../vdp1_probe/bake.c"','"'+(ROOT/'tools/vdp1_probe/bake.c').as_posix()+'"')
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);(d/'model_probe.h').write_text(header)
            def run(source):
                (d/'bake.c').write_text(source)
                subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(d),
                                '-I'+str(ROOT/'include'),str(d/'bake.c'),str(ROOT/'src/fzx_material.c'),
                                '-lm','-o',str(d/'gate')],check=True,capture_output=True)
                return subprocess.run([str(d/'gate')],capture_output=True)
            def check_corners(result):
                self.assertEqual(result.returncode,0,result.stderr)
                data=[int(x) for x in re.findall(rb'\d+',re.search(rb'indices_0_0\[\]=\{(.*?)\};',result.stdout,re.S)[1])]
                colors=[tuple(map(int,x)) for x in re.findall(rb'\{(\d+),(\d+),(\d+),65535\}',
                    re.search(rb'base_0_0\[\]=\{(.*?)\};',result.stdout,re.S)[1])]
                self.assertEqual(set(colors),{(0,0,0),(65535,65535,65535)})
                self.assertEqual(len(data),16) # width 8, height 4, two indices/byte
                self.assertEqual(colors[data[0]>>4],(0,0,0))
                self.assertEqual(colors[data[3]&15],(65535,65535,65535))
                self.assertEqual(colors[data[-1]&15],(65535,65535,65535))
            check_corners(run(original))
            with self.assertRaises(AssertionError):
                check_corners(run(original.replace('value+=128;','value+=384;')))
            (d/'model_probe.h').write_text(header.replace('FZX_TEX_I4,2,2,0,0,0,0','FZX_TEX_I4,2,2,0,0,1,0'))
            self.assertEqual(run(original).returncode,3)

if __name__=='__main__':unittest.main()
