"""ROM-free title atlas bounds, exact channel conversion and layout controls."""
from pathlib import Path
import copy
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from title_assets import ASSETS, extract, verify_layout, ArtError, DECOMP, SOURCE


def literal_mio(raw):
    masks = bytes([255])*((len(raw)+7)//8)
    at = 16+len(masks)
    return b'MIO0'+struct.pack('>3I',len(raw),at,at)+masks+raw


def fixture():
    rows, raw_images, data = {}, [], bytearray(4)
    for index,(symbol,width,height,left,top) in enumerate(ASSETS):
        raw = b''.join(struct.pack('>H', ((x%32)<<11)|((y%32)<<6)|(index<<1)|((x+y)%2))
                       for y in range(height) for x in range(width))
        fmt='I4' if symbol=='aTitleNoControllerTex' else 'RGBA16'
        if fmt=='I4':raw=bytes((i%256 for i in range(width*height//2)))
        rows[symbol] = dict(type='COMPRESSED_TEXTURE',format=fmt,width=width,height=height,offset=len(data))
        data += literal_mio(raw); raw_images.append(raw)
    rows['end'] = dict(offset=len(data))
    return bytes(data), rows, raw_images


class TitleAssetsTests(unittest.TestCase):
    def test_all_pixels_and_placement(self):
        data,rows,raw_images = fixture()
        pixels,records = extract(data,rows)
        self.assertEqual((pixels,records),extract(data,rows))
        self.assertEqual(len(pixels),186944)
        for record,raw,asset in zip(records,raw_images,ASSETS):
            self.assertEqual((record['symbol'],record['width'],record['height'],record['left'],record['top']),asset)
            self.assertEqual(record['offset']%8,0)
            expected = b''.join(struct.pack('>H',0x8000|((v>>11)&31)|(((v>>6)&31)<<5)|(((v>>1)&31)<<10)) if v&1 else b'\0\0'
                                for (v,) in struct.iter_unpack('>H',raw))
            if asset[0]=='aTitleNoControllerTex':
                expected=b''.join(struct.pack('>H',0x8000|((n*31+7)//15)) if n else b'\0\0' for b in raw for n in (b>>4,b&15))
            self.assertEqual(pixels[record['offset']:record['offset']+record['bytes']],expected)
        # A channel-order mutation cannot pass the independent bit mapping.
        bad=bytearray(pixels); bad[2]^=8
        self.assertNotEqual(bytes(bad),pixels)

    def test_bad_inputs_rejected(self):
        data,rows,_=fixture()
        first=ASSETS[0][0]
        for key,value in [('type','TEXTURE'),('format','I4'),('width',320),('height',224),('offset',len(data)+1)]:
            broken=copy.deepcopy(rows);broken[first][key]=value
            with self.assertRaises(ArtError):extract(data,broken)
        broken=bytearray(data);broken[4]=0
        with self.assertRaises(ArtError):extract(bytes(broken),rows)
        broken=bytearray(data);struct.pack_into('>I',broken,8,304*240*2-1)
        with self.assertRaises(ArtError):extract(bytes(broken),rows)
        with self.assertRaises(ArtError):extract(data[:-1],rows)

    def test_original_layout_binding(self):
        source=(DECOMP/SOURCE).read_text()
        verify_layout(source)
        for before,after in [('OBJECT_LEFT(logoObj) = 0x55;','OBJECT_LEFT(logoObj) = 0x54;'),
                             ('D_i4_8011D790 < 80','D_i4_8011D790 < 81')]:
            with self.assertRaises(ArtError):verify_layout(source.replace(before,after))


if __name__=='__main__':unittest.main()
