"""Synthetic results crops and clipping, with independently checked texel coordinates."""
from pathlib import Path
import os,shlex,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class ResultsDraw(unittest.TestCase):
    def test_crops_controls_and_compilers(self):
        outputs=[]
        with tempfile.TemporaryDirectory() as temp:
            for cc in ('gcc','clang'):
                exe=Path(temp)/cc
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(ROOT/'include'),str(ROOT/'tests/results_draw_driver.c'),
                    str(ROOT/'src/fzx_results_draw.c'),'-o',str(exe)],check=True,capture_output=True)
                outputs.append(subprocess.check_output([str(exe)]))
        self.assertEqual(outputs,[b'UI_CROP_PIXELS_AND_REJECTIONS_PASS\n']*2)

if __name__=='__main__':unittest.main()
