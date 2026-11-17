"""ROM-free finish pack tests; all geometry and colors below are synthetic."""
from pathlib import Path
from fractions import Fraction
import copy
import struct
import sys
import unittest
import shutil
import subprocess
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from finish_pack import build,triangle_chart,METADATA_BYTES,ArtError
from finish_assets import TEXTURES,GEOMETRY,OPA_SURF
from road_pack import fnv


def fixture():
    vertices=[]
    for i in range(256):
        x=i-128
        vertices.append([x,x*x,0,(i%16)*32-16,(i//16)*32,0,0,127,255])
    triangles=[[i,(i+70)%256,(i+139)%256,0 if i<124 else 1] for i in range(222)]
    materials=[dict(texture=dict(name=n),geometry=GEOMETRY,combine=[0xfcffffff,0xfffcf279],
                    scale=[65535,65535],tile=0,other_l=OPA_SURF,other_h=0x82c00,
                    tiles={'0':[0xf5100400,0x4c130] if i==0 else [0xf5100800,0x90240]})
               for i,n in enumerate(TEXTURES)]
    textures=[dict(symbol=n,width=(8,16)[i],height=(8,16)[i],file=n+'.rgb1555') for i,n in enumerate(TEXTURES)]
    files={textures[0]['file']:struct.pack('>H',0x8123)*64,
           textures[1]['file']:b''.join(struct.pack('>H',0x8000+i%13) for i in range(256))}
    return dict(schema=1,model=dict(vertices=vertices,triangles=triangles,materials=materials,
                                  source_order=True,source_loaded_rows=256),textures=textures),files


def independent_pixel(vertices,pixels,x,y):
    """Solve source UV from a geometric chart point, using exact barycentrics.

    This does not call triangle_chart or reuse its integer UV interpolation.
    It constructs a point on the actual 3D source triangle and solves the
    independent two-dimensional area ratios after dropping its normal axis.
    """
    a,b,c=vertices
    left=[Fraction(a[i]*(15-y)+c[i]*y,15) for i in range(3)]
    right=[Fraction(b[i]*(15-y)+c[i]*y,15) for i in range(3)]
    point=[(left[i]*(15-x)+right[i]*x)/15 for i in range(3)]
    ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
    normal=[ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3] for i in range(3)]
    drop=max(range(3),key=lambda i:abs(normal[i]));axes=[i for i in range(3) if i!=drop]
    def area(p,q,r):
        u,v=axes
        return (q[u]-p[u])*(r[v]-p[v])-(q[v]-p[v])*(r[u]-p[u])
    total=area(a,b,c)
    bary=(area(point,b,c)/total,area(a,point,c)/total,area(a,b,point)/total)
    uv=[sum(bary[k]*vertices[k][axis] for k in range(3)) for axis in (3,4)]
    ij=[max(0,min(15,int((q+16)//32))) for q in uv]
    return struct.unpack_from('>H',pixels,(ij[1]*16+ij[0])*2)[0]


class FinishPackTests(unittest.TestCase):
    def test_complete_pack_and_independent_sampling(self):
        m,files=fixture();metadata,pixels,report=build(m,files)
        self.assertEqual((metadata,pixels,report),build(m,files))
        self.assertEqual(len(metadata),METADATA_BYTES)
        self.assertEqual(metadata[:8],b'FZXFIN01')
        values=struct.unpack_from('>14I',metadata,8)
        self.assertEqual(values[:3],(len(metadata),len(pixels),fnv(pixels)))
        zeroed=bytearray(metadata);zeroed[44:48]=bytes(4)
        self.assertEqual(values[9],fnv(zeroed))
        self.assertEqual(values[3:9],(256,222,98,64,4160,7712))
        self.assertEqual(struct.unpack_from('>16H',pixels)[:13],tuple(0x8000+i for i in range(13)))
        for i,v in enumerate(m['model']['vertices']):
            self.assertEqual(struct.unpack_from('>5h4B2x',metadata,64+i*16),tuple(v))
        image=0
        for i,t in enumerate(m['model']['triangles']):
            row=struct.unpack_from('>8H',metadata,4160+i*16)
            self.assertEqual(row,tuple(t+[0xffff if t[3]==0 else image,i,0,0]))
            if t[3]:image+=1
        # Three independent full charts include negative UV, clamping and the
        # collapsed C edge. Every image descriptor is separately accounted above.
        for image in (0,37,97):
            w,h,offset,ti,*reserved=struct.unpack_from('>2HI4H',metadata,7712+image*16)
            self.assertEqual((w,h,reserved),(16,16,[0,0,0]))
            vertices=[m['model']['vertices'][i] for i in m['model']['triangles'][ti][:3]]
            for y in range(16):
                for x in range(16):
                    value=pixels[offset+y*8+x//2];index=(value>>4) if x%2==0 else value&15
                    actual=struct.unpack_from('>H',pixels,index*2)[0]
                    self.assertEqual(actual,independent_pixel(vertices,files[TEXTURES[1]+'.rgb1555'],x,y))

    def test_shifted_uv_control_fails_sampling_comparison(self):
        m,files=fixture();t=m['model']['triangles'][124]
        v=[copy.deepcopy(m['model']['vertices'][i]) for i in t[:3]]
        texture=files[TEXTURES[1]+'.rgb1555'];palette=list(range(0x8000,0x800d))
        expected=triangle_chart(v,texture,palette)
        for p in v:p[3]+=64
        self.assertNotEqual(expected,triangle_chart(v,texture,palette))

    def test_invalid_source_controls(self):
        good,files=fixture()
        mutations=[lambda m:m['model']['vertices'].pop(),
                   lambda m:m['model']['triangles'].pop(),
                   lambda m:m['model'].__setitem__('source_order',False),
                   lambda m:m['model']['triangles'][0].__setitem__(0,256),
                   lambda m:m['model']['triangles'][0].__setitem__(1,0),
                   lambda m:m['model']['triangles'][0].__setitem__(3,1),
                   lambda m:m['model']['materials'][0].__setitem__('geometry',0x820404),
                   lambda m:m['model']['materials'][1]['tiles'].__setitem__('0',[0,0]),
                   lambda m:m['model']['vertices'][0].__setitem__(0,32768)]
        for change in mutations:
            m=copy.deepcopy(good);change(m)
            with self.assertRaises(ArtError):build(m,files)
        bad=dict(files);bad[TEXTURES[1]+'.rgb1555']=bytes(512)
        with self.assertRaises(ArtError):build(good,bad)
        bad=dict(files);bad[TEXTURES[0]+'.rgb1555']=bad[TEXTURES[0]+'.rgb1555'][:-1]
        with self.assertRaises(ArtError):build(good,bad)


class FinishCParserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not compiler:raise unittest.SkipTest('HOST C compiler unavailable')
        cls.temp=tempfile.TemporaryDirectory();cls.folder=Path(cls.temp.name)
        cls.root=Path(__file__).resolve().parents[1];cls.driver=cls.folder/'finish-pack-driver'
        subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                        '-I'+str(cls.root/'include'),'-I'+str(cls.root/'third_party/SH2ZAM/saturn/include'),
                        str(cls.root/'tests/finish_pack_driver.c'),str(cls.root/'src/fzx_finish_pack.c'),
                        '-o',str(cls.driver)],check=True,capture_output=True,text=True)
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'temp'):cls.temp.cleanup()
    def run_pack(self,metadata,texture,mh=None,th=None):
        a=self.folder/'meta.bin';b=self.folder/'texture.bin';a.write_bytes(metadata);b.write_bytes(texture)
        r=subprocess.run([str(self.driver),str(a),str(b),str(fnv(metadata) if mh is None else mh),
                          str(fnv(texture) if th is None else th)],check=True,capture_output=True,text=True)
        return r.stdout.strip()
    @staticmethod
    def repair(metadata,texture):
        result=bytearray(metadata);struct.pack_into('>I',result,16,fnv(texture))
        result[44:48]=bytes(4);struct.pack_into('>I',result,44,fnv(result));return result
    def test_admission_accessors_guards_and_corruption_controls(self):
        m,files=fixture();meta,tex,_=build(m,files)
        self.assertTrue(self.run_pack(meta,tex).startswith('PASS 256 222 124 98 '))
        self.assertEqual(self.run_pack(meta,tex,mh=fnv(meta)^1),'REJECT_METADATA')
        self.assertEqual(self.run_pack(meta,tex,th=fnv(tex)^1),'REJECT_METADATA')
        self.assertEqual(self.run_pack(meta[:-1],tex),'REJECT_METADATA')
        mutations=[(0,b'BAD'),(32,struct.pack('>I',65)),(52,struct.pack('>I',GEOMETRY|1)),
                   (64+14,b'\0\1'),(4160,struct.pack('>H',256)),
                   (4160+10,b'\0\1'),(4160+8,b'\0\0'),
                   (7712+4,struct.pack('>I',0xfffffff8)),(7712+8,b'\0\0'),
                   (7712,b'\0\x08'),(7712+12,b'\0\0\0\1')]
        for at,value in mutations:
            bad=bytearray(meta);bad[at:at+len(value)]=value;bad=self.repair(bad,tex)
            self.assertEqual(self.run_pack(bad,tex),'REJECT_METADATA',at)
        for at,value in ((0,b'\0\0'),(26,b'\x80\x01'),(32,b'\xff')):
            bad=bytearray(tex);bad[at:at+len(value)]=value
            # Valid identities force the C texture schema checks to reject.
            self.assertEqual(self.run_pack(self.repair(meta,bad),bad),'REJECT_TEXTURE',at)
        bad=bytearray(tex);bad[-1]^=1
        self.assertEqual(self.run_pack(meta,bad,th=fnv(tex)),'REJECT_TEXTURE')


if __name__=='__main__':unittest.main()
