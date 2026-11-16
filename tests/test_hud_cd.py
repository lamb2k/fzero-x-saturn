from pathlib import Path
import re,struct,sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import hud_assets,hud_cd_assets

class HudCdTests(unittest.TestCase):
    def test_same_layout_pixels_and_padding_control(self):
        sheets={}
        for n,(name,width,height) in enumerate(hud_assets.HUD_SHEETS):
            stride=(width+7)&~7
            words=[(0x8000|(n*997+y*13+x)&0x7fff) if x<width else 0
                   for y in range(height) for x in range(stride)]
            sheets[name]=struct.pack('>%dH'%len(words),*words)
        pixels,metadata,embedded=hud_cd_assets.payload(sheets)
        self.assertEqual(len(pixels),14080)
        words=re.findall(rb'0x([0-9a-f]{4})(?:,|\n)',embedded)
        self.assertEqual(pixels,b''.join(struct.pack('>H',int(v,16)) for v in words))
        self.assertEqual(metadata.split(b'#define FZX_HUD_PACK_BYTES')[0],
                         embedded.split(b'static const uint16_t fzx_hud_pixels')[0])
        self.assertNotIn(b'fzx_hud_pixels',metadata)
        bad=dict(sheets);bad['aSpeedDigitsTex']=bad['aSpeedDigitsTex'][:-2]
        with self.assertRaises(ValueError):hud_cd_assets.payload(bad)
        bad=dict(sheets);data=bytearray(bad['aSpeedDigitsTex']);data[24]=1;bad['aSpeedDigitsTex']=bytes(data)
        with self.assertRaises(ValueError):hud_cd_assets.payload(bad)

if __name__=='__main__':unittest.main()
