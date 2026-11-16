"""Native road tile/combiner gate on a synthetic two-half texture load."""
from pathlib import Path
import copy
import ctypes
import hashlib
import math
import os
import random
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from road_pack import pack
from test_road_pack import fixture

class Texture(ctypes.Structure):
    _fields_=[('data',ctypes.c_void_p),('size',ctypes.c_uint32),('width',ctypes.c_uint16),
              ('height',ctypes.c_uint16)]+[(n,ctypes.c_uint8) for n in
              ('format','s_mode','t_mode','s_mask','t_mask','s_shift','t_shift')]
class Material(ctypes.Structure):
    _fields_=[('texture',Texture),('primitive',ctypes.c_bool)]
class Pack(ctypes.Structure):
    _fields_=[('data',ctypes.c_void_p)]+[(n,ctypes.c_uint32) for n in
              ('bytes','hash','flags','course_hash')]+[(n,ctypes.c_uint32*6) for n in ('count','offset')]
class Color(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint16) for n in ('r','g','b','a')]

def source_fixture():
    mesh,course=fixture()
    words=[((i*7&31)<<11)|((i*13&31)<<6)|((i*3&31)<<1)|(i&1) for i in range(32)]
    raw=struct.pack('>32H',*words)
    rgba=bytes(c for w in words for c in ((w>>11)*255//31,((w>>6)&31)*255//31,
                                         ((w>>1)&31)*255//31,(w&1)*255))
    mesh['textures']={'synthetic_checker':dict(width=4,height=8,rgba_hex=rgba.hex(),
                                             source_sha256=hashlib.sha256(raw).hexdigest())}
    mesh['materials']=[]
    for i in range(3):
        m=copy.deepcopy(fixture()[0]['materials'][0])
        m.update(tile=0,tiles={'0':[0xf5100200|(4 if i==1 else 0),0x00048020 if i==0 else 0x00008020]},
                 sizes={'0':[0xf2000000,0x0000c00c]},load=[0xf3000000,0x0001f800])
        if i==2:m['combine'][0]=0xfc357e6a
        mesh['materials'].append(m)
    data,info=pack(mesh,course,True)
    return data,info,words

def oracle_tex(words,index,u,v):
    # Independent source image addressing, including the second TMEM half and
    # negative mirrored coordinates. Input coordinates are already scaled.
    x=math.floor(u/256);y=math.floor(v/256);fx=u/256-x;fy=v/256-y
    def texel(s,t):
        s%=4
        if index==0:
            t%=8
            if t>=4:t=7-t
        else:t%=4
        if index==1:t+=4
        w=words[t*4+s]
        return [c/255 for c in ((w>>11)*255//31,((w>>6)&31)*255//31,
                                ((w>>1)&31)*255//31,(w&1)*255)]
    if fx+fy<1:a,b,c=texel(x,y),texel(x+1,y),texel(x,y+1);wx,wy=fx,fy
    else:a,b,c=texel(x+1,y+1),texel(x,y+1),texel(x+1,y);wx,wy=1-fx,1-fy
    return [a[k]+(b[k]-a[k])*wx+(c[k]-a[k])*wy for k in range(4)]

@unittest.skipUnless(shutil.which('gcc') and os.name!='nt','POSIX shared-library compiler required')
class RoadMaterialTests(unittest.TestCase):
    def run_gate(self,library):
        library.fzx_road_pack_open.argtypes=[ctypes.POINTER(Pack),ctypes.c_void_p,ctypes.c_uint32,
                                             ctypes.c_uint32,ctypes.c_uint32,ctypes.c_bool]
        library.fzx_road_pack_open.restype=ctypes.c_bool
        library.fzx_road_material_open.argtypes=[ctypes.POINTER(Material),ctypes.POINTER(Pack),ctypes.c_uint32]
        library.fzx_road_material_open.restype=ctypes.c_bool
        library.fzx_material_sample.argtypes=[ctypes.POINTER(Texture),ctypes.c_int32,ctypes.c_int32]
        library.fzx_material_sample.restype=Color
        library.fzx_road_material_shade.argtypes=[ctypes.POINTER(Material),Color,Color,ctypes.c_void_p,ctypes.c_void_p]
        library.fzx_road_material_shade.restype=Color
        library.fzx_road_material_pixel.argtypes=[ctypes.POINTER(Material),ctypes.c_int32,ctypes.c_int32,
                                                 Color,ctypes.c_void_p,ctypes.c_void_p]
        library.fzx_road_material_pixel.restype=ctypes.c_uint16
        data,info,words=source_fixture();buffer=ctypes.create_string_buffer(data);p=Pack()
        assert library.fzx_road_pack_open(ctypes.byref(p),buffer,len(data),info['hash'],info['course_hash'],True)
        rng=random.Random(0x524f4144)
        for i in range(3):
            material=Material()
            assert library.fzx_road_material_open(ctypes.byref(material),ctypes.byref(p),i)
            t=material.texture
            assert (t.width,t.height,t.size)==(4,4,32)
            assert t.data-p.data==info['offsets'][5]+(32 if i==1 else 0)
            for j in range(1500):
                u,v=rng.randrange(-32768*8,32768*8),rng.randrange(-32768*8,32768*8)
                sample=library.fzx_material_sample(ctypes.byref(t),u,v)
                expected=oracle_tex(words,i,u,v)
                assert all(abs(getattr(sample,n)-e*65535)<=1 for n,e in zip('rgba',expected))
                shade=Color(*(rng.randrange(65536) for _ in range(4)))
                primitive=(ctypes.c_uint8*4)(*(rng.randrange(256) for _ in range(4)))
                fog=(ctypes.c_uint8*3)(*(rng.randrange(256) for _ in range(3)))
                result=library.fzx_road_material_shade(ctypes.byref(material),sample,shade,primitive,fog)
                assert result.a==65535
                alpha=primitive[3]/255 if i==2 else sample.a/65535
                for c,n in enumerate('rgb'):
                    source=primitive[c]/255 if i==2 else getattr(sample,n)/65535
                    mixed=(source-getattr(shade,n)/65535)*alpha+getattr(shade,n)/65535
                    final=mixed*(1-shade.a/65535)+fog[c]/255*shade.a/65535
                    assert abs(getattr(result,n)-final*65535)<=2
                pixel=library.fzx_road_material_pixel(ctypes.byref(material),u,v,shade,primitive,fog)
                assert pixel==0x8000|sum((((getattr(result,n)+128)//257)>>3)<<(5*c) for c,n in enumerate('rgb'))
        # State/layout validation is independent of the storage hash gate.
        for offset,mask in ((4,1),(12,1),(24,8),(28,0x40),(32,1),(36,1),(60,1),(64,0x10)):
            changed=bytearray(data);changed[info['offsets'][3]+offset+3]^=mask
            damaged=ctypes.create_string_buffer(bytes(changed));p.data=ctypes.addressof(damaged)
            assert not library.fzx_road_material_open(ctypes.byref(Material()),ctypes.byref(p),0),(offset,mask)

    def test_tiles_combiner_and_controls(self):
        source=(ROOT/'src/fzx_road_material.c').read_text()
        controls=[source.replace('(tile0&0x1ffu)*8u','0u'),
                  source.replace('(uint8_t)((tile1>>18)&3u)','0'),
                  source.replace('light[c],65535u-alpha','light[c],alpha'),
                  source.replace('result[2],65535','result[2],shade.a')]
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td)
            def build(text,cc,name):
                unit=folder/(name+'.c');unit.write_text(text)
                lib=folder/(name+'.so')
                flags=shlex.split(os.environ.get('FZX_TEST_CFLAGS',''))
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                    '-Wshadow','-Wpedantic','-shared','-fPIC',
                    *flags,
                    '-I'+str(ROOT/'include'),str(unit),
                    str(ROOT/'src/fzx_material.c'),str(ROOT/'src/fzx_road_pack.c'),
                    *(['-lubsan'] if any('-fsanitize=undefined'==f for f in flags) else []),
                    '-o',str(lib)],check=True)
                return ctypes.CDLL(str(lib))
            for cc in ('gcc','clang'):
                if shutil.which(cc):self.run_gate(build(source,cc,cc))
            for i,text in enumerate(controls):
                self.assertNotEqual(text,source)
                with self.subTest(control=i),self.assertRaises(AssertionError):
                    self.run_gate(build(text,'gcc','control'+str(i)))

if __name__=='__main__':unittest.main()
