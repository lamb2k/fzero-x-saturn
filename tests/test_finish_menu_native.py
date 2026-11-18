"""Native writer API and palette boundaries; HOST only, not SDK/scanout proof."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux compilers required')
class Native(unittest.TestCase):
    @unittest.skipUnless((ROOT/'generated/finish_menu_ui/finish_menu_identity.h').is_file() and
        (ROOT/'generated/finish-menu-draw-source/finish_menu_draw_source.c').is_file(),'private generated source/assets required')
    def test_original_source_packets(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            for cc in ('gcc','clang'):
                cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow','-fsigned-char',
                    '-fsanitize=undefined','-fno-sanitize-recover=all']
                cmd+=['-I'+str(q) for q in (ROOT/'generated/finish_menu_ui',ROOT/'tests/finish_menu_native_stub',ROOT/'saturn',ROOT/'include')]
                cmd += [str(ROOT/'saturn/finish_menu_native.c'),
                    str(ROOT/'generated/finish-menu-draw-source/finish_menu_draw_source.c'),
                    str(ROOT/'tests/finish_menu_native_source_driver.c'),'-o',str(p/'test')]
                built=subprocess.run(cmd,capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stderr)
                result=subprocess.run([str(p/'test')],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertIn('rows=36000',result.stdout)
    def run_source(self,source,cc):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            label='{0,64,8,8,8,8,32,0,{0,65535,32768}}'
            glyph='{64,32,8,8,8,4,16,0,{0,65535,32768}}'
            (p/'finish_menu_identity.h').write_text('''#define FZX_FINISH_MENU_BYTES 256u
#define FZX_FINISH_MENU_IMAGES 25u
typedef struct {unsigned offset,bytes;unsigned short width,height,source_width,bpp,palette_count,reserved;unsigned short palette[32];} fzx_finish_menu_image_t;
static const fzx_finish_menu_image_t fzx_finish_menu_images[25]={'''+','.join([label]*24+[glyph])+'};\n')
            (p/'native.c').write_text(source)
            cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow',
                '-fsanitize=undefined','-fno-sanitize-recover=all']
            cmd+=['-I'+str(q) for q in (p,ROOT/'tests/finish_menu_native_stub',ROOT/'saturn',ROOT/'include')]
            cmd += [str(p/'native.c'),str(ROOT/'tests/finish_menu_native_driver.c'),'-o',str(p/'test')]
            built=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            return subprocess.run([str(p/'test')],capture_output=True,text=True)
    def test_api_and_controls(self):
        source=(ROOT/'saturn/finish_menu_native.c').read_text()
        for cc in ('gcc','clang'):
            result=self.run_source(source,cc)
            self.assertEqual(result.returncode,0,result.stderr)
        for old,new in (
            ('VDP1_CMDT_CM_CB_256','VDP1_CMDT_CM_CLUT_16'),
            ('(bank+1u)<<8','(bank+1u)<<9'),
            ('(right-1)','(right)'),
            ('(d->y-8)','(d->y-7)'),
            ('n+2>capacity','n+2>capacity+1'),
            ('(bank+1u)*256u+j','bank*256u+j'),
            ('j<32;++j)cram','j<31;++j)cram'),
            ('(pending.used&(1u<<bank))','(0u&(1u<<bank))')):
            self.assertIn(old,source)
            result=self.run_source(source.replace(old,new),'gcc')
            self.assertNotEqual(result.returncode,0,old)
if __name__=='__main__':unittest.main()
