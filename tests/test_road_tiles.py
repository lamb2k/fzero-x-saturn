"""Native shared-road strips: source coverage, packing and draw rejection gates."""
from pathlib import Path
import copy,ctypes,hashlib,os,shlex,shutil,struct,subprocess,tempfile,unittest
from test_road_draw_pipeline import View,Plan,Group
from test_road_material import Pack
from test_road_pack import fixture
import road_tiles
from road_pack import pack,fnv
ROOT=Path(__file__).resolve().parents[1]
class Tiles(ctypes.Structure):
    _fields_=[('road',ctypes.POINTER(Pack)),('data',ctypes.c_void_p)]+[(n,ctypes.c_uint32) for n in ('bytes','textures','chunks','quads','strips','chunk_at','quad_at','strip_at')]
class Quad(ctypes.Structure):
    _fields_=[('x',ctypes.c_int16*4),('y',ctypes.c_int16*4),('shade',ctypes.c_uint16*4),('width',ctypes.c_uint16),('height',ctypes.c_uint16),('offset',ctypes.c_uint32),('solid',ctypes.c_bool)]
class Stats(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('chunks','quads','draws','culled','mean','clipped','fault','hash')]
CB=ctypes.CFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.POINTER(Quad))
def synthetic():
    mesh,course=fixture();raw=struct.pack('>16H',*[0xf801,0x07c1,0x003f,0xffff]*4)
    rgba=bytes([255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255])*4
    mesh['textures']['synthetic_checker']=dict(width=4,height=4,rgba_hex=rgba.hex(),source_sha256=hashlib.sha256(raw).hexdigest())
    m=mesh['materials'][0];m.update(tiles={'0':[0xf5100200,0x00008020]},sizes={'0':[0xf2000000,0x0000c00c]},load=[0xf3000000,0x0000f800])
    prim=copy.deepcopy(m);prim['combine'][0]=0xfc357e6a;mesh['materials'].append(prim)
    mesh['vertices']=[[-200,-40,600,0,2048,0,0,0,255],[-200,-40,200,0,0,0,0,0,255],
                      [200,-40,200,96,0,0,0,0,255],[200,-40,600,96,2048,0,0,0,255]]
    mesh['triangles']=[[0,1,2,0],[0,2,3,0],[0,1,2,1],[0,2,3,1]]
    mesh['origins']=[dict(chunk=i//2,conditional_outer=i>=2) for i in range(4)]
    mesh['chunks'].append(copy.deepcopy(mesh['chunks'][0]));return mesh,course
class OfflineTests(unittest.TestCase):
    def test_low_ram_log_reservation_and_rejection(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);stub=p/'stubs.c';stub.write_text('unsigned char fixture[0x6b0c0];\n')
            def generate(flag,machine_menu='0'):
                env=dict(os.environ,FZX_INPUT_LOG_LOW_RAM=flag,FZX_SOURCE_MACHINE_MENU=machine_menu)
                return subprocess.run([os.sys.executable,str(ROOT/'ref/sat/gen_sat.py'),
                    '--stubs',str(p/'out'),str(stub)],env=env,capture_output=True)
            self.assertEqual(generate('0').returncode,0)
            off=(p/'out/sat_stubs_data.s').read_text()
            self.assertNotIn('_fzx_input_log',off)
            self.assertEqual(generate('1').returncode,0)
            on=(p/'out/sat_stubs_data.s').read_text()
            self.assertTrue(on.startswith(off))
            self.assertIn('.set _fzx_input_log, 0x0026C0C0',on)
            self.assertEqual(generate('1','1').returncode,0)
            menu=(p/'out/sat_stubs_data.s').read_text()
            self.assertTrue(menu.startswith(off))
            self.assertIn('.set _fzx_input_log, 0x0026E0C0',menu)
            self.assertNotEqual(generate('1','invalid').returncode,0)
            self.assertNotEqual(generate('invalid').returncode,0)
            stub.write_text('unsigned char too_full[0xf4000];\n')
            result=generate('1');self.assertNotEqual(result.returncode,0)
            self.assertIn(b'Input log exceeds low work RAM',result.stderr)
            # The 256-entry directory reserve must also reject a layout which
            # still fits with the old 32-entry directory and 4 KiB gap.
            stub.write_text('unsigned char tight[0xf2000];\n')
            self.assertEqual(generate('1').returncode,0)
            self.assertNotEqual(generate('1','1').returncode,0)
    def test_pair_coverage_and_original_strips(self):
        m,c=synthetic();r,ri=pack(m,c,True);meta,tex,info=road_tiles.convert(m,ri['hash'],[255,0,255])
        self.assertEqual((info['quads'],info['triangles'],info['strips']),(2,4,1))
        self.assertEqual(len(meta),64+16+32+32);self.assertTrue(all(w&0x8000 for w in struct.unpack('>%dH'%(len(tex)//2),tex)))
        # Full fog must erase texture variation to the source fog color.
        stride=len(tex)//3;self.assertEqual(tex[2*stride:],struct.pack('>%dH'%(stride//2),*([0xfc1f]*(stride//2))))
        bad=copy.deepcopy(m);bad['triangles'][1][0]=1
        with self.assertRaises(road_tiles.ArtError):road_tiles.pair(bad)
        bad=copy.deepcopy(m);bad['origins'][1]['conditional_outer']=True
        with self.assertRaises(road_tiles.ArtError):road_tiles.pair(bad)
        bad=copy.deepcopy(m);bad['vertices'][0][5]=20
        with self.assertRaises(road_tiles.ArtError):road_tiles.pair(bad)
@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang') and os.name!='nt','POSIX GCC/Clang required')
class NativeTests(unittest.TestCase):
    def gate(self,lib):
        m,c=synthetic();data,info=pack(m,c,True);meta,tex,mi=road_tiles.convert(m,info['hash'],[255,0,255])
        rb=ctypes.create_string_buffer(data);mb=ctypes.create_string_buffer(meta);road=Pack();tiles=Tiles()
        lib.fzx_road_pack_open.argtypes=[ctypes.POINTER(Pack),ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_bool];lib.fzx_road_pack_open.restype=ctypes.c_bool
        self.assertTrue(lib.fzx_road_pack_open(ctypes.byref(road),rb,len(data),info['hash'],info['course_hash'],True))
        lib.fzx_road_tiles_open.argtypes=[ctypes.POINTER(Tiles),ctypes.POINTER(Pack),ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32];lib.fzx_road_tiles_open.restype=ctypes.c_bool
        self.assertTrue(lib.fzx_road_tiles_open(ctypes.byref(tiles),ctypes.byref(road),mb,len(meta),len(tex),fnv(meta)))
        lib.fzx_road_tiles_draw.argtypes=[ctypes.POINTER(Tiles),ctypes.POINTER(View),ctypes.POINTER(Plan),ctypes.c_int32,ctypes.c_int32,ctypes.c_uint32,CB,ctypes.c_void_p,ctypes.POINTER(Stats)];lib.fzx_road_tiles_draw.restype=ctypes.c_bool
        v=View();v.words[0]=v.words[4]=v.words[8]=1<<30;v.words[12]=v.words[13]=160*65536;v.words[14]=160*65536;v.words[15]=112*65536;v.words[16]=4096;v.words[17]=10000*4096;v.clip[:]=[12,16,308,224]
        plan=Plan();plan.count=1;plan.visible=2;plan.outer=1;plan.fog_start=990;plan.fog[:]=[255,0,255,255];plan.primitive[:]=[255,0,0,255];plan.groups[0]=Group(0,0,1,0)
        rows=[];stats=Stats();valid=True
        @CB
        def emit(ctx,ptr):
            nonlocal valid
            q=ptr.contents
            if ctx:return False
            if not q.solid and (not q.width or q.width%8 or q.offset+q.width*q.height*2>len(tex)):valid=False
            if any(abs(x)>900 for x in list(q.x)+list(q.y)):valid=False
            rows.append(q.solid);return True
        def draw(capacity=256,context=None):
            rows.clear();return lib.fzx_road_tiles_draw(ctypes.byref(tiles),ctypes.byref(v),ctypes.byref(plan),1<<30,-8192,capacity,emit,context,ctypes.byref(stats))
        self.assertTrue(draw());self.assertTrue(valid);self.assertGreater(stats.draws,1);self.assertTrue(rows[0]);h=stats.hash
        plan.groups[0].direction=-1;self.assertTrue(draw());self.assertFalse(rows[0])
        plan.outer=0;self.assertTrue(draw());self.assertNotIn(True,rows)
        self.assertFalse(draw(1));self.assertEqual(stats.fault,2)
        self.assertFalse(draw(context=1));self.assertEqual(stats.fault,3)
        v.words[11]=1500*4096;v.words[13]=1000*65536;self.assertTrue(draw());self.assertGreater(stats.mean,0)
        # Valid-hash structural mutations cannot hide a missing source vertex.
        for at,value in ((tiles.quad_at,65535),(tiles.quad_at+8,100),(tiles.quad_at+12,8)):
            bad=bytearray(meta);struct.pack_into('>H',bad,at,value);bb=ctypes.create_string_buffer(bytes(bad));out=Tiles()
            self.assertFalse(lib.fzx_road_tiles_open(ctypes.byref(out),ctypes.byref(road),bb,len(bad),len(tex),fnv(bad)))
        return h
    def clipped_fog_gate(self,lib):
        # One corner crosses the near plane: five clipped vertices need two
        # VDP1 commands. Check colors independently of the implementation hash.
        v=View();v.words[12]=v.words[13]=160*65536
        v.words[14]=160*65536;v.words[15]=112*65536
        v.words[16]=10*4096;v.words[17]=1000*4096;v.clip[:]=[0,0,320,224]
        plan=Plan();plan.primitive[:]=[255,0,0,255];plan.fog[:]=[0,0,255,255]
        points=((ctypes.c_int32*3)*4)(*[(ctypes.c_int32*3)(x*4096,y*4096,z*4096)
            for x,y,z in [(-3,-3,5),(-3,3,20),(3,3,30),(3,-3,40)]])
        clipped=((ctypes.c_int32*3)*10)()
        lib.fzx_view_clip.argtypes=[ctypes.POINTER(View),ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p]
        guard=View.from_buffer_copy(v);guard.clip[:]=[-900,-900,900,900]
        n=lib.fzx_view_clip(ctypes.byref(guard),points,4,clipped)
        self.assertEqual(n,5)
        def trunc(a,b):return (abs(a)//abs(b))*(-1 if (a<0)!=(b<0) else 1)
        expected={}
        for x,y,z in list(clipped)[:n]:
            xy=(trunc(v.words[14]+trunc(v.words[12]*x,z),65536),
                trunc(v.words[15]-trunc(v.words[13]*y,z),65536))
            ratio=max(-(1<<30),min(1<<30,(1<<30)+trunc(-81920*(1<<30),z)))
            fade=max(0,min(255,trunc(ratio*128,1<<30)))
            red=((255*(255-fade))*31+32512)//65025
            blue=((255*fade)*31+32512)//65025
            expected[xy]=0x8000|red|(blue<<10)
        rows=[]
        @CB
        def emit(ctx,ptr):
            q=ptr.contents;rows.append([(q.x[i],q.y[i],q.shade[i]) for i in range(4)]);return True
        lib.tiles_test_piece.argtypes=[ctypes.POINTER(View),ctypes.POINTER(Plan),ctypes.c_void_p,CB]
        lib.tiles_test_piece.restype=ctypes.c_bool
        self.assertTrue(lib.tiles_test_piece(ctypes.byref(v),ctypes.byref(plan),points,emit))
        self.assertEqual(len(rows),2);seen={}
        for row in rows:
            for x,y,color in row:
                self.assertEqual(color,expected[(x,y)])
                if (x,y) in seen:self.assertEqual(color,seen[(x,y)])
                seen[(x,y)]=color
        self.assertEqual(seen,expected)
        self.assertGreater(len(set(seen.values())),1)

    def projected_cull_gate(self,lib):
        import random
        v=View();v.words[12]=v.words[13]=160*65536
        v.words[14]=160*65536;v.words[15]=112*65536
        v.words[16]=4096;v.words[17]=10000*4096;v.clip[:]=[12,16,308,224]
        lib.tiles_test_outcode.argtypes=[ctypes.POINTER(View),ctypes.POINTER(ctypes.c_int32)]
        lib.tiles_test_outcode.restype=ctypes.c_uint
        rng=random.Random(1936)
        points=[(rng.randrange(-20000000,20000000),rng.randrange(-20000000,20000000),
                 rng.randrange(-100000,50000000)) for _ in range(2000)]
        # Exact projected edges, their adjacent Q16.16 units, and nonpositive Z.
        for delta in (-2,-1,0,1,2):
            for edge in (v.clip[0],v.clip[2]):points.append((edge*65536-v.words[14]+delta,0,v.words[12]))
            for edge in (v.clip[1],v.clip[3]):points.append((0,v.words[15]-edge*65536+delta,v.words[13]))
            points.append((delta,delta,delta))
        # Nonintegral projection at each clipping edge: integer world positions
        # immediately on either side exercise truncation in both signs.
        for z in (4097,123457,10000003):
            for edge in (v.clip[0],v.clip[2]):
                x=((edge*65536-v.words[14])*z)//v.words[12]
                points.extend((x+delta,0,z) for delta in (-1,0,1,2))
            for edge in (v.clip[1],v.clip[3]):
                y=((v.words[15]-edge*65536)*z)//v.words[13]
                points.extend((0,y+delta,z) for delta in (-1,0,1,2))
        points.extend(((2147483647,0,1),(-2147483648,0,1),
                       (0,2147483647,1),(0,-2147483648,1)))
        for x,y,z in points:
            distances=(z-v.words[16],v.words[17]-z,
                v.words[12]*x+(v.words[14]-v.clip[0]*65536)*z,
                (v.clip[2]*65536-v.words[14])*z-v.words[12]*x,
                (v.words[15]-v.clip[1]*65536)*z-v.words[13]*y,
                v.words[13]*y+(v.clip[3]*65536-v.words[15])*z)
            exact=sum(1<<i for i,d in enumerate(distances) if d<0)
            got=lib.tiles_test_outcode(ctypes.byref(v),(ctypes.c_int32*3)(x,y,z))
            self.assertEqual(got&~exact,0,'Projected culling rejected an in-bounds plane')

    def test_compilers_and_controls(self):
        code=(ROOT/'src/fzx_road_tiles.c').read_text(encoding='utf-8')
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)
            def build(text,cc,name):
                unit=p/(name+'.c');unit.write_text(text+"""
bool tiles_test_piece(const fzx_view_t *v,const RefRoadPlan *p,const int32_t points[][3],fzx_road_tile_emit_fn emit) {
    fzx_road_tile_stats_t stats={0};draw_t d={0};d.view=v;d.plan=p;d.pa=1<<30;d.pb=-81920;
    d.fm=128;d.fo=0;d.capacity=8;d.emit=emit;d.stats=&stats;
    return piece(&d,points,NULL,true,0,0,false);
}
unsigned tiles_test_outcode(const fzx_view_t *v,const int32_t p[3]) {
#if FZX_ROAD_TILE_PROJECT_CULL
    int32_t xy[2];bool valid=project(v,p,xy);return projected_outcode(v,p,xy,valid);
#else
    unsigned out=0;for(unsigned k=0;k<6;++k)if(fzx_view_plane(v,p,k)<0)out|=1u<<k;return out;
#endif
}
""");out=p/(name+'.so')
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',*shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),*(['-fno-sanitize=function'] if cc=='clang' and 'sanitize' in os.environ.get('FZX_TEST_CFLAGS','') else []),
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),str(unit),str(ROOT/'src/fzx_view.c'),str(ROOT/'src/fzx_road_pack.c'),'-o',str(out)],check=True,capture_output=True)
                return ctypes.CDLL(str(out))
            compilers=('gcc','gcc') if os.environ.get('FZX_TEST_CFLAGS') else ('gcc','clang','gcc')
            hashes=[]
            for i,cc in enumerate(compilers):
                lib=build(code,cc,'good'+str(i));hashes.append(self.gate(lib));self.clipped_fog_gate(lib);self.projected_cull_gate(lib)
            self.assertTrue(all(h==hashes[0] for h in hashes))
            for projected,edges in ((1,0),(0,1),(1,1)):
                variant=('#define FZX_ROAD_TILE_PROJECT_CULL %d\n#define FZX_ROAD_TILE_EDGE_REUSE %d\n'%(projected,edges))+code
                for cc in dict.fromkeys(compilers):
                    lib=build(variant,cc,'reuse%s%d%d'%(cc,projected,edges))
                    self.assertEqual(self.gate(lib),hashes[0]);self.clipped_fog_gate(lib);self.projected_cull_gate(lib)
            for name,prefix,mutant in (
                ('bad_projection','#define FZX_ROAD_TILE_PROJECT_CULL 1\n',code.replace('return code;','return code|63u;')),
                ('bad_edge','#define FZX_ROAD_TILE_EDGE_REUSE 1\n',code.replace('part[0][k]=previous[0][k]','part[0][k]=previous[1][k]'))):
                with self.assertRaises(AssertionError):self.assertEqual(self.gate(build(prefix+mutant,'gcc',name)),hashes[0])
            mutant=code.replace('!d->plan->outer &&','false &&')
            with self.assertRaises(AssertionError):self.gate(build(mutant,'gcc','mutant'))
            for name,mutant in (
                ('bad_fan',code.replace('q->shade[i]=shades[indices[i]]','q->shade[i]=shades[i]')),
                ('bad_depth',code.replace('fog(d,vertices[i][2])','fog(d,v[i%4][2])'))):
                # Keep the mutated unit warning-clean without changing its fault.
                mutant=mutant.replace('int fades[10],middle=0;','(void)vertices;int fades[10],middle=0;')
                with self.assertRaises(AssertionError):self.clipped_fog_gate(build(mutant,'gcc',name))
            print('road tiles '+','.join(compilers)+' hash %08x and negative controls PASS'%hashes[0])
if __name__=='__main__':unittest.main()
