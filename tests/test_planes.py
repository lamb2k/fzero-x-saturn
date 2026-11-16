from pathlib import Path
import os,shlex,shutil,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import plane_assets

class PlaneAtlasTests(unittest.TestCase):
    def test_original_pixels_subtiles_fog_and_alpha(self):
        pixels=[0x8000|((x+y*7)&31) for y in range(32) for x in range(64)]
        data,fog,mean=plane_assets.atlas(struct.pack('>2048H',*pixels),bytes([0xff])*2048,(255,0,255))
        self.assertEqual(len(data),262144);self.assertEqual(data[:4096],struct.pack('>2048H',*pixels))
        for level in range(4):
            width,height=64>>level,32>>level;at=level*4096
            for ty in range(1<<level):
                for tx in range(1<<level):
                    expected=[pixels[(ty*height+y)*64+tx*width+x] for y in range(height) for x in range(width)]
                    size=width*height*2;self.assertEqual(data[at:at+size],struct.pack('>%dH'%len(expected),*expected));at+=size
        self.assertEqual(data[7*16384:7*16384+4096],struct.pack('>2048H',*([0xfc1f]*2048)))
        self.assertEqual(data[131072:131072+4096],b'\xff\xff'*2048)
        self.assertEqual(data[131072+7*16384:],bytes(16384))
        with self.assertRaises(plane_assets.ArtError):plane_assets.atlas(bytes(4096),bytes(2048),(255,0,255))

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class PlaneDrawTests(unittest.TestCase):
    def test_draw_and_controls(self):
        source=(ROOT/'src/fzx_planes.c').read_text(encoding='utf-8')
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            def run(code,compiler):
                (p/'planes.c').write_text(code,encoding='utf-8')
                result=subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),
                    str(p/'planes.c'),str(ROOT/'src/fzx_view.c'),str(ROOT/'tests/planes_driver.c'),'-o',str(p/'gate')],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode());return subprocess.run([str(p/'gate')],capture_output=True)
            output=[]
            for compiler in ('gcc','clang','gcc'):
                result=run(source,compiler);self.assertEqual(result.returncode,0,result.stderr.decode());output.append(result.stdout)
            self.assertEqual(output[0],output[1]);self.assertEqual(output[0],output[2])
            mutant=source.replace('p->layer*FZX_PLANE_LAYER_BYTES+','0*FZX_PLANE_LAYER_BYTES+')
            self.assertNotEqual(mutant,source);self.assertNotEqual(run(mutant,'gcc').returncode,0)
            mutant=source.replace('if(n<0)return fault(p,4);','if(n<0 || n>4)return fault(p,4);')
            self.assertNotEqual(mutant,source);self.assertNotEqual(run(mutant,'gcc').returncode,0)
            mutant=source.replace('p->ds[j]=s;p->dt[j]=t;',
                'if(s<INT32_MIN || s>INT32_MAX || t<INT32_MIN || t>INT32_MAX)return fault(p,1);\n        p->ds[j]=s;p->dt[j]=t;')
            self.assertNotEqual(mutant,source);self.assertNotEqual(run(mutant,'gcc').returncode,0)
            print(output[0].decode().strip())

if __name__=='__main__':unittest.main()
