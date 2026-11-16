"""Original-machine pack/renderer safety and source-state presentation contracts."""
from pathlib import Path
import copy,ctypes,os,shutil,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from model_probe.make_fixture import fixture_data
from model_assets import header
from machine_pack import chart_records,convert
from road_pack import fnv
from test_road_draw_pipeline import View
class Pack(ctypes.Structure):
    _fields_=[('data',ctypes.c_void_p),('bytes',ctypes.c_uint32),('textures',ctypes.c_uint32),('hash',ctypes.c_uint32),('fixture',ctypes.c_uint8)]
class State(ctypes.Structure):
    _fields_=[('position',ctypes.c_float*3),('basis',(ctypes.c_float*3)*3),('scale',ctypes.c_float*3),('distance',ctypes.c_float),('flags',ctypes.c_uint32),('faults',ctypes.c_uint32),('character',ctypes.c_int32),('custom',ctypes.c_int32),('lod',ctypes.c_int32),('visible',ctypes.c_int32),('body',ctypes.c_uint8*3),('ambient',ctypes.c_uint8*3),('light_rgb',ctypes.c_uint8*3),('fog',ctypes.c_uint8*3),('direction',ctypes.c_int8*3),('reserved',ctypes.c_uint8),('reflection',ctypes.c_int16*3),('reserved2',ctypes.c_uint16)]
class Light(ctypes.Structure):
    _fields_=[('palette',ctypes.c_uint16*16),('gouraud',ctypes.c_uint16*4),('saturated',ctypes.c_uint32)]
class Quad(ctypes.Structure):
    _fields_=[('x',ctypes.c_int16*4),('y',ctypes.c_int16*4),('width',ctypes.c_uint16),('height',ctypes.c_uint16),('triangle',ctypes.c_uint16),('color',ctypes.c_uint16),('offset',ctypes.c_uint32),('solid',ctypes.c_bool),('light',Light)]
class Stats(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('lod','triangles','draws','culled','clipped','saturated','fault','hash','positions','normals')]
CB=ctypes.CFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.POINTER(Quad))
def bind(lib):
    lib.fzx_machine_pack_open.argtypes=[ctypes.POINTER(Pack),ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_bool];lib.fzx_machine_pack_open.restype=ctypes.c_bool
    lib.fzx_machine_draw.argtypes=[ctypes.POINTER(Pack),ctypes.POINTER(View),ctypes.POINTER(State),ctypes.c_void_p,ctypes.c_uint,CB,ctypes.c_void_p,ctypes.POINTER(Stats)];lib.fzx_machine_draw.restype=ctypes.c_bool
    return lib
