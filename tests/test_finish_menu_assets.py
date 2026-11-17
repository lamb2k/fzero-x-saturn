"""Synthetic finish-menu controls plus pinned source-only census checks."""
from pathlib import Path
import copy
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import finish_menu_assets as assets
from import_source_art import ArtError


def literals(raw):
    flags=bytes([255])*((len(raw)+7)//8)
    start=16+len(flags)
    return b'MIO0'+struct.pack('>3I',len(raw),start,start)+flags+raw


def fixture():
    palette=struct.pack('>32H',0,*((n<<11)|(n<<6)|(n<<1)|1 for n in range(1,32)))
    indices=bytes(range(32))*2
    label=literals(indices)
    font=bytes(range(16))*2
    compressed=literals(font)
    rows={assets.PALETTE:dict(offset=0),
          'label':dict(offset=64,type='COMPRESSED_TEXTURE',compression='MIO0',format='CI8',width=16,height=4),
          'font':dict(offset=64+len(label),type='TEXTURE',format='I4',width=16,height=4),
          'compressed':dict(offset=64+len(label)+len(font),type='COMPRESSED_TEXTURE',compression='MIO0',format='I4',width=16,height=4)}
    info=dict(specs=[('label',16,4,'CI8'),('font',16,4,'I4'),('compressed',16,4,'I4')],crops={'font':7},
              labels=[0],font=dict(images=[1,2,1],advance=[6,12,6],space=3,baseline=16))
    return palette+label+font+compressed,rows,info,indices,font


def decode(bank,row):
    raw=bank[row['offset']:row['offset']+row['bytes']]
    indices=raw if row['bpp']==8 else [n for b in raw for n in (b>>4,b&15)]
    return [row['palette'][i] for i in indices]


class FinishMenuAssets(unittest.TestCase):
    def test_yaml_tlut_colors_and_compression_survive(self):
        rows=assets.asset_rows('p:\n  type: TEXTURE\n  format: TLUT\n  offset: 0x20\n  colors: 32\nt:\n  type: COMPRESSED_TEXTURE\n  format: CI8\n  tlut: 0xF000020\n  compression: MIO0\n')
        self.assertEqual(rows['p']['colors'],32)
        self.assertEqual(rows['t']['tlut'],0xF000020)
        self.assertEqual(rows['t']['compression'],'MIO0')

    def test_full_32_color_indices_alpha_compressed_i4_and_crop(self):
        data,rows,info,indices,font=fixture()
        bank,records,palhash=assets.convert(data,rows,info)
        self.assertEqual(bank[:64],indices)
        self.assertEqual(decode(bank,records[0]),[0 if i==0 else 0x8000|i*1057 for i in indices])
        expected=[0 if n==0 else 0x8000|(n*17>>3)*1057 for b in font for n in (b>>4,b&15)]
        self.assertEqual(decode(bank,records[2]),expected)
        cropped=decode(bank,records[1])
        for y in range(4):
            self.assertEqual(cropped[y*8:y*8+7],expected[y*16:y*16+7])
            self.assertEqual(cropped[y*8+7],0)
        self.assertEqual(records[1]['source_width'],7)
        self.assertEqual(records[1]['original_width'],16)
        self.assertTrue(all(r['offset']%8==0 and r['bytes']%8==0 for r in records))
        self.assertEqual([r['bpp'] for r in records],[8,4,4])
        self.assertEqual(len(palhash),64)

    def test_source_palette_colors_are_not_assumed_grayscale(self):
        data,rows,info,_,_=fixture()
        palette=[0]+[((n*3&31)<<11)|((n*7&31)<<6)|((n*11&31)<<1)|1 for n in range(1,32)]
        data=struct.pack('>32H',*palette)+data[64:]
        bank,records,_=assets.convert(data,rows,info)
        expected=[0 if not p&1 else 0x8000|((p>>11)&31)|(((p>>6)&31)<<5)|(((p>>1)&31)<<10) for p in palette]
        self.assertEqual(decode(bank,records[0]),expected*2)

    def test_every_tint_channel_value_preserves_transparency_and_rgb5(self):
        palette=[0]+[0x8000|n|(n<<5)|(n<<10) for n in range(1,32)]
        for value in range(256):
            rgb=(value,255-value,(value*17)%256)
            result=assets.modulate_palette(palette,*rgb)
            self.assertEqual(result[0],0)
            for n,word in enumerate(result[1:],1):
                self.assertEqual(word,0x8000|(n*rgb[0]//255)|((n*rgb[1]//255)<<5)|((n*rgb[2]//255)<<10))
        self.assertEqual(assets.modulate_palette(palette,255,255,255),palette)
        self.assertEqual(assets.modulate_palette([0x8000],0,0,0),[0x8000])
        for rgb in ((-1,0,0),(0,256,0),(0,0,0.5)):
            with self.assertRaises(ArtError):assets.modulate_palette(palette,*rgb)
        with self.assertRaises(ArtError):assets.modulate_palette(palette+[0],0,0,0)

    def test_bad_index_palette_alpha_and_source_stream_rejected(self):
        data,rows,info,_,_=fixture()
        bad=bytearray(data);bad[64+16+8]=32
        with self.assertRaisesRegex(ArtError,'index exceeds'):assets.convert(bad,rows,info)
        for pos in (0,2):
            bad=bytearray(data)
            struct.pack_into('>H',bad,pos,1 if pos==0 else 0)
            with self.assertRaisesRegex(ArtError,'transparent index'):assets.convert(bad,rows,info)
        with self.assertRaises(ArtError):assets.convert(data[:-1],rows,info)
        bad=bytearray(data);bad[64:68]=b'FAIL'
        with self.assertRaises(ArtError):assets.convert(bad,rows,info)
        for change in ({'offset':-1},{'format':'I8'},{'height':5},{'compression':'OTHER'},{'type':'PALETTE'}):
            altered=copy.deepcopy(rows);altered['label'].update(change)
            with self.assertRaises(ArtError):assets.convert(data,altered,info)

    def test_header_explicit_formats_and_index_tables(self):
        data,rows,info,_,_=fixture()
        bank,records,_=assets.convert(data,rows,info)
        header=assets.identity_header(bank,records,info).decode()
        self.assertIn('#define FZX_FINISH_MENU_IMAGES 3u',header)
        self.assertIn('width,height,source_width,bpp,palette_count,reserved',header)
        self.assertIn('{1,6},{2,12},{1,6}',header)
        self.assertIn('16u,4u,16u,8u,32u,0',header)

    @unittest.skipUnless(assets.FONT.is_file(),'pinned source required')
    def test_full_source_catalog_and_adversarial_metadata(self):
        info=assets.catalog()
        self.assertEqual((len(info['labels']),len(info['font']['images']),len(info['specs'])),(24,235,133))
        self.assertEqual(info['labels'],list(range(24)))
        self.assertEqual(info['specs'][info['font']['images'][220]][0],'aFont5SideArrowTex')
        self.assertEqual(info['font']['advance'][220],14)
        self.assertEqual((info['font']['space'],info['font']['baseline']),(3,16))
        self.assertTrue(any(s[0]=='aFont5SelectCourseTex' for s in info['specs']))
        self.assertEqual(info['crops']['aFont4ATex'],8)
        rows=assets.asset_rows(assets.COMMON.read_text())
        for symbol,change in [(assets.PALETTE,{'colors':16}),('aMenuRetryTex',{'tlut':0}),
                              ('aMenuRetryTex',{'compression':'NONE'}),('aMenuRetryTex',{'width':129}),
                              ('aFont5PointsTex',{'type':'TEXTURE'}),('aFont5Num0Tex',{'format':'I8'})]:
            altered=copy.deepcopy(rows);altered[symbol].update(change)
            with self.subTest(symbol=symbol,change=change),self.assertRaises(ArtError):assets.catalog(rows=altered)
        font=assets.FONT.read_text().replace('COMP_TEX_INFO_DEF(20, aFont5PointsTex)','COMP_TEX_INFO_DEF(18, aFont5PointsTex)')
        with self.assertRaisesRegex(ArtError,'loader changed'):assets.catalog(font_source=font)
        menus=assets.MENUS.read_text().replace('aMenuRetryTex,                 // RACE_MENU_RETRY','aMenuSettingsTex,                 // RACE_MENU_RETRY')
        with self.assertRaisesRegex(ArtError,'label extent'):assets.catalog(menu_source=menus)


if __name__=='__main__':unittest.main()
