"""Original results atlas: pixel preservation, stride and malformed input gates."""
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import results_assets as assets
from import_source_art import ArtError


def decode(pixels,record):
    if record['clut']:
        return [record['palette'][v] for byte in pixels for v in (byte>>4,byte&15)]
    return list(struct.unpack('>%dH'%(len(pixels)//2),pixels))


class ResultsAssets(unittest.TestCase):
    def test_offline_record_pulse_error_and_alpha(self):
        raw=struct.pack('>32H',*(0x8000|v|(v<<5)|(v<<10) for v in range(32)))+bytes(64)
        records=[dict(offset=0,bytes=128,width=32,height=2,clut=False)]*8
        tints,stride,offsets=assets.tint_banks(raw,records)
        self.assertEqual((stride,len(tints)),(384,6144))
        for shade in range(256):
            level=(shade*15+127)//255
            for i in range(32):
                value=struct.unpack_from('>H',tints,level*stride+offsets[3]+i*2)[0]
                self.assertEqual(value&0x801f,0x8000|i)
                self.assertLessEqual(abs(((value>>5)&31)-i*shade//255),1)
                self.assertEqual((value>>5)&31,(value>>10)&31)
            self.assertEqual(tints[level*stride+64:level*stride+128],bytes(64))
    def test_all_rgb5_colors_and_alpha_survive(self):
        # Every RGB5 triple, with both source alpha values. Width12 also tests
        # transparent row padding in the format used by speed digits.
        original=list(range(65536))
        for start in range(0,len(original),12*16):
            words=original[start:start+12*16]
            words += [0]*(12*16-len(words))
            pixels,record=assets.encode_image(struct.pack('>192H',*words),'RGBA16',12,16)
            decoded=decode(pixels,record)
            for y in range(16):
                for x in range(12):
                    source=words[y*12+x]
                    expected=0 if not source&1 else 0x8000|((source>>11)&31)|(((source>>6)&31)<<5)|(((source>>1)&31)<<10)
                    self.assertEqual(decoded[y*16+x],expected)
                self.assertEqual(decoded[y*16+12:y*16+16],[0]*4)

    def test_clut_opaque_black_transparency_and_intensity(self):
        pixels,record=assets.encode_image(bytes(range(256)),'I4',32,16)
        self.assertTrue(record['clut'])
        self.assertEqual(len(record['palette']),16)
        expected=[0 if n==0 else 0x8000|(n*17>>3)*1057 for b in range(256) for n in (b>>4,b&15)]
        self.assertEqual(decode(pixels,record),expected)
        raw=struct.pack('>8H',0,1,0xffff,1,0,1,0xffff,1)*2
        pixels,record=assets.encode_image(raw,'RGBA16',8,2)
        self.assertEqual(decode(pixels,record)[:3],[0,0x8000,0xffff])

    def test_rgb_fallback_and_crop(self):
        words=[(n<<1)|1 for n in range(32)]*4
        pixels,record=assets.encode_image(struct.pack('>128H',*words),'RGBA16',32,4)
        self.assertFalse(record['clut'])
        self.assertEqual(record['palette'],[])
        pixels,record=assets.encode_image(bytes([0x12,0x34,0x56,0x78])*8,'I4',8,8,3)
        self.assertEqual(record['source_width'],3)
        self.assertEqual(record['width'],8)
        self.assertEqual(decode(pixels,record)[3:8],[0]*5)

    def test_bad_dimensions_format_length_crop_rejected(self):
        for args in [(b'', 'RGBA16',0,16), (b'', 'RGBA16',8,256),
                     (b'', 'IA16',8,16), (bytes(255),'RGBA16',8,16),
                     (bytes(64),'I4',8,16,9), (bytes(64),'I4',8,16,0),
                     (bytes(4),'I4',3,3)]:
            with self.subTest(args=args[1:]),self.assertRaises(ArtError):
                assets.encode_image(*args)

    @unittest.skipUnless(assets.FONT.is_file(),'pinned font source required')
    def test_original_sheet_ids_and_complete_font_mapping(self):
        info=assets.catalog()
        self.assertEqual(info['specs'][:8],[(n,w,h,'RGBA16')for n,w,h in assets.SHEETS])
        self.assertEqual(len(info['font']['images']),len(info['font']['advance']))
        self.assertTrue(all(8<=i<len(info['specs']) for i in info['font']['images']))
        self.assertEqual(len({r[0]for r in info['specs']}),len(info['specs']))
        rows=assets.metadata(assets.COMMON.read_text())
        first=info['specs'][8][0]
        rows[first]=dict(rows[first],height=17)
        with self.assertRaises(ArtError):assets.catalog(rows=rows)


if __name__=='__main__':unittest.main()