@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang') and os.name!='nt','POSIX compilers required')
class MachineTests(unittest.TestCase):
    def gate(self,lib,meta,bank):
        mb=ctypes.create_string_buffer(meta);pack=Pack();stats=Stats();work=ctypes.create_string_buffer(16384)
        self.assertTrue(lib.fzx_machine_pack_open(ctypes.byref(pack),mb,len(meta),len(bank),fnv(meta),True))
        v=View();v.words[0]=v.words[4]=v.words[8]=1<<30;v.words[12]=v.words[13]=160*65536;v.words[14]=160*65536;v.words[15]=112*65536;v.words[16]=4096;v.words[17]=2000*4096;v.clip[:]=[12,8,308,216]
        s=State();self.assertEqual(ctypes.sizeof(s),112);s.position[:]=[0,0,180];s.basis[0][:]=[0,0,1];s.basis[1][:]=[0,1,0];s.basis[2][:]=[1,0,0];s.scale[:]=[.1,.1,.1];s.lod=1;s.visible=1;s.ambient[:]=[100]*3;s.body[:]=[0,40,255];s.light_rgb[:]=[255]*3;s.direction[:]=[69]*3;s.reflection[:]=[10000,10000,-7000];s.fog[:]=[200,120,80]
        rows=[]
        @CB
        def emit(ctx,p):
            q=p.contents
            self.assertTrue(q.solid or q.offset+q.width*q.height//2<=len(bank))
            self.assertTrue(all(abs(x)<=1023 for x in list(q.x)+list(q.y)))
            rows.append(bytes(q));return not ctx
        def draw(capacity=511,context=None):
            rows.clear();before=bytes(s);ok=lib.fzx_machine_draw(ctypes.byref(pack),ctypes.byref(v),ctypes.byref(s),work,capacity,emit,context,ctypes.byref(stats));self.assertEqual(bytes(s),before);return ok
        self.assertTrue(draw());self.assertEqual(stats.draws,16);self.assertEqual(stats.clipped,0);baseline=stats.hash;baseline_rows=list(rows)
        self.assertTrue(draw());self.assertEqual(stats.hash,baseline);self.assertEqual(rows,baseline_rows)
        s.position[0]=20;self.assertTrue(draw());self.assertNotEqual(stats.hash,baseline);s.position[0]=0
        s.body[:]=[255,0,0];self.assertTrue(draw());self.assertNotEqual(stats.hash,baseline);s.body[:]=[0,40,255]
        s.ambient[:]=[50]*3;self.assertTrue(draw());self.assertNotEqual(stats.hash,baseline);s.ambient[:]=[100]*3
        s.reflection[:]=[-10000,-10000,7000];self.assertTrue(draw());self.assertNotEqual(stats.hash,baseline);s.reflection[:]=[10000,10000,-7000]
        for lod,native in ((1,1),(2,1),(3,1),(4,4),(5,5),(6,5)):
            s.lod=lod;self.assertTrue(draw());self.assertEqual(stats.lod,native);self.assertEqual(stats.draws,16)
        for lod in (0,):s.lod=lod;self.assertTrue(draw());self.assertEqual(stats.draws,0)
        s.lod=1;s.visible=0;self.assertTrue(draw());self.assertEqual(stats.draws,0);s.visible=1
        self.assertFalse(draw(1));self.assertEqual(stats.fault,7)
        self.assertFalse(draw(context=1));self.assertEqual(stats.fault,8)
        s.faults=1;self.assertFalse(draw());s.faults=0
        s.position[0]=float('nan');self.assertFalse(draw());s.position[0]=0
        s.character=1;self.assertFalse(draw());s.character=0
        s.direction[:]=[0]*3;self.assertFalse(draw());s.direction[:]=[69]*3
        s.position[2]=-180;self.assertTrue(draw());self.assertEqual(stats.draws,0)
        # Tilt the panel through the near plane. The guarded fallback must keep
        # clipped source geometry, write bounded commands, and remain deterministic.
        s.position[2]=22;v.words[16]=15*4096;s.basis[2][:]=[.707,0,.707];s.basis[0][:]=[-.707,0,.707]
        self.assertTrue(draw());self.assertGreater(stats.clipped,0);clipped=stats.hash
        self.assertTrue(draw());self.assertEqual(stats.hash,clipped)
        self.assertTrue(any(Quad.from_buffer_copy(r).solid for r in rows))
        # Hash verification and structural validation are independent: recompute
        # the hash of malformed metadata to ensure bounds checks reject it too.
        def reject(data,fixture=True):
            b=ctypes.create_string_buffer(bytes(data));self.assertFalse(lib.fzx_machine_pack_open(ctypes.byref(Pack()),b,len(data),len(bank),fnv(data),fixture))
        reject(meta,False);reject(meta[:-1])
        for at,value,fmt in ((8,len(meta)+1,'I'),(24,4,'I'),(40,1,'B'),(64,2,'H'),(64+2,513,'H'),(64+8,425,'I'),(64+20,129,'H')):
            bad=bytearray(meta);struct.pack_into('>'+fmt,bad,at,value);reject(bad)
        va,ta,ma=struct.unpack_from('>3I',meta,72)
        for at,value,fmt in ((va,2000,'h'),(va+14,128,'H'),(ma+6,2,'B'),(ta+8,7,'H'),(ta+12,17,'H'),(ta+16,len(bank),'I'),(ta+20,len(meta),'I')):
            bad=bytearray(meta);struct.pack_into('>'+fmt,bad,at,value);reject(bad)
        self.assertFalse(lib.fzx_machine_pack_open(ctypes.byref(Pack()),mb,len(meta),len(bank),fnv(meta)^1,True))
        return baseline,clipped
    def test_native_pack_draw_and_controls(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);models,bindings,textures,raw,lut,body,fog=fixture_data();(tmp/'model_probe.h').write_bytes(header(models,bindings,textures,raw,lut,body,fog,fixture=True))
            charts=[]
            for cc in ('gcc','clang'):
                exe=tmp/('bake-'+cc);subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),'-I'+str(tmp),str(ROOT/'tools/machine_pack_bake.c'),str(ROOT/'src/fzx_material.c'),'-lm','-o',str(exe)],check=True)
                charts.append(subprocess.check_output([str(exe)]))
            self.assertEqual(charts[0],charts[1]);models=copy.deepcopy(models)
            for model in models:model['materials'][0]['environment']='racer.bodyRGB'
            meta,bank,info=convert(models,chart_records(charts[0]),lut,fog,True)
            self.assertLessEqual(info['texture_bytes']+info['shade_bytes'],29952)
            code=(ROOT/'src/fzx_machine_draw.c').read_text();results=[]
            def build(text,cc,name):
                source=tmp/(name+'.c');source.write_text(text);out=tmp/(name+'.so')
                subprocess.run([cc,'-shared','-fPIC','-O2','-std=c11','-Wall','-Wextra','-Werror','-DFZX_SOURCE_MACHINE=1','-DFZX_SOURCE_CAMERA=1','-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),str(source),str(ROOT/'src/fzx_view.c'),str(ROOT/'src/fzx_machine.c'),str(ROOT/'src/fzx_material.c'),'-o',str(out)],check=True)
                return bind(ctypes.CDLL(str(out)))
            for cc in ('gcc','clang'):results.append(self.gate(build(code,cc,cc),meta,bank))
            self.assertEqual(results[0],results[1])
            for name,mutant in (
                ('body',code.replace('if(mat[7])memcpy(combine.environment,state->body,3);','if(0)memcpy(combine.environment,state->body,3);')),
                ('pose',code.replace('value=value/16+position[k];','value=value/16+(k==0?0:position[k]);')),
                ('alias',code.replace('pi=u16(v+14),ni=u16(v+16);','pi=0,ni=u16(v+16);'))):
                with self.assertRaises(AssertionError):self.gate(build(mutant,'gcc',name),meta,bank)
if __name__=='__main__':unittest.main()
