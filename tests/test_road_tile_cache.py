"""Per-draw source vertex reuse: exact commands, view invalidation and RAM bounds."""
from pathlib import Path
import ctypes as C
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from test_road_tiles import Tiles, Quad, Stats, CB, Pack, View, Plan, Group, synthetic
import road_tiles
from road_pack import pack, fnv


class CachedTiles(C.Structure):
    _fields_ = Tiles._fields_ + [('cache', C.c_void_p)]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'), 'POSIX compilers')
class CacheTests(unittest.TestCase):
    def build(self, folder, cc, name, enabled, text, projected=1):
        unit=folder/(name+'.c');unit.write_text(text)
        out=folder/(name+'.so')
        subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-DFZX_ROAD_TILE_EDGE_REUSE=1',
                        '-DFZX_ROAD_TILE_PROJECT_CULL='+str(projected),
                        '-DFZX_ROAD_TILE_VERTEX_CACHE='+str(enabled),
                        '-I'+str(ROOT/'include'), '-I'+str(ROOT/'ref'),
                        str(unit), str(ROOT/'src/fzx_view.c'), str(ROOT/'src/fzx_road_pack.c'),
                        '-o', str(out)], check=True, capture_output=True)
        return C.CDLL(str(out))

    def setup_scene(self, lib, cached):
        mesh,course=synthetic();raw,info=pack(mesh,course,True)
        meta,tex,_=road_tiles.convert(mesh,info['hash'],[255,0,255])
        rb=C.create_string_buffer(raw);mb=C.create_string_buffer(meta);road=Pack()
        T=CachedTiles if cached else Tiles;tiles=T()
        lib.fzx_road_pack_open.argtypes=[C.POINTER(Pack),C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_bool]
        lib.fzx_road_pack_open.restype=C.c_bool
        self.assertTrue(lib.fzx_road_pack_open(C.byref(road),rb,len(raw),info['hash'],info['course_hash'],True))
        lib.fzx_road_tiles_open.argtypes=[C.POINTER(T),C.POINTER(Pack),C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32]
        lib.fzx_road_tiles_open.restype=C.c_bool
        self.assertTrue(lib.fzx_road_tiles_open(C.byref(tiles),C.byref(road),mb,len(meta),len(tex),fnv(meta)))
        lib.fzx_road_tiles_draw.argtypes=[C.POINTER(T),C.POINTER(View),C.POINTER(Plan),C.c_int32,C.c_int32,C.c_uint32,CB,C.c_void_p,C.POINTER(Stats)]
        lib.fzx_road_tiles_draw.restype=C.c_bool
        cache=None;header=None;size=0
        if cached:
            lib.fzx_road_tiles_cache_bytes.argtypes=[C.c_uint32];lib.fzx_road_tiles_cache_bytes.restype=C.c_uint32
            self.assertEqual(lib.fzx_road_tiles_cache_bytes(0),0)
            self.assertEqual(lib.fzx_road_tiles_cache_bytes(0xffffffff),0)
            size=lib.fzx_road_tiles_cache_bytes(4);self.assertEqual(size,16+4*28)
            cache=C.create_string_buffer(bytes([0xa5])*(size+64));address=C.addressof(cache)+32
            header=(C.c_uint32*4).from_address(address)
            lib.fzx_road_tiles_cache_bind.argtypes=[C.POINTER(T),C.c_void_p,C.c_uint32]
            lib.fzx_road_tiles_cache_bind.restype=C.c_bool
            self.assertFalse(lib.fzx_road_tiles_cache_bind(C.byref(tiles),address,size-1))
            self.assertFalse(lib.fzx_road_tiles_cache_bind(C.byref(tiles),address+1,size))
            self.assertFalse(tiles.cache)
            self.assertTrue(lib.fzx_road_tiles_cache_bind(C.byref(tiles),address,size))
        v=View();v.words[0]=v.words[4]=v.words[8]=1<<30
        v.words[12]=v.words[13]=160*65536;v.words[14]=160*65536;v.words[15]=112*65536
        v.words[16]=4096;v.words[17]=10000*4096;v.clip[:]=[12,16,308,224]
        p=Plan();p.count=1;p.visible=2;p.outer=1;p.fog_start=990
        p.fog[:]=[255,0,255,255];p.primitive[:]=[255,0,0,255];p.groups[0]=Group(0,0,1,0)
        return dict(lib=lib,tiles=tiles,view=v,plan=p,cache=cache,header=header,size=size,keep=(road,rb,mb))

    def draw(self, scene, capacity=256, refuse=False):
        rows=[];stats=Stats()
        @CB
        def emit(context, ptr):
            q=ptr.contents
            rows.append((tuple(q.x),tuple(q.y),tuple(q.shade),q.width,q.height,q.offset,q.solid))
            return not refuse
        ok=scene['lib'].fzx_road_tiles_draw(C.byref(scene['tiles']),C.byref(scene['view']),C.byref(scene['plan']),
                                          1<<30,-8192,capacity,emit,None,C.byref(stats))
        if scene['cache'] is not None:
            raw=bytes(scene['cache']);self.assertEqual(raw[:32],bytes([0xa5])*32)
            self.assertEqual(raw[32+scene['size']:32+scene['size']+32],bytes([0xa5])*32)
        return ok,tuple(getattr(stats,n) for n,_ in stats._fields_),rows

    def compare(self, off, on):
        a=self.setup_scene(off,False);b=self.setup_scene(on,True)
        first=self.draw(a);self.assertTrue(first[0]);self.assertGreater(len(first[2]),1)
        self.assertEqual(self.draw(b),first)
        self.assertEqual(list(b['header']),[4,1,4,4])
        # Roll generation back to 1 while stale vertex tags still equal 1.
        # The new view must not reuse them after UINT32_MAX wraps.
        b['header'][1]=0xffffffff
        for s in (a,b):s['view'].words[9]=75*4096
        changed=self.draw(a);self.assertNotEqual(changed,first)
        self.assertEqual(self.draw(b),changed);self.assertEqual(b['header'][1],1)
        for x,z,near,fog in ((-85,0,1,990),(0,-400,1,990),(0,0,200,950),(0,1000,1,990)):
            for s in (a,b):
                s['view'].words[9]=x*4096;s['view'].words[11]=z*4096
                s['view'].words[16]=near*4096;s['plan'].fog_start=fog
            self.assertEqual(self.draw(b),self.draw(a))
        for s in (a,b):s['view'].words[9]=s['view'].words[11]=0;s['view'].words[16]=4096
        for capacity,refuse in ((1,False),(256,True),(256,False)):
            self.assertEqual(self.draw(b,capacity,refuse),self.draw(a,capacity,refuse))
        b['header'][0]=3;self.assertFalse(self.draw(b)[0]);b['header'][0]=4
        self.assertEqual(self.draw(b),self.draw(a))

    def test_compilers_views_wrap_and_controls(self):
        code=(ROOT/'src/fzx_road_tiles.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td)
            for cc in ('gcc','clang'):
                for projected in (0,1):
                    name=cc+str(projected)
                    off=self.build(folder,cc,name+'off',0,code,projected)
                    on=self.build(folder,cc,name+'on',1,code,projected)
                    self.compare(off,on)
            off=self.build(folder,'gcc','control-off',0,code)
            for name,mutant in (
                ('stale',code.replace('++tiles->cache->frame==0','tiles->cache->frame==0')),
                ('wrap',code.replace('memset(tiles->cache->vertices,0,tiles->cache->capacity*sizeof(fzx_road_tile_vertex_t));','')),
                ('alias',code.replace('cache->vertices+id;','cache->vertices+(id&~1u);'))):
                with self.assertRaises(AssertionError):self.compare(off,self.build(folder,'gcc',name,1,mutant))


if __name__=='__main__':unittest.main()
