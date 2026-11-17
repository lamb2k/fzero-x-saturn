"""Synthetic source-sprite preservation, exact packing and rejection controls."""
from pathlib import Path
import copy
import hashlib
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import records_models as models
from import_source_art import ArtError


def capture():
    identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    view=identity.copy();view[14]=-100
    return dict(model=identity,view=view,projection=[1,1],viewport_scale=[160,120],light=[0,0,0,255,255,255,0,0,127])


def machine():
    return dict(skins=[[255,0,0],[0,255,0],[0,0,255],[255,255,255]],
                geometry=dict(vertices=[dict(position=p,normal=[0,0,127],uv=[0,0]) for p in ([-5,-5,0],[5,-5,0],[0,5,0])],
                              triangles=[dict(vertices=[0,1,2],material=0)],
                              materials=[dict(tile=0,primitive=[255,255,255,255],environment='body',combine=2)]),
                bindings=[dict(symbol='fixture',sampler=dict(s_mode=2,t_mode=2,s_mask=0,t_mask=0,s_shift=0,t_shift=0))])


class RecordsModels(unittest.TestCase):
    def test_original_geometry_body_and_normals_drive_raster(self):
        m=machine();t={'fixture':dict(width=1,height=1,rgba=bytes([255]*4))}
        first=models.raster(m,t,capture(),0)
        self.assertEqual(set(first['pixels']),{0,0x801f})
        second=models.raster(m,t,capture(),1)
        self.assertEqual(set(second['pixels']),{0,0x83e0})
        self.assertEqual([bool(v) for v in first['pixels']],[bool(v) for v in second['pixels']])
        bad=copy.deepcopy(m);bad['geometry']['triangles'][0]['vertices']=[0,2,1]
        with self.assertRaises(ArtError):models.raster(bad,t,capture(),0)
        dark=copy.deepcopy(m)
        for v in dark['geometry']['vertices']:v['normal']=[0,0,-127]
        self.assertEqual(set(models.raster(dark,t,capture(),0)['pixels']),{0,0x8000})
        bad=copy.deepcopy(m);bad['geometry']['triangles'][0]['vertices'][0]=99
        with self.assertRaises(ArtError):models.raster(bad,t,capture(),0)

    def test_uv_and_combiner_are_not_flat_body_replacement(self):
        m=machine();m['geometry']['materials'][0]['combine']=0
        t={'fixture':dict(width=1,height=1,rgba=bytes([0,0,255,255]))}
        self.assertEqual(set(models.raster(m,t,capture(),0)['pixels']),{0,0xfc00})
        t['fixture']['rgba']=bytes([0,0,255,0])
        self.assertEqual(set(models.raster(m,t,capture(),0)['pixels']),{0,0x801f})
        m['bindings'][0]['sampler']['s_shift']=1
        with self.assertRaises(ArtError):models.raster(m,t,capture(),0)

    def test_crop_padding_transparency_and_exact_clut(self):
        source=dict(x=-3,y=-2,width=10,height=3,pixels=[0]*30)
        source['pixels'][11:14]=[0x8000,0x801f,0xffff]
        out=models.encode(source)
        self.assertEqual((out['x'],out['y'],out['width'],out['height']),(-2,-1,8,1))
        # 16RGB bytes are smaller than a32-byte CLUT plus4pixels, so RGB wins.
        self.assertEqual(out['format'],5)
        self.assertEqual(struct.unpack('>8H',out['pixels']),(0x8000,0x801f,0xffff,0,0,0,0,0))
        source=dict(x=0,y=0,width=8,height=16,pixels=[0x801f,0x8000,0,0]*32)
        out=models.encode(source);self.assertEqual(out['format'],1)
        palette=struct.unpack('>16H',out['palette']);decoded=[]
        for byte in out['pixels']:decoded.extend([palette[byte>>4],palette[byte&15]])
        expected=[source['pixels'][y*8+x] if x<6 else 0 for y in range(16) for x in range(8)]
        self.assertEqual(decoded,expected)
        self.assertEqual(palette[0],0);self.assertIn(0x8000,palette)

    def test_full_inventory_hash_and_structural_corruptions_reject(self):
        sprite=dict(x=-2,y=-1,width=8,height=2,pixels=[0x8000+i for i in range(16)])
        rows=[dict(character=c,skin=s,body=[c,s,255],sprite=sprite) for c in range(30) for s in range(4)]
        packed,entries=models.pack(rows)
        self.assertTrue(models.validate_pack(packed,hashlib.sha256(packed).hexdigest()))
        self.assertEqual(models.select(entries,29,0,[29,3,255])['skin'],3)
        for custom,char,body in ((1,0,[0,0,255]),(0,30,[0,0,255]),(0,0,[1,2,3])):
            with self.assertRaises(ArtError):models.select(entries,char,custom,body)
        for offset in (0,12,24,32,36,44,48,len(packed)-1):
            bad=bytearray(packed);bad[offset]^=1
            with self.assertRaises(ArtError):models.validate_pack(bytes(bad))
        for bad in (packed[:-1],packed+b'\0',packed[:20]):
            with self.assertRaises(ArtError):models.validate_pack(bad)
        with self.assertRaises(ArtError):models.pack(rows[:-1])

    def test_unrecognized_capture_and_bounds_fail(self):
        for key,value in (('model',[0]*16),('projection',[float('nan'),1]),('viewport_scale',[80,60]),('light',[0]*9)):
            c=capture();c[key]=value
            with self.assertRaises(ArtError):models.raster(machine(),{'fixture':dict(width=1,height=1,rgba=bytes(4))},c,0)
        m=machine();m['geometry']['vertices'][0]['position']=[10000,0,0]
        with self.assertRaises(ArtError):models.raster(m,{'fixture':dict(width=1,height=1,rgba=bytes(4))},capture(),0)

    @unittest.skipUnless((ROOT/'tools/records_draw_source.py').exists(),'Records source generator required')
    def test_capture_receipt_missing_stale_and_wrong_owner_controls(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'capture.json';receipt=Path(directory)/'capture.receipt'
            path.write_text(json.dumps(capture()))
            generator=ROOT/'tools/records_draw_source.py'
            lines=['# fzx generation receipt v1; regenerate, never edit',
                   'generator %s records_draw_source.py'%models.sha(generator.read_bytes()),
                   'output %s capture.json'%models.sha(path.read_bytes())]
            receipt.write_text('\n'.join(lines)+'\n')
            result,deps=models.read_capture(path,receipt)
            self.assertEqual(result,capture());self.assertIn(path.resolve(),deps)
            path.write_text(json.dumps({**capture(),'light':[255]*9}))
            with self.assertRaises(ArtError):models.read_capture(path,receipt)
            path.write_text(json.dumps(capture()))
            for bad in (lines[:1]+lines[2:],lines[:2],['wrong']+lines[1:],
                        [lines[0],lines[1].replace(models.sha(generator.read_bytes()),'0'*64),lines[2]]):
                receipt.write_text('\n'.join(bad)+'\n')
                with self.assertRaises(ArtError):models.read_capture(path,receipt)


if __name__=='__main__':unittest.main()
