from pathlib import Path
import os,shlex,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class TextureCdTests(unittest.TestCase):
    def test_transfer_and_rejections(self):
        source=(ROOT/'saturn/texture_cd.c').read_text()
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            def run(code,compiler):
                (p/'loader.c').write_text(code)
                result=subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(ROOT/'tests/texture_cd'),'-I'+str(ROOT/'saturn'),
                    str(p/'loader.c'),str(ROOT/'tests/texture_cd_driver.c'),'-o',str(p/'gate')],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                return subprocess.run([str(p/'gate')],capture_output=True)
            outputs=[]
            for compiler in ('gcc','clang','gcc'):
                result=run(source,compiler)
                self.assertEqual(result.returncode,0,result.stderr.decode());outputs.append(result.stdout)
            self.assertEqual(outputs[0],outputs[1]);self.assertEqual(outputs[0],outputs[2])
            mutant=source.replace('if(hash!=expected)','if(hash==expected)')
            self.assertNotEqual(mutant,source);self.assertNotEqual(run(mutant,'gcc').returncode,0)
            print(outputs[0].decode().strip())

if __name__=='__main__':unittest.main()
