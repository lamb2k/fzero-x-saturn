"""Synthetic cartridge-pack round trip and independently rehashed bad records."""
from pathlib import Path
import copy
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from road_pack import pack,fnv

def fixture():
    raw=struct.pack('>4H',0xf801,0x07c1,0x003f,0xffff)
    rgba=bytes([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255])
    tex=dict(width=2,height=2,rgba_hex=rgba.hex(),source_sha256=hashlib.sha256(raw).hexdigest())
    material=dict(geometry=0xa10405,other_h=0x82c00,other_l=0xfa006220,combine=[0xfc147e28,0x44fe793c],
                  prim=0xff0000ff,tiles={'0':[0xf5100200,0x00004010]},sizes={'0':[0xf2000000,0x00004004]},
                  texture=dict(name='synthetic_checker',image=0xfd100001),enabled=True,scale=[65535,65535],
                  tile=0,load=[0xf3000000,0x07003000])
    chunk=dict(flags=1,segment=0,points=[0]*24)
    mesh=dict(format='fzx-source-road-v1',vertices=[[-123,45,67,-90,120,0,10,20,255],
              [180,-200,330,30,50,4,5,6,255],[70,90,110,210,-110,7,8,9,255]],
              triangles=[[0,1,2,0],[2,1,0,0]],origins=[dict(chunk=0,conditional_outer=False),
              dict(chunk=0,conditional_outer=True)],chunks=[chunk,copy.deepcopy(chunk)],
              materials=[material],textures={'synthetic_checker':tex})
    course=bytes((i*7)&255 for i in range(0x7e0))
    return mesh,course

DRIVER=r'''
#include "fzx_road_pack.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if(argc!=5) return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END)) return 2;
    long length=ftell(f);rewind(f);
    if(length<0 || length>FZX_ROAD_MAX_BYTES+1) return 2;
    uint8_t *data=malloc((size_t)length+1u);if(!data)return 2;
    if(fread(data,1,(size_t)length,f)!=(size_t)length)return 2;
    fclose(f);
    fzx_road_pack_t p;
    bool ok=fzx_road_pack_open(&p,data,(uint32_t)length,(uint32_t)strtoul(argv[2],NULL,16),
                                (uint32_t)strtoul(argv[3],NULL,16),atoi(argv[4])!=0);
    if(ok) {
        for(unsigned s=0;s<FZX_ROAD_SECTIONS;++s)
            if(!fzx_road_record(&p,s,0) || fzx_road_record(&p,s,p.count[s])) abort();
        printf("%u %u %u %u %u %u\n",p.count[0],p.count[1],p.count[2],p.count[3],p.count[4],p.count[5]);
    } else if(p.data) abort();
    free(data);return ok?0:3;
}
'''

class RoadPack(unittest.TestCase):
    def test_native_loader_and_structural_controls(self):
        mesh,course=fixture();data,info=pack(mesh,course,True)
        variants=[]
        def corrupt(at,value):
            b=bytearray(data);struct.pack_into('>I',b,at,value)
            b[20:24]=bytes(4);h=fnv(b);struct.pack_into('>I',b,20,h)
            variants.append((bytes(b),h))
        corrupt(24,0xffffffff) # Impossible vertex count, even with a valid hash.
        corrupt(48,132) # Non-canonical/overlapping section.
        corrupt(108,1) # Reserved header.
        corrupt(info['offsets'][1],0xffff0001) # Missing vertex.
        corrupt(info['offsets'][1]+8,0x00010000) # Wrong owning chunk.
        corrupt(info['offsets'][2]+8,1) # Missing triangle span.
        corrupt(info['offsets'][3]+40,0xffff0002) # Missing texture.
        corrupt(info['offsets'][4]+4,0xffffffff) # Texture beyond pack.
        corrupt(info['offsets'][4]+12,0) # Texture checksum.
        variants.extend([(data[:-1],info['hash']),(data+b'x',info['hash'])])
        tampered=bytearray(data);tampered[-1]^=1;variants.append((bytes(tampered),info['hash']))
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);(d/'driver.c').write_text(DRIVER)
            for cc in ('gcc','clang'):
                if not shutil.which(cc):continue
                for sanitized in (False,True):
                    exe=d/(cc+str(sanitized))
                    flags=['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitized else ['-O2']
                    subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',*flags,
                                    '-I'+str(ROOT/'include'),str(d/'driver.c'),str(ROOT/'src/fzx_road_pack.c'),'-o',str(exe)],check=True)
                    def run(blob,h,course_hash=info['course_hash'],mode=1):
                        (d/'pack').write_bytes(blob)
                        return subprocess.run([str(exe),str(d/'pack'),'%x'%h,'%x'%course_hash,str(mode)],capture_output=True,text=True)
                    good=run(data,info['hash']);self.assertEqual(good.returncode,0,good.stderr)
                    self.assertEqual(list(map(int,good.stdout.split())),info['counts'])
                    for blob,h in variants:
                        bad=run(blob,h);self.assertEqual(bad.returncode,3,bad.stderr)
                    for bad in (run(data,info['hash'],mode=0),run(data,info['hash'],course_hash=1),run(data,1)):
                        self.assertEqual(bad.returncode,3,bad.stderr)

    def test_pack_preserves_signed_vertices_source_colors_and_provenance(self):
        mesh,course=fixture();data,info=pack(mesh,course,True)
        self.assertEqual(list(struct.unpack_from('>5h4B',data,info['offsets'][0])),mesh['vertices'][0])
        self.assertEqual(struct.unpack_from('>6H',data,info['offsets'][1]+12),(2,1,0,0,0,1))
        self.assertEqual(data[info['offsets'][5]:],struct.pack('>4H',0xf801,0x07c1,0x003f,0xffff))
        bad=copy.deepcopy(mesh);bad['textures']['synthetic_checker']['rgba_hex']='01'+bad['textures']['synthetic_checker']['rgba_hex'][2:]
        with self.assertRaises(ValueError):pack(bad,course,True)
        bad=copy.deepcopy(mesh);bad['origins'][1]['chunk']=1
        with self.assertRaises(ValueError):pack(bad,course,True)

if __name__=='__main__':unittest.main()
