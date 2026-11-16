"""Rotation-table encoding, finite windows and original-pixel offline packing."""
from pathlib import Path
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from floor_rbg_assets import build, coefficient

DRIVER = r'''
#include "fzx_floor_rbg.c"
static bool accept(void *unused,const fzx_floor_polygon_t *p){(void)unused;return p->band<8;}
static bool refuse(void *unused,const fzx_floor_polygon_t *p){(void)unused;(void)p;return false;}
int main(void) {
    fzx_view_t v={0};v.clip[2]=320;v.clip[3]=224;
    fzx_floor_rbg_t out={0};
    int32_t xy[10][2]={{0,0},{16384,0},{16384,65536},{0,65536}};
    window(&v,(const int32_t (*)[2])xy,4,&out);
    if(out.visible_lines || out.window[0][0]!=640)return 1;
    xy[1][0]=xy[2][0]=32768;
    window(&v,(const int32_t (*)[2])xy,4,&out);
    if(out.visible_lines!=1 || out.window[0][0]!=0 || out.window[0][1]!=0)return 2;
    memset(&out,0,sizeof(out));xy[0][0]=xy[3][0]=32769;xy[1][0]=xy[2][0]=98304;
    window(&v,(const int32_t (*)[2])xy,4,&out);
    if(out.visible_lines!=1 || out.window[0][0]!=2 || out.window[0][1]!=2)return 3;
    if(fzx_floor_rbg_prepare(NULL,NULL,&out,NULL,NULL) || out.fault!=1)return 4;
    if(fzx_floor_rbg_coefficient(32768)!=0x80000000u)return 5;
    RefBackdrop source={.count=20};
    v.rotation[0][0]=v.rotation[1][1]=v.rotation[2][2]=1073741824;
    v.focal[0]=v.focal[1]=160*65536;v.center[0]=160*65536;v.center[1]=112*65536;
    v.near_plane=4096;v.far_plane=2000*4096;
    for(unsigned i=0;i<8;++i) {
        RefBackdropVertex *p=&source.vertices[i];
        p->position[0]=(i&1)?100:-100;p->position[1]=-10;
        p->position[2]=i<2?100:i>=6?900:500;
        p->uv[0]=p->position[0];p->uv[1]=p->position[2];
        p->color[0]=i<2 || i>=6?255:0;
    }
    if(!fzx_floor_rbg_prepare(&v,&source,&out,accept,NULL) || !out.commands || !out.visible_lines)return 6;
    if(fzx_floor_rbg_prepare(&v,&source,&out,refuse,NULL) || out.fault!=5)return 7;
    source.vertices[1].position[1]++;
    if(fzx_floor_rbg_prepare(&v,&source,&out,accept,NULL) || out.fault!=1)return 8;
    source.vertices[1].position[1]--;
    for(unsigned i=0;i<8;++i)memcpy(source.vertices[i].position,source.vertices[0].position,6);
    if(fzx_floor_rbg_prepare(&v,&source,&out,accept,NULL) || out.fault!=2)return 9;
    return 0;
}
'''


class FloorAssets(unittest.TestCase):
    def test_original_pixel_round_trip(self):
        pixels=[0x8000|i for i in range(2048)]
        chars,names,lut=build(struct.pack('>2048H',*pixels))
        self.assertEqual((len(chars),len(names),len(lut)),(4096,16384,131072))
        for y in range(32):
            for x in range(64):
                attr,addr=struct.unpack_from('>HH',names,((y//8)*64+x//8)*4)
                self.assertEqual(attr,0)
                word=struct.unpack_from('>H',chars,addr*32+((y%8)*8+x%8)*2)[0]
                self.assertEqual(word,pixels[y*64+x])
        self.assertEqual(names[:32],names[32:64])
        with self.assertRaises(ValueError):
            build(bytes(4096))

    @unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'POSIX compilers')
    def test_window_and_encoding_controls(self):
        source=(ROOT/'src/fzx_floor_rbg.c').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            for cc in ('gcc','clang'):
                for mutant in (False,True):
                    text=source.replace('left>right || right<32768','left>right') if mutant else source
                    (folder/'fzx_floor_rbg.c').write_text(text)
                    (folder/'driver.c').write_text(DRIVER)
                    exe=folder/'probe'
                    subprocess.run([cc,'-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),str(folder/'driver.c'),
                        str(ROOT/'src/fzx_view.c'),'-o',str(exe)],check=True,capture_output=True)
                    run=subprocess.run([str(exe)],capture_output=True)
                    self.assertEqual(run.returncode,1 if mutant else 0,run.stderr)
                lib=folder/'floor.so'
                subprocess.run([cc,'-std=c11','-O2','-shared','-fPIC','-I'+str(ROOT/'include'),
                    '-I'+str(ROOT/'ref'),str(ROOT/'src/fzx_floor_rbg.c'),str(ROOT/'src/fzx_view.c'),
                    '-o',str(lib)],check=True,capture_output=True)
                core=ctypes.CDLL(str(lib));core.fzx_floor_rbg_coefficient.restype=ctypes.c_uint32
                self.assertEqual([core.fzx_floor_rbg_coefficient(i) for i in range(32768)],
                                 [coefficient(i) for i in range(32768)])


if __name__=='__main__':
    unittest.main()
