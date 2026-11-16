from pathlib import Path
import os,shlex,shutil,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import skyline_assets

class SkylineAssetTests(unittest.TestCase):
    def test_source_indices_tint_and_fade(self):
        colors=[0,0x801f,0x83e0,0xfc00]+[0]*12
        tex,pal=skyline_assets.convert(bytes([0x11])*2048,struct.pack('>16H',*colors),(200,130,240))
        self.assertEqual(len(tex),2048);self.assertEqual(len(pal),32)
        result=struct.unpack('>16H',pal)
        self.assertEqual(result[1]&31,24)
        self.assertEqual((result[2]>>5)&31,16)
        self.assertEqual((result[3]>>10)&31,29)
        # No new palette indices or colors; original alpha holes stay holes.
        self.assertTrue(all((v>>4) in (0,1) and (v&15) in (0,1) for v in tex))
        for y in range(0,64,4):
            row=tex[y*32:(y+4)*32]
            coverage=sum((v>>4!=0)+(v&15!=0) for v in row)/256
            self.assertLessEqual(abs(coverage-(1-(y+2)/64)),1/16)
        transparent,_=skyline_assets.convert(bytes([0x66])*2048,struct.pack('>16H',*colors),(255,255,255))
        self.assertEqual(transparent,bytes(2048))
        colors[0]=0xffff
        with self.assertRaises(skyline_assets.ArtError):skyline_assets.convert(bytes(2048),struct.pack('>16H',*colors),(255,255,255))
        with self.assertRaises(skyline_assets.ArtError):skyline_assets.convert(bytes(2047),bytes(32),(255,255,255))

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class SkylineRenderTests(unittest.TestCase):
    def test_source_projection_and_controls(self):
        original=(ROOT/'src/fzx_skyline.c').read_text()
        with tempfile.TemporaryDirectory() as temporary:
            p=Path(temporary)
            def run(source,compiler):
                (p/'skyline.c').write_text(source)
                result=subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),str(p/'skyline.c'),
                    str(ROOT/'src/fzx_view.c'),str(ROOT/'tests/skyline_driver.c'),'-lm','-o',str(p/'gate')],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                return subprocess.run([str(p/'gate')],capture_output=True)
            outputs=[]
            for compiler in ('gcc','clang','gcc'):
                result=run(original,compiler)
                self.assertEqual(result.returncode,0,result.stderr.decode());outputs.append(result.stdout)
            self.assertEqual(outputs[0],outputs[1]);self.assertEqual(outputs[0],outputs[2])
            for old,new in [('matrix[12+axis]','matrix[axis]'),('s->sprite_id-1','4-s->sprite_id')]:
                mutant=original.replace(old,new);self.assertNotEqual(mutant,original)
                self.assertNotEqual(run(mutant,'gcc').returncode,0)
            print(outputs[0].decode().strip())

if __name__=='__main__':unittest.main()
