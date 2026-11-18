"""HOST checks of the one menu-table layout (tools/menu_tables_layout.py) and its consumers."""
import re
import unittest
from pathlib import Path

import menu_tables_layout as L

ROOT = Path(__file__).resolve().parents[1]
SIZES = {'fzx_machine_menu_images': 11568, 'fzx_finish_menu_images': 11172,
         'fzx_records_images': 10992, 'fzx_course_menu_images': 10348}


class MenuTablesLayoutTests(unittest.TestCase):
    def test_plan_r4_addresses(self):
        lay = L.layout(SIZES)
        self.assertEqual([t['address'] for t in lay['tables']], [0x200040, 0x202DC0, 0x2059C0, 0x208500])
        self.assertEqual(lay['guards'], [0x0, 0x2D80, 0x5980, 0x84C0, 0xAD80])
        self.assertEqual((lay['pack_bytes'], lay['spare_bytes']), (0xADC0, 576))
        self.assertEqual(L.STAND_IN_BASE, 0x20B000)
        for t in lay['tables']:
            self.assertEqual(t['offset'] % L.ALIGN, 0)
            self.assertLessEqual(t['bytes'], t['padded'])

    def test_rejects(self):
        for bad in ({**SIZES, 'fzx_records_images': 10994}, {**SIZES, 'fzx_course_menu_images': 0},
                    {**SIZES, 'fzx_course_menu_images': 10348 + 1024}):
            with self.assertRaises(L.LayoutError):
                L.layout(bad)
        with self.assertRaises(L.LayoutError):
            L.layout({k: v for k, v in SIZES.items() if k != 'fzx_records_images'})

    def test_pack_bytes_guards_and_padding(self):
        lay = L.layout(SIZES)
        payloads = {s: bytes([i + 1]) * n for i, (s, n) in enumerate(SIZES.items())}
        pack = L.build_pack(payloads, lay)
        self.assertEqual(len(pack), lay['pack_bytes'])
        slot = (0x4D544731).to_bytes(4, 'big') * 8 + bytes(32)
        for g in lay['guards']:
            self.assertEqual(pack[g:g + 64], slot)
        for t in lay['tables']:
            self.assertEqual(pack[t['offset']:t['offset'] + t['bytes']], payloads[t['symbol']])
            self.assertEqual(pack[t['offset'] + t['bytes']:t['offset'] + t['padded']], bytes(t['padded'] - t['bytes']))
        with self.assertRaises(L.LayoutError):
            L.build_pack({**payloads, 'fzx_records_images': b'\0' * 8}, lay)
        self.assertEqual(L.fnv(b''), 2166136261)
        self.assertEqual(L.fnv(b'a'), 0xE40C292C)

    def test_consumers_keep_off_path(self):
        """Knob off, each consumer still includes its generated header unchanged; knob on, it reads the fixed address."""
        for f, key, sym in (('machine_menu_native.c', 'MACHINE', 'fzx_machine_menu_images'),
                            ('course_menu_native.c', 'COURSE', 'fzx_course_menu_images'),
                            ('finish_menu_native.c', 'FINISH', 'fzx_finish_menu_images'),
                            ('records_native.c', 'RECORDS', 'fzx_records_images')):
            text = (ROOT / 'saturn' / f).read_text()
            self.assertEqual(len(re.findall(r'^#if FZX_MENU_TABLES_LOW', text, re.M)), 2, f)
            self.assertIn('FZX_MENU_TABLE(%s_rom_layout,FZX_MENU_TABLES_%s_ADDR)' % (sym, key), text)
            self.assertIn('sizeof(%s_rom_layout)==FZX_MENU_TABLES_%s_BYTES' % (sym, key), text)


if __name__ == '__main__':
    unittest.main()
