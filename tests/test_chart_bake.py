"""Synthetic native chart mapping, lineage and failure controls; no retail data."""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
HEADER=r'''
#include "fzx_model.h"
#define FZX_MODEL_FIXTURE 1
#define FZX_MODEL_VIEW_COUNT 1u
static const uint8_t data[]={0x0f,0xf0};
static const uint8_t fzx_model_body[3]={0,0,0},fzx_model_fog[3]={0,0,0};
static const fzx_material_texture_t fzx_model_textures[]={{data,2,2,2,FZX_TEX_I4,2,2,0,0,0,0}};
static const fzx_model_vertex_t vertices[]={
 {0,0,1024,0,0,{65535,65535,65535,0}},
 {256,0,2048,33,0,{65535,65535,65535,0}},
 {0,256,1024,0,33,{65535,65535,65535,0}}};
static const fzx_model_triangle_t triangles[]={{{0,1,2},0}};
static const fzx_model_material_t materials[]={{{{255,255,255},{0,0,0},false},0,false}};
static const fzx_model_view_t fzx_model_views[]={{vertices,triangles,materials,3,1,1}};
'''
DRIVER=r'''
#define main chart_bake_main
#include "bake.c"
#undef main
int main(void){
 const fzx_model_vertex_t *a=&vertices[0],*b=&vertices[1],*c=&vertices[2];
 const fzx_model_material_t *m=&materials[0];
 if(pixel(a,b,c,0,0,m)!=0x8000)return 11;
 if(pixel(a,b,c,1,0,m)!=0xffff)return 12;
 if(pixel(a,b,c,0,1,m)!=0xffff)return 13;
 /* s=t=.5 gives weights .25,.25,.5. Reciprocal depths 1,2,1
    give u=v=102 in Q8 after the original 65535 scale/truncation (tc 33 -> Q8 256). */
 fzx_material_color_t shade={65535,65535,65535,0};
 uint16_t expected=fzx_material_pixel(&m->combine,
     fzx_material_sample(&fzx_model_textures[0],102,102),shade,fzx_model_fog);
 if(pixel(a,b,c,.5,.5,m)!=expected)return 14;
 if(dimension(0,8)!=8||dimension(15,8)!=16||dimension(200,8)!=128)return 15;
 return 0;
}
'''

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC and Clang required')
class ChartBakeTests(unittest.TestCase):
    def test_mapping_lineage_and_controls(self):
        original=(ROOT/'tools/vdp1_probe/bake.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);(d/'model_probe.h').write_text(HEADER);(d/'driver.c').write_text(DRIVER)
            flags=shlex.split(os.environ.get('FZX_TEST_CFLAGS',''))
            def build(text,cc,driver):
                (d/'bake.c').write_text(text)
                exe=d/'gate'
                subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror',*flags,
                                '-I'+str(d),'-I'+str(ROOT/'include'),str(d/driver),
                                str(ROOT/'src/fzx_material.c'),'-lm','-o',str(exe)],check=True,capture_output=True)
                return subprocess.run([str(exe)],capture_output=True)
            outputs=[]
            for cc in ('gcc','clang'):
                r=build(original,cc,'driver.c');self.assertEqual(r.returncode,0,r.stderr)
                r=build(original,cc,'bake.c');self.assertEqual(r.returncode,0,r.stderr)
                outputs.append(r.stdout)
                self.assertIn(b'triangles=1',r.stderr)
                words=re.search(rb'chart_0_0\[\]=\{(.*?)\};',r.stdout,re.S)[1]
                self.assertTrue(all(int(v)&0x8000 for v in re.findall(rb'\d+',words)))
                self.assertIn(b'{chart_0_0,24,24,0,0,',r.stdout)
            self.assertEqual(outputs[0],outputs[1])
            for changed_header in (HEADER.replace('0,0,1024,','0,0,0,'),
                                   HEADER.replace('{{0,1,2},0}', '{{0,1,3},0}'),
                                   HEADER.replace('false},0,false','false},1,false')):
                self.assertNotEqual(HEADER,changed_header)
                (d/'model_probe.h').write_text(changed_header)
                self.assertEqual(build(original,'gcc','bake.c').returncode,2)
            (d/'model_probe.h').write_text(HEADER)
            for name,changed in [
                ('wrong-uv',original.replace('fzx_material_vertex_uv(v[i]->u_q5,65535)','0')),
                ('wrong-perspective',original.replace('weight[i]*v[i]->inverse_z','weight[i]'))]:
                self.assertNotEqual(original,changed)
                with self.subTest(control=name):
                    self.assertNotEqual(build(changed,'gcc','driver.c').returncode,0)

if __name__=='__main__':unittest.main()
