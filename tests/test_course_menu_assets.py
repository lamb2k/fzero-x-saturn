"""ROM-free course-menu conversion controls; independent pixels and strides."""
from pathlib import Path
import copy
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from course_menu_assets import ASSETS, ArtError, extract, font_tables, specs_with_fonts


def literal_mio(raw):
    mask=bytes([255])*((len(raw)+7)//8);at=16+len(mask)
    return b'MIO0'+struct.pack('>3I',len(raw),at,at)+mask+raw


def fixture():
    specs=list(ASSETS)+[('aFontTestTex',16,8,'I4')]
    data,rows,raws=bytearray(4),{},[]
    for image,(symbol,w,h,fmt) in enumerate(specs):
        if fmt=='I4':raw=bytes(i%256 for i in range(w*h//2))
        else:
            raw=b''.join(struct.pack('>H',((x%32)<<11)|((y%32)<<6)|((image%32)<<1)|((x+y)%2))
                         for y in range(h) for x in range(w))
        kind='TEXTURE' if fmt=='I4' else 'COMPRESSED_TEXTURE'
        rows[symbol]=dict(type=kind,format=fmt,width=w,height=h,offset=len(data))
        data+=raw if fmt=='I4' else literal_mio(raw);raws.append(raw)
    rows['end']=dict(offset=len(data))
    return bytes(data),rows,raws,specs


class CourseMenuAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.data,cls.rows,cls.raws,cls.specs=fixture()

    def test_independent_pixels_and_native_font_stride(self):
        bank,records=extract(self.data,self.rows,self.specs,{'aFontTestTex':8})
        self.assertEqual((bank,records),extract(self.data,self.rows,self.specs,{'aFontTestTex':8}))
        for rec,raw,(_,w,h,fmt) in zip(records,self.raws,self.specs):
            if fmt=='I4':expected=[0x8000+(n*17//8)*1057 if n else 0 for b in raw for n in (b//16,b%16)]
            else:expected=[0x8000+(v//2048%32)+(v//64%32)*32+(v//2%32)*1024 if v%2 else 0 for v, in struct.iter_unpack('>H',raw)]
            if rec['symbol']=='aFontTestTex':
                expected=[v for y in range(h) for v in expected[y*w:y*w+8]]
                self.assertEqual(rec['width'],8);self.assertEqual(rec['source_width'],16)
            def tint(values,level):
                return [0x8000+(v%32*level//255)+(v//32%32*level//255)*32+(v//1024%32*level//255)*1024 if v else 0 for v in values]
            if rec['image']==0:expected=tint(expected,75)
            chunk=bank[rec['offset']:rec['offset']+rec['bytes']]
            actual=[rec['palette'][n] for b in chunk for n in (b//16,b%16)] if rec['clut'] else [v for v, in struct.iter_unpack('>H',chunk)]
            self.assertEqual(actual,expected)
            if 1<=rec['image']<=7:
                for key,level in [('dim_offset',100),('pulse_offset',180)]:
                    actual=[v for v, in struct.iter_unpack('>H',bank[rec[key]:rec[key]+rec['bytes']])]
                    self.assertEqual(actual,tint(expected,level))
        # Wrong packed-font interpretation loses the source row stride.
        full=[n for b in self.raws[-1] for n in (b//16,b%16)]
        self.assertNotEqual(full[:64],[v for y in range(8) for v in full[y*16:y*16+8]])

    def test_reject_malformed_inputs(self):
        for key,value in [('type','TEXTURE'),('format','I4'),('width',320),('height',224),('offset',len(self.data)+1)]:
            rows=copy.deepcopy(self.rows);rows[ASSETS[0][0]][key]=value
            with self.assertRaises(ArtError):extract(self.data,rows,self.specs)
        broken=bytearray(self.data);broken[4]=0
        with self.assertRaises(ArtError):extract(bytes(broken),self.rows,self.specs)
        with self.assertRaises(ArtError):extract(self.data[:-1],self.rows,self.specs)
        for crop in (0,7,24):
            with self.assertRaises(ArtError):extract(self.data,self.rows,self.specs,{'aFontTestTex':crop})

    def test_source_font_mapping_and_crop_controls(self):
        source='unk_80077D50 sFont3ACompTexInfo[] = COMP_TEX_INFO(4, aFont3ATex, 0);\n'
        source+='unk_80077D50 sFont4ACompTexInfo[] = COMP_TEX_INFO_WH(4, aFont4ATex, 8, TEX_HEIGHT(aFont4ATex), 0);\n'
        for font in (3,4):
            source+='unk_80077D50* sFont%dCompTexInfos[] = {%s};\n'%(font,','.join(['sFont%dACompTexInfo'%font]*74))
            source+='const s16 kFont%dCharWidths[] = {%s};\n'%(font,','.join(['7']*74))
        fonts=font_tables(source)
        self.assertEqual(fonts[4]['crops'],{'aFont4ATex':8});self.assertEqual(len(fonts[3]['symbols']),74)
        for bad in (source.replace('TEX_HEIGHT(aFont4ATex)','TEX_HEIGHT(aFont3ATex)'),
                    source.replace('8, TEX_HEIGHT','16, TEX_HEIGHT'),
                    source.replace('const s16 kFont3CharWidths','const s16 missing')):
            with self.assertRaises(ArtError):font_tables(bad)
        rows={'aFont3ATex':dict(type='TEXTURE',format='I4',width=16,height=16),
              'aFont4ATex':dict(type='TEXTURE',format='I4',width=16,height=8)}
        self.assertEqual(len(specs_with_fonts(fonts,rows)),26)
        rows['aFont3ATex']['format']='RGBA16'
        with self.assertRaises(ArtError):specs_with_fonts(fonts,rows)


if __name__=='__main__':unittest.main()
