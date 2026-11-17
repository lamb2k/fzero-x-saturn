"""Synthetic UI/finish VDP1 API recording, limits and failing controls."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux compilers required')
class ResultsNative(unittest.TestCase):
    def test_api_and_controls(self):
        source=(ROOT/'saturn/results_native.c').read_text()
        with tempfile.TemporaryDirectory()as td:
            p=Path(td)
            rows=[]
            for i in range(8):
                palette='{0,65535,0,0,0,0,0,0,0,0,0,0,0,0,0,0}'
                rows.append('{%d,%d,8,4,8,%d,0,%s}'%(i*64,16 if i==1 else 64,i==1,palette))
            (p/'results_identity.h').write_text('#include "fzx_results_draw.h"\n#define FZX_RESULTS_UI_BYTES 512u\n#define FZX_RESULTS_UI_IMAGES 8u\n#define FZX_RESULTS_FONT3_GLYPHS 1u\n#define FZX_RESULTS_TINT_BANK_BYTES 64u\n'+
                'static const fzx_results_image_t fzx_results_images[8]={'+','.join(rows)+'};\n'+
                'static const uint16_t fzx_results_font3[1][2]={{1,0}};\n'+
                'static const uint32_t fzx_results_tint_offsets[8]={UINT32_MAX,UINT32_MAX,UINT32_MAX,0,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};\n')
            def run(text,cc):
                (p/'native.c').write_text(text);exe=p/'native'
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all']
                command+=['-I'+str(q)for q in (p,ROOT/'tests/results_native_stub',ROOT/'saturn',ROOT/'include',ROOT/'third_party/SH2ZAM/saturn/include')]
                command+=[str(p/'native.c'),str(ROOT/'src/fzx_results_draw.c'),str(ROOT/'tests/results_native_driver.c'),'-o',str(exe)]
                c=subprocess.run(command,capture_output=True,text=True);self.assertEqual(c.returncode,0,c.stdout+c.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            for cc in ('gcc','clang'):
                r=run(source,cc);self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertIn('PASS',r.stdout)
            for before,after in (
                ('s->clip[2]-1','s->clip[2]'),
                ('w->sprites*32u','0u'),
                ('.trans_pixel_disable=1,.user_clipping_mode=2','.trans_pixel_disable=0,.user_clipping_mode=2'),
                ('w->textures+image.texture','w->textures'),
                ('w->count+2>w->capacity','w->count+2>w->capacity+1')):
                self.assertIn(before,source)
                r=run(source.replace(before,after),'gcc');self.assertNotEqual(r.returncode,0,'control passed: '+before)

if __name__=='__main__':unittest.main()
