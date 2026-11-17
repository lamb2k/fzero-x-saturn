"""Records atlas boundaries and original font identities; no retail data in git."""
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import records_assets as assets
from import_source_art import ArtError
from test_results_assets import decode


def literals(raw):
    flags=bytes([255])*((len(raw)+7)//8)
    return b'MIO0'+struct.pack('>3I',len(raw),16+len(flags),16+len(flags))+flags+raw


class RecordsAssets(unittest.TestCase):
    def test_raw_and_compressed_source_extents(self):
        raw=struct.pack('>16H',*range(16))
        packed=literals(raw)
        rows={'glyph':dict(type='COMPRESSED_TEXTURE',format='RGBA16',width=8,height=2,offset=0),
              'next':dict(offset=len(packed))}
        got,offset=assets.common_image(packed,rows,'glyph',8,2,'RGBA16')
        self.assertEqual((got,offset),(raw,0))
        rows['glyph']['type']='TEXTURE'
        rows['next']['offset']=len(raw)
        self.assertEqual(assets.common_image(raw,rows,'glyph',8,2,'RGBA16')[0],raw)
        for data in (raw[:-1],b''):
            with self.assertRaises(ArtError):assets.common_image(data,rows,'glyph',8,2,'RGBA16')
        for change in ({'offset':-1},{'format':'I8'},{'height':3},{'type':'PALETTE'}):
            bad={**rows,'glyph':{**rows['glyph'],**change}}
            with self.assertRaises(ArtError):assets.common_image(raw,bad,'glyph',8,2,'RGBA16')
        rows['glyph']['type']='COMPRESSED_TEXTURE'
        rows['next']['offset']=len(packed)
        with self.assertRaises(ArtError):assets.common_image(b'XXXX'+packed[4:],rows,'glyph',8,2,'RGBA16')

    def test_combined_bank_preserves_alpha_color_and_crop(self):
        sheet=struct.pack('>16H',0,1,0xffff,0x7c1,*([1]*12))
        intensity=bytes([0x12,0x34,0x56,0x78])*2
        class Art:
            def texture(self,symbol):
                return dict(width=8,height=2,format='RGBA16',texels=sheet,family='fixture',offset=0)
        specs=[('sheet%d'%i,8,2,'RGBA16') for i in range(8)]+[('font',8,2,'I4')]
        rows={'font':dict(type='TEXTURE',format='I4',width=8,height=2,offset=0)}
        bank,records=assets.convert(Art(),intensity,rows,dict(specs=specs,crops={'font':3}))
        for record in records[:8]:
            pixels=bank[record['offset']:record['offset']+record['bytes']]
            self.assertEqual(decode(pixels,record)[:3],[0,0x8000,0xffff])
        last=records[-1];pixels=bank[last['offset']:last['offset']+last['bytes']]
        self.assertEqual(last['source_width'],3)
        self.assertEqual(decode(pixels,last)[3:8],[0]*5)
        self.assertTrue(all(r['offset']%8==0 and r['bytes']%8==0 for r in records))

    @unittest.skipUnless(assets.FONT.is_file(),'pinned font source required')
    def test_complete_source_fonts_and_fixed_sheet_ids(self):
        info=assets.catalog()
        self.assertEqual(info['specs'][:8],[(n,w,h,'RGBA16')for n,w,h in assets.results.SHEETS])
        self.assertEqual(info['specs'][8],('aBestTex',16,12,'RGBA16'))
        self.assertEqual({n:len(t['images'])for n,t in info['fonts'].items()},{1:48,3:234,4:235})
        for table in info['fonts'].values():
            self.assertEqual(len(table['images']),len(table['advance']))
            self.assertTrue(all(9<=n<len(info['specs'])for n in table['images']))
        self.assertEqual(len(info['specs']),len({s[0]for s in info['specs']}))
        rows=assets.metadata(assets.COMMON.read_text())
        for symbol in ('aBestTex',info['specs'][info['fonts'][1]['images'][0]][0],
                       info['specs'][info['fonts'][4]['images'][-1]][0]):
            bad={**rows,symbol:{**rows[symbol],'height':255}}
            with self.assertRaises(ArtError):assets.catalog(rows=bad)


if __name__=='__main__':unittest.main()
