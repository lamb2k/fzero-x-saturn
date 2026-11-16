"""Source-camera projection, crop, clipping and precision gates (synthetic)."""
from pathlib import Path
import os,shlex,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class ViewTests(unittest.TestCase):
    def test_source_projection(self):
        source=(ROOT/'src/fzx_view.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp)
            def run(text,compiler):
                (d/'view.c').write_text(text)
                subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),'-I'+str(ROOT/'include'),str(d/'view.c'),
                    str(ROOT/'tests/view_driver.c'),'-lm','-o',str(d/'gate')],check=True,capture_output=True)
                return subprocess.run([str(d/'gate')],capture_output=True)
            for compiler in ('gcc','clang'):
                result=run(source,compiler);self.assertEqual(result.returncode,0,result.stderr)
            for old,new in [('v[col][row]','v[row][col]'),('vp[3]-8.0f','vp[3]'),('vp[1]*p[1][1]','vp[0]*p[0][0]')]:
                mutant=source.replace(old,new);self.assertNotEqual(mutant,source)
                self.assertNotEqual(run(mutant,'gcc').returncode,0)
            mutant=source.replace('if(!inward(v,q,plane))return -1;','/* mutant: omit plane correction */')
            # Keep the helper referenced so -Werror tests execution, not a warning.
            mutant=mutant.replace('/* mutant: omit plane correction */','if(plane==99 && !inward(v,q,plane))return -1;')
            self.assertNotEqual(run(mutant,'gcc').returncode,0)
    def test_render_integration(self):
        with tempfile.TemporaryDirectory() as temp:
            binary=str(Path(temp)/'render')
            core=(ROOT/'src/fzx.c').read_text()
            driver=(ROOT/'tests/view_render_driver.c').read_text().replace('../src/fzx.c',str(Path(temp)/'core.c'))
            (Path(temp)/'driver.c').write_text(driver)
            def run(text,compiler):
                (Path(temp)/'core.c').write_text(text)
                subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),'-DFZX_SOURCE_CAMERA=1','-DSH2Z_FORCE_PORTABLE=1',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(Path(temp)/'driver.c'),str(ROOT/'src/fzx_view.c'),
                    *map(str,(ROOT/'third_party/SH2ZAM/saturn/src').glob('*.c')),'-lm','-o',binary],check=True,capture_output=True)
                return subprocess.run([binary],capture_output=True)
            for compiler in ('gcc','clang'):
                result=run(core,compiler)
                self.assertEqual(result.returncode,0,result.stderr)
            mutant=core.replace('if(source_view_valid)visible=ring_count;',
                                'if(source_view_valid)visible=ring_count<VISIBLE_RINGS?ring_count:VISIBLE_RINGS;')
            self.assertNotEqual(mutant,core)
            self.assertNotEqual(run(mutant,'gcc').returncode,0)
if __name__=='__main__':unittest.main()
