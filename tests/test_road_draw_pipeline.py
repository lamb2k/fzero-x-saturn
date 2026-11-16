"""Public road draw boundary: painter traversal, outer walls, material binding, capacity."""
from pathlib import Path
import copy,ctypes,hashlib,os,shutil,struct,subprocess,tempfile,unittest
from test_road_material import Pack
from test_road_pack import fixture
from road_pack import pack
ROOT=Path(__file__).resolve().parents[1]
class View(ctypes.Structure):
    _fields_=[('words',ctypes.c_int32*18),('clip',ctypes.c_int16*4)]
class Group(ctypes.Structure):
    _fields_=[('start',ctypes.c_uint16),('end',ctypes.c_uint16),('direction',ctypes.c_int32),('depth',ctypes.c_float)]
class Plan(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('faults','count','visible','outer')]+[('fog_start',ctypes.c_int32),
              ('fog',ctypes.c_uint8*4),('primitive',ctypes.c_uint8*4),('groups',Group*64)]
class Chart(ctypes.Structure):
    _fields_=[('x',ctypes.c_int16),('y',ctypes.c_int16)]+[(n,ctypes.c_uint16) for n in
              ('width','height','triangle','material')]+[('offset',ctypes.c_uint32)]
class Stats(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('chunks','triangles','culled','charts','bytes','tested','shaded','fault')]
CB=ctypes.CFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.POINTER(Chart))
def synthetic():
    mesh,course=fixture()
    raw=b'\xff\xff'*16
    mesh['textures']['synthetic_checker']=dict(width=4,height=4,rgba_hex=(b'\xff'*64).hex(),
                                              source_sha256=hashlib.sha256(raw).hexdigest())
    m=mesh['materials'][0]
    m.update(tiles={'0':[0xf5100200,0x00008020]},sizes={'0':[0xf2000000,0x0000c00c]},
             load=[0xf3000000,0x0000f800])
    primitive=copy.deepcopy(m);primitive['combine'][0]=0xfc357e6a
    mesh['materials'].append(primitive)
    mesh['vertices']=[[-32,32,256,0,0,0,0,0,255],[-32,-32,256,0,0,0,0,0,255],
                      [32,32,256,0,0,0,0,0,255]]
    mesh['triangles']=[[0,1,2,0],[0,1,2,1]]
    mesh['origins']=[dict(chunk=0,conditional_outer=False),dict(chunk=1,conditional_outer=True)]
    mesh['chunks'].append(copy.deepcopy(mesh['chunks'][0]))
    return pack(mesh,course,True)
@unittest.skipUnless(shutil.which('gcc') and os.name!='nt','POSIX compiler required')
class RoadPipelineTests(unittest.TestCase):
    def gate(self,lib):
        data,info=synthetic();storage=ctypes.create_string_buffer(data);p=Pack()
        lib.fzx_road_pack_open.argtypes=[ctypes.POINTER(Pack),ctypes.c_void_p,ctypes.c_uint32,
                                        ctypes.c_uint32,ctypes.c_uint32,ctypes.c_bool]
        lib.fzx_road_pack_open.restype=ctypes.c_bool
        self.assertTrue(lib.fzx_road_pack_open(ctypes.byref(p),storage,len(data),info['hash'],info['course_hash'],True))
        lib.fzx_road_draw.argtypes=[ctypes.POINTER(Pack),ctypes.POINTER(View),ctypes.POINTER(Plan),
            ctypes.c_int32,ctypes.c_int32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,CB,ctypes.c_void_p,ctypes.POINTER(Stats)]
        lib.fzx_road_draw.restype=ctypes.c_bool
        v=View();v.words[0]=v.words[4]=v.words[8]=1<<30
        v.words[12]=v.words[13]=64*65536;v.words[14]=160*65536;v.words[15]=112*65536
        v.words[16]=65536;v.words[17]=64*65536;v.clip[:]=[0,0,320,224]
        plan=Plan();plan.count=1;plan.visible=2;plan.outer=1;plan.fog_start=990
        plan.primitive[:]=[0,255,0,255];plan.groups[0]=Group(0,0,1,0)
        atlas=(ctypes.c_uint16*1024)();rows=[];stats=Stats()
        @CB
        def emit(_,cp):
            c=cp.contents;rows.append([getattr(c,n) for n,_ in Chart._fields_]);return True
        def draw(capacity=2,size=ctypes.sizeof(atlas)):
            rows.clear()
            return lib.fzx_road_draw(ctypes.byref(p),ctypes.byref(v),ctypes.byref(plan),0,0,
                                     atlas,size,capacity,emit,None,ctypes.byref(stats))
        for direction,order in ((1,[1,0]),(-1,[0,1])):
            plan.groups[0].direction=direction
            self.assertTrue(draw());self.assertEqual([r[4] for r in rows],order)
            self.assertEqual((stats.chunks,stats.triangles,stats.charts),(2,2,2))
            for r in rows:
                x,y,w,h,tri,material,offset=r
                self.assertEqual((x,y,w,h),(152,104,16,16))
                self.assertEqual(material,tri)
                expected=0x83e0 if material else 0xffff
                for iy in range(h):
                    for ix in range(w):
                        self.assertEqual(atlas[offset//2+iy*w+ix],expected if ix+iy<15 else 0)
        plan.outer=0;self.assertTrue(draw());self.assertEqual([r[4] for r in rows],[0])
        plan.outer=1;self.assertFalse(draw(capacity=1));self.assertEqual(stats.fault,2)
        self.assertFalse(draw(size=8));self.assertEqual(stats.fault,2)
        plan.groups[0].direction=0;self.assertFalse(draw());self.assertEqual(stats.fault,1)
    def test_pipeline_and_controls(self):
        original=(ROOT/'src/fzx_road_draw.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td)
            def build(source,cc,name):
                unit=folder/(name+'.c');unit.write_text(source);lib=folder/(name+'.so')
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                    '-Wpedantic','-shared','-fPIC','-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),str(unit),
                    *[str(ROOT/'src'/n) for n in ('fzx_view.c','fzx_road_pack.c','fzx_material.c','fzx_road_material.c')],
                    '-o',str(lib)],check=True,capture_output=True)
                return ctypes.CDLL(str(lib))
            for cc in ('gcc','clang'):
                if shutil.which(cc):self.gate(build(original,cc,cc))
            mutants=[original.replace('group->direction==1','group->direction==-1'),
                     original.replace('!d->plan->outer &&','false &&'),
                     original.replace('material!=d->material_index','d->material_index==UINT32_MAX')]
            for i,mutant in enumerate(mutants):
                self.assertNotEqual(mutant,original)
                with self.subTest(control=i),self.assertRaises(AssertionError):self.gate(build(mutant,'gcc','mutant'+str(i)))
if __name__=='__main__':unittest.main()
