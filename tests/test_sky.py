"""Native sky gradient geometry. Authored synthetic content only."""
from pathlib import Path
import os,re,shlex,shutil,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import sky_assets

class SkyAssetTests(unittest.TestCase):
    def test_adaptive_spans_bound_texture_error(self):
        for colors in ([0x8000|i//2|((31-i//2)<<5) for i in range(64)],
                       [0x8000|((i*811)&0x7fff) for i in range(64)]):
            points=sky_assets.breakpoints(colors)
            self.assertEqual((points[0],points[-1]),(0,63))
            for start,stop in zip(points,points[1:]):
                self.assertGreater(stop,start)
                for i in range(start,stop+1):
                    for shift in (0,5,10):
                        actual=((colors[i]>>shift)&31)
                        fitted=(((colors[start]>>shift)&31)*(stop-i)+((colors[stop]>>shift)&31)*(i-start))/(stop-start)
                        self.assertLessEqual(abs(actual-fitted),1.00000001)
        self.assertEqual(sky_assets.breakpoints([0x8000|i//2 for i in range(64)]),[0,63])
        # A single sharp authored feature must survive; the two-endpoint control fails.
        colors=[0x8000]*64;colors[31]|=31
        self.assertIn(31,sky_assets.breakpoints(colors))
        self.assertGreater((colors[31]&31)-(colors[0]&31),1)

    def test_original_sample_order_and_opacity(self):
        values=[0x8000|((i*811)&0x7fff) for i in range(64)]
        data=struct.pack('>64H',*values)
        result=sky_assets.header(data)
        self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]{4})',result)],values)
        self.assertEqual(sky_assets.header(data),result)
        for bad in (data[:-2],data+b'\0\0',b'\0\0'+data[2:]):
            with self.assertRaises(sky_assets.ArtError):sky_assets.header(bad)

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class SkyTests(unittest.TestCase):
    def test_gradient_geometry_and_controls(self):
        original=(ROOT/'src/fzx_sky.c').read_text()
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)
            def run(source,compiler):
                (path/'sky.c').write_text(source)
                result=subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),str(path/'sky.c'),
                    str(ROOT/'src/fzx_view.c'),str(ROOT/'tests/sky_driver.c'),'-lm','-o',str(path/'gate')],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                return subprocess.run([str(path/'gate')],capture_output=True)
            outputs=[]
            for compiler in ('gcc','clang'):
                result=run(original,compiler)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                outputs.append(result.stdout)
            self.assertEqual(outputs[0],outputs[1])
            self.assertEqual(run(original,'gcc').stdout,outputs[0])
            # Independently exercise the coalesced path. Its declared bound is
            # one texture RGB5 unit plus projection/rounding, not exact-path parity.
            adapted=original.replace('vertices,colors,NULL,0,capacity',
                'vertices,colors,(const uint8_t[]){0,63},2,capacity')
            self.assertNotEqual(adapted,original)
            outputs_adapted=[]
            for compiler in ('gcc','clang'):
                # Keep the original fixture gate unchanged; this build opts into
                # the separate approximation allowance explicitly.
                prior=os.environ.get('FZX_TEST_CFLAGS','')
                os.environ['FZX_TEST_CFLAGS']=prior+' -DFZX_TEST_SKY_ADAPTED=1'
                try: result=run(adapted,compiler)
                finally: os.environ['FZX_TEST_CFLAGS']=prior
                self.assertEqual(result.returncode,0,result.stderr.decode())
                outputs_adapted.append(result.stdout)
            self.assertEqual(*outputs_adapted)
            print('adapted '+outputs_adapted[0].decode().strip())
            for old,new in [('vertices[i].uv[0]*2048','(vertices[i].uv[0]+512)*2048'),
                            ('return colors[0];','return colors[63];'),
                            ('{{0,3,1},{0,2,3}}','{{0,3,1},{0,3,1}}'),
                            ('if(!ordered(a,b))','if(!ordered(a,b) && plane==99)')]:
                mutant=original.replace(old,new);self.assertNotEqual(mutant,original)
                self.assertNotEqual(run(mutant,'gcc').returncode,0)
            print(outputs[0].decode().strip())

if __name__=='__main__':unittest.main()
