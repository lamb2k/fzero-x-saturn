"""Native HUD packing checks, using authored words rather than game pixels."""
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from hud_assets import HUD_SHEETS, header


def synthetic_sheets():
    sheets = {}
    for name, width, height in HUD_SHEETS:
        stride = (width + 7) & ~7
        raw = bytearray(stride * height * 2)
        for y in range(height):
            for x in range(width):
                struct.pack_into(">H", raw, (y*stride+x)*2, 0x8000 | ((x+y*width) & 0x7fff))
        sheets[name] = bytes(raw)
    return sheets


class HudAtlasTests(unittest.TestCase):
    def test_header_preserves_every_word_and_labels_provenance(self):
        sheets = synthetic_sheets()
        for fixture in (False, True):
            text = header(sheets, fixture=fixture).decode()
            self.assertIn(f"#define FZX_HUD_FIXTURE {int(fixture)}", text)
            array = text.split("fzx_hud_pixels[] = {", 1)[1].split("};", 1)[0]
            words = [int(h, 16) for h in re.findall(r"0x([0-9a-f]{4})", array)]
            self.assertEqual(struct.pack(f">{len(words)}H", *words), b"".join(sheets.values()))
            self.assertEqual(len(words) * 2, 14080)

    def test_padding_bleed_and_wrong_size_rejected(self):
        sheets = synthetic_sheets()
        name = "aSpeedDigitsTex"
        raw = bytearray(sheets[name])
        raw[12*2] = 0x80  # first pad pixel must be transparent
        sheets[name] = bytes(raw)
        with self.assertRaisesRegex(ValueError, "padding"):
            header(sheets)
        sheets = synthetic_sheets()
        sheets[name] = sheets[name][:-2]
        with self.assertRaisesRegex(ValueError, "size"):
            header(sheets)


if __name__ == "__main__":
    unittest.main()
