"""Synthetic real native command writer: HOST API contract, not Saturn rendering."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux host compilers required')
class RecordsNative(unittest.TestCase):
    def headers(self,p,invalid_span=False):
        rows=[]
        for i in range(9):
            height=16 if i==8 else 4
            clut=i==1
            rows.append('{%d,%d,8,%d,8,%d,0,{0,65535,32768,0,0,0,0,0,0,0,0,0,0,0,0,0}}'%
                        (i*64,8*height//2 if clut else 8*height*2,height,clut))
        common='#include "fzx_results_draw.h"\n'
        (p/'records_identity.h').write_text(common+'#define FZX_RECORDS_UI_BYTES 1024u\n#define FZX_RECORDS_UI_IMAGES 9u\n'+
                'static const fzx_results_image_t fzx_records_images[9]={'+','.join(rows)+'};\n')
        (p/'results_identity.h').write_text(common+'#define FZX_RESULTS_TINT_BANK_BYTES 64u\n'+
                'static const fzx_results_image_t fzx_results_images[9]={'+','.join(rows)+'};\n'+
                'static const uint32_t fzx_results_tint_offsets[8]={UINT32_MAX,UINT32_MAX,UINT32_MAX,0,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};\n')
        models=[]
        for c in range(30):
            group=[]
            for s in range(4):
                at=(c*4+s)*32
                if invalid_span and (c,s)==(29,3):at=3840-8
                group.append('{%d,32,%d,0,0,%d,8,2,5,-2,-1}'%(at,at+32,(c<<16)|(s<<8)|1))
            models.append('{'+','.join(group)+'}')
        (p/'records_models_identity.h').write_text('''#define FZX_RECORDS_MODELS_BYTES 3904u
#define FZX_RECORDS_MODELS_BANK_OFFSET 64u
#define FZX_RECORDS_MODELS_MAX_VRAM_BYTES 32u
#define FZX_RECORDS_MODELS_SIX_VRAM_BYTES 192u
typedef struct { unsigned pixel_offset,pixel_bytes,palette_offset,palette_bytes,hash,body_rgb24; unsigned short width,height,format; short x,y; } FzxRecordsModelAsset;
static const int fzx_records_models_matrix_q16[18]={65536,0,0,0,0,65536,0,0,0,0,65536,0,0,0,0,65536,65536,65536};
static const FzxRecordsModelAsset fzx_records_models[30][4]={'''+','.join(models)+'};\n')

    def run_native(self,source,cc='gcc',argument=None,invalid_span=False):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);self.headers(p,invalid_span)
            (p/'native.c').write_text(source);exe=p/'native'
            command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow','-Wno-misleading-indentation',
                     '-fsanitize=undefined','-fno-sanitize-recover=all']
            command+=['-I'+str(q) for q in (p,ROOT/'tests/results_scene_stub',ROOT/'saturn',ROOT/'include',ROOT/'third_party/SH2ZAM/saturn/include')]
            command+=[str(p/'native.c'),str(ROOT/'src/fzx_records_draw.c'),str(ROOT/'tests/records_native_driver.c'),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            return subprocess.run([str(exe)]+([argument] if argument else []),capture_output=True,text=True)

    def test_native_api_two_compilers_and_negative_controls(self):
        source=(ROOT/'saturn/records_native.c').read_text()
        for cc in ('gcc','clang'):
            result=self.run_native(source,cc)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('RECORDS_NATIVE_API_AND_CONTROLS_PASS',result.stdout)
        for before,after in (
            ('s->clip[2]-1','s->clip[2]'),
            ('w->palettes+w->palette_count*32u','w->palettes+0u'),
            ('w->count+2>w->capacity','w->count+2>w->capacity+1'),
            ('init.center_y=(int16_t)(s->y-8)','init.center_y=s->y'),
            ('init.focal_x=m[16]*320','init.focal_x=m[16]*160'),
            ('previews+i*FZX_RECORDS_MODELS_MAX_VRAM_BYTES','previews+q->slot*FZX_RECORDS_MODELS_MAX_VRAM_BYTES')):
            self.assertIn(before,source)
            result=self.run_native(source.replace(before,after))
            self.assertNotEqual(result.returncode,0,'Mutation unexpectedly passed: '+before)

    def test_overlapping_vram_ranges_rejected(self):
        result=self.run_native((ROOT/'saturn/records_native.c').read_text(),argument='overlap')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_model_span_crossing_bank_end_rejected(self):
        result=self.run_native((ROOT/'saturn/records_native.c').read_text(),argument='model-span',invalid_span=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
