"""Synthetic road raster coverage, perspective interpolation and range controls."""
from pathlib import Path
import os,shlex,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class RoadDrawTests(unittest.TestCase):
    def test_chart_gate(self):
        source=(ROOT/'src/fzx_road_draw.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            def run(text,cc):
                (folder/'fzx_road_draw.c').write_text(text)
                exe=folder/'gate'
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                    '-Wshadow','-Wpedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(folder),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),
                    str(ROOT/'tests/road_draw_driver.c'),
                    *[str(ROOT/'src'/n) for n in ('fzx_view.c','fzx_road_pack.c','fzx_road_material.c','fzx_material.c')],
                    '-o',str(exe)],check=True,capture_output=True)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            def check(result):
                self.assertEqual(result.returncode,0,result.stderr)
                pixels=[int(x,16) for x in result.stdout.split()]
                self.assertEqual(len(pixels),576)
                for y in range(24):
                    for x in range(24):
                        actual=pixels[y*24+x]
                        # Analytic right-triangle coverage; the descending diagonal is excluded.
                        if x+y>=23:self.assertEqual(actual,0);continue
                        self.assertTrue(actual&0x8000)
                        b=(y+.5)/24;c=(x+.5)/24;a=1-b-c
                        shade=(b*65535/2+c*32768/4)/(a+b/2+c/4)
                        expected=int((shade+128)/257)//8
                        # Q8 plane stepping and 5-bit rounding can straddle one bin.
                        self.assertLessEqual(abs((actual&31)-expected),1)
                        self.assertEqual(actual&0x7fe0,0)
            for cc in ('gcc','clang'):check(run(source,cc))
            controls=[source.replace('(a->x==b->x && a->y==b->y) || top_left(a,b)','top_left(a,b)'),
                      source.replace('minimum_depth*65536/in->p[2]','minimum_depth*0+65536'),
                      source.replace('!plane_span_valid(row[i],sx[i],sy[i],width,height)',
                                     '(plane_span_valid(row[i],sx[i],sy[i],width,height) && false)')]
            for i,mutant in enumerate(controls):
                self.assertNotEqual(mutant,source)
                with self.subTest(control=i),self.assertRaises(AssertionError):check(run(mutant,'gcc'))
if __name__=='__main__':unittest.main()
