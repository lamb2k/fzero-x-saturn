"""Backdrop decoder/binding gates; every test texture is synthetic."""
from pathlib import Path
import copy
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import backdrop_assets as bg


class DecoderTests(unittest.TestCase):
    def test_every_ia8_value_and_fractional_alpha(self):
        data = bytes(range(256))
        rgba = bg.decode_ia8(data, 16, 16)
        for value in range(256):
            intensity, alpha = divmod(value, 16)
            self.assertEqual(rgba[value*4:value*4+4], bytes([intensity*17]*3+[alpha*17]))
        self.assertEqual(len(set(rgba[3::4])), 16)
        # A binary-alpha substitution is an observable fidelity failure.
        changed = bytearray(rgba)
        changed[3::4] = bytes(255 if a else 0 for a in rgba[3::4])
        self.assertNotEqual(changed, rgba)
        with self.assertRaises(bg.ArtError):
            bg.rgb1555(rgba, 16, 16)

    def test_index_order_palette_endian_and_odd_row_padding(self):
        words = [(i << 11) | ((15-i) << 6) | ((i*2) << 1) | (i&1) for i in range(16)]
        palette = struct.pack('>16H', *words)
        data = bytes([0x12, 0x3F, 0x45, 0x6E])
        result = bg.decode_ci4(data, palette, 3, 2)
        # The unused low nibble in the second byte of each row is padding.
        expected = bytes(c for i in range(1, 7) for c in
                         ((words[i]>>11)*255//31, ((words[i]>>6)&31)*255//31,
                          ((words[i]>>1)&31)*255//31, (words[i]&1)*255))
        self.assertEqual(result, expected)
        swapped = b''.join(palette[i:i+2][::-1] for i in range(0, 32, 2))
        self.assertNotEqual(bg.decode_ci4(data, swapped, 3, 2), expected)
        self.assertNotEqual(bg.decode_ci4(bytes((v>>4)|((v&15)<<4) for v in data), palette, 3, 2), expected)
        for bad, pal, w, h in [(data[:-1], palette, 3, 2), (data, palette[:-2], 3, 2), (b'', palette, 0, 2)]:
            with self.assertRaises(bg.ArtError):
                bg.decode_ci4(bad, pal, w, h)

    def test_metadata_and_extent_controls(self):
        text = 'synthetic:\n  type: TEXTURE\n  symbol: synthetic\n  format: CI4\n  width: 4\n  height: 2\n  offset: 0x10\n  tlut: 0xF000020\n'
        rows = bg.catalog(text)
        self.assertEqual(rows['synthetic']['tlut'], 0xF000020)
        with self.assertRaises(bg.ArtError):
            bg.catalog(text+text)
        with mock.patch.object(bg, 'ROM_BASE', 0), mock.patch.object(bg, 'ROM_END', 64):
            record, data = bg.read_asset(bytes(range(64)), rows, 'synthetic')
            self.assertEqual(data, bytes(range(16, 20)))
            self.assertEqual(record['source_bytes'], 4)
            for mutate in ('negative', 'oversize', 'next', 'compressed', 'palette'):
                changed = copy.deepcopy(rows)
                if mutate == 'negative': changed['synthetic']['offset'] = -1
                if mutate == 'oversize': changed['synthetic']['width'] = 128
                if mutate == 'next': changed['overlap'] = {'offset': 18}
                if mutate == 'compressed': changed['synthetic']['type'] = 'COMPRESSED_TEXTURE'
                if mutate == 'palette': changed['synthetic'].update(format='TLUT', colors=256)
                with self.subTest(control=mutate), self.assertRaises(bg.ArtError):
                    bg.read_asset(bytes(range(64)), changed, 'synthetic')


@unittest.skipUnless((bg.DECOMP/bg.SOURCE).is_file(), 'pinned source submodule required')
class SourceBindingTests(unittest.TestCase):
    def setUp(self):
        self.source = (bg.DECOMP/bg.SOURCE).read_text()
        self.header = (bg.DECOMP/bg.HEADER).read_text()
        self.course = bytes(0x7E0)

    def test_source_selection_and_mutation_controls(self):
        result = bg.bindings(self.source, self.header, self.course)
        self.assertEqual([s['angle_degrees'] for s in result['sprites']], [0,60,120,150,240,300])
        self.assertEqual(len({s['texture'] for s in result['sprites']}), 4)
        self.assertEqual(result['sky']['sprite_rgb'], [200,130,240])
        self.assertEqual(result['sky']['racer_fog_override'], [0,0,0])
        self.assertEqual(result['sky']['racer_fog'], [252,192,253])
        self.assertEqual(result['floor']['scale'], [0.03,0.03])
        for original, replacement in (
            ('sBackgroundCtx.venueFloor = sCourseVenueFloors[gVenueType];', '/* no binding */'),
            ('sBackgroundCtx.skybox = sCourseSkyboxes[gSkyboxType];', '/* no sky */'),
            ('aSkyboxPurpleTex, 252', 'wrongTexture, 252'),
            ('sBackgroundSpriteInitMuteCity,', 'wrongCourseList,'),
            ('{ D_F220810, D_F221010 },', '{ D_F220810 },'),
            ('BG_SPRITE_END, 0.0f', 'BG_SPRITE_END, 1.0f'),
        ):
            changed = self.source.replace(original, replacement)
            self.assertNotEqual(changed, self.source)
            with self.subTest(control=original), self.assertRaises((ValueError, bg.ArtError)):
                bg.bindings(changed, self.header, self.course)
        course = bytearray(self.course); course[3] = 4
        with self.assertRaises(bg.ArtError): bg.bindings(self.source, self.header, course)
        # Table comments cannot change the program's numeric enum binding.
        self.assertEqual(bg.bindings(self.source.replace('// BG_SPRITE_CITY_SKYLINE_1', '// unrelated label'),
                                     self.header, self.course), result)
        with self.assertRaises(bg.ArtError):
            bg.bindings(self.source, self.header.replace('BG_SPRITE_END,', 'BG_SPRITE_END = 2,'), self.course)
        with self.assertRaises(bg.ArtError):
            bg.bindings(self.source, self.header.replace('f32 xScale;', 'f32 wrongScale;'), self.course)

    def test_complete_synthetic_export_and_palette_pointer_control(self):
        synthetic = bytes((i*29+7)&255 for i in range(256)) * ((bg.ROM_END+255)//256)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            with mock.patch.object(bg, 'output_location'), mock.patch.object(bg, 'verify_source'), \
                 mock.patch.object(bg, 'extract_rom_course', return_value=(self.course, {'normalized_rom_sha256':bg.sha(synthetic)})), \
                 mock.patch.object(bg, 'normalize_rom', return_value=(synthetic, 'synthetic')):
                report = bg.export(synthetic, out)
                self.assertEqual(report, dict(assets=11, skyline_instances=6, source_bytes=14592,
                                             native_rgb_bytes=37120, cloud_alpha_levels=16))
                first = (out/'backdrop.json').read_bytes()
                receipt = (out/'backdrop.receipt').read_bytes()
                bg.export(synthetic, out)
                self.assertEqual((out/'backdrop.json').read_bytes(), first)
                self.assertEqual((out/'backdrop.receipt').read_bytes(), receipt)
                manifest = json.loads(first)
                self.assertEqual(len(manifest['assets']), 11)
                for asset in manifest['assets']:
                    self.assertEqual(bg.sha((out/asset['raw_file']).read_bytes()), asset['source_sha256'])
                    self.assertEqual(bg.sha((out/asset['rgba_file']).read_bytes()), asset['rgba_sha256'])
                self.assertFalse((out/'aCloudTex.rgb1555').exists())
                if os.name != 'nt' and shutil.which('sh'):
                    check = ['sh', str(ROOT/'tools/check_receipt.sh'), str(out/'backdrop.receipt')]
                    self.assertEqual(subprocess.run(check, capture_output=True).returncode, 0)
                    target = out/'aCloudTex.rgba'; original_bytes = target.read_bytes()
                    target.write_bytes(bytes([original_bytes[0]^1])+original_bytes[1:])
                    self.assertNotEqual(subprocess.run(check, capture_output=True).returncode, 0)
                    target.write_bytes(original_bytes)
                original = bg.catalog
                def wrong_palette(text):
                    rows = original(text);rows['D_F220810']['tlut'] ^= 0x10;return rows
                with mock.patch.object(bg, 'catalog', side_effect=wrong_palette), self.assertRaises(bg.ArtError):
                    bg.export(synthetic, out)

    @unittest.skipUnless((ROOT/'upstream/G-Diffuser/tools/n64_texels.py').is_file(), 'source visual oracle required')
    def test_source_oracle_all_ia8_values_and_ci4_indices(self):
        spec = importlib.util.spec_from_file_location('backdrop_texel_oracle', ROOT/'upstream/G-Diffuser/tools/n64_texels.py')
        oracle = importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
        raw = bytes(range(256))
        self.assertEqual(bg.decode_ia8(raw, 16, 16), oracle.decode('IA8', raw, 16, 16))
        palette = bytes((i*61+9)&255 for i in range(32))
        self.assertEqual(bg.decode_ci4(raw, palette, 32, 16), oracle.DECODERS['CI4'](raw, 32, 16, palette))


if __name__ == '__main__':
    unittest.main()
