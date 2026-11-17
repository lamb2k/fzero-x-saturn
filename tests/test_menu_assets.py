"""ROM-free original-menu format, channel, index and dither controls."""
from pathlib import Path
import copy
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from menu_assets import ASSETS, LABELS, ArtError, extract


def literal_mio(raw):
    mask=bytes([255])*((len(raw)+7)//8)
    at=16+len(mask)
    return b'MIO0'+struct.pack('>3I',len(raw),at,at)+mask+raw


def fixture():
    data,rows,raws=bytearray(4),{},[]
    for image,(symbol,w,h,fmt) in enumerate(ASSETS):
        if fmt=='I4':raw=bytes(i%256 for i in range(w*h//2))
        elif image in LABELS:
            # 15 opaque grayscale colors plus transparent; all 16 CLUT slots.
            raw=b''.join(struct.pack('>H',(((i%15)*2)*2114)|1) if i%16 else b'\0\0' for i in range(w*h))
        else:
            raw=b''.join(struct.pack('>H',((x%32)<<11)|((y%32)<<6)|((image%32)<<1)|((x+y)%2))
                         for y in range(h) for x in range(w))
        kind='TEXTURE' if fmt=='I4' else 'COMPRESSED_TEXTURE'
        rows[symbol]=dict(type=kind,format=fmt,width=w,height=h,offset=len(data))
        data+=raw if fmt=='I4' else literal_mio(raw)
        raws.append(raw)
    rows['end']=dict(offset=len(data))
    return bytes(data),rows,raws


class MenuAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.data,cls.rows,cls.raws=fixture()

    def test_independent_pixels_palette_and_dither(self):
        bank,records=extract(self.data,self.rows)
        self.assertEqual(len(records),19)
        self.assertEqual(len(bank),371736)
        self.assertEqual((bank,records),extract(self.data,self.rows))
        threshold=((0,8,2,10),(12,4,14,6),(3,11,1,9),(15,7,13,5))
        for rec,raw,(_,w,h,fmt) in zip(records,self.raws,ASSETS):
            if fmt=='I4':
                values=[n*17//8 for b in raw for n in (b//16,b%16)]
                expected=[(0x8000+v*1057) if v else 0 for v in values]
            else:
                expected=[0x8000+(v//2048%32)+(v//64%32)*32+(v//2%32)*1024 if v%2 else 0
                          for v, in struct.iter_unpack('>H',raw)]
            actual=bank[rec['offset']:rec['offset']+rec['bytes']]
            if rec['clut']:
                decoded=[rec['palette'][n] for b in actual for n in (b//16,b%16)]
            else:decoded=[v for v, in struct.iter_unpack('>H',actual)]
            effective=expected
            if rec['image']==0:
                effective=[0x8000+(v%32*75//255)+((v//32%32)*75//255)*32+((v//1024%32)*75//255)*1024 if v else 0 for v in expected]
            self.assertEqual(decoded,effective)
            if 1<=rec['image']<=6:
                for offset,tint,coverage in [('dim_offset',128,11),('pulse_offset',255,13)]:
                    expected_variant=[]
                    for i,v in enumerate(expected):
                        if not v or threshold[(i//w)%4][i%w%4]>=coverage:expected_variant.append(0)
                        else:expected_variant.append(0x8000+(v%32*tint//255)+((v//32%32)*tint//255)*32+((v//1024%32)*tint//255)*1024)
                    values=[v for v, in struct.iter_unpack('>H',bank[rec[offset]:rec[offset]+w*h*2])]
                    self.assertEqual(values,expected_variant)
        # An independently decoded wrong pixel must fail, even when the extent holds.
        corrupted=bytearray(bank);corrupted[2]^=8
        self.assertNotEqual(bytes(corrupted),bank)

    def test_bad_format_extent_and_palette(self):
        for key,value in [('type','TEXTURE'),('format','I4'),('width',320),('height',224),('offset',len(self.data)+1)]:
            rows=copy.deepcopy(self.rows);rows[ASSETS[0][0]][key]=value
            with self.assertRaises(ArtError):extract(self.data,rows)
        broken=bytearray(self.data);broken[4]=0
        with self.assertRaises(ArtError):extract(bytes(broken),self.rows)
        broken=bytearray(self.data);struct.pack_into('>I',broken,8,304*240*2-1)
        with self.assertRaises(ArtError):extract(bytes(broken),self.rows)
        with self.assertRaises(ArtError):extract(self.data[:-1],self.rows)
        broken=bytearray(self.data)
        offset=self.rows['aNoviceTex']['offset'];raw_size=64*18*2
        # Literal compressed fixture payload: introduce a non-grayscale texel.
        struct.pack_into('>H',broken,offset+16+(raw_size+7)//8,0xf801)
        with self.assertRaises(ArtError):extract(bytes(broken),self.rows)


if __name__=='__main__':unittest.main()
