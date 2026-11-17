"""ROM-free machine artwork conversion, source-table and residency controls."""
from pathlib import Path
import copy
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from machine_menu_assets import ArtError, SOURCE, census, extract, resident_banks, identity_header


def literal_mio(raw):
    mask = bytes([255])*((len(raw)+7)//8)
    offset = 16+len(mask)
    return b'MIO0'+struct.pack('>3I', len(raw), offset, offset)+mask+raw


def fixture():
    specs = [('aWideTestTex', 180, 3, 'RGBA16'), ('aLabelTestTex', 20, 8, 'RGBA16'),
             ('aHeaderTestTex', 8, 3, 'I4'), ('aOpaqueTestTex', 16, 8, 'RGBA16')]
    data, rows, raws = bytearray(4), {}, []
    for image, (symbol, w, h, fmt) in enumerate(specs):
        if fmt == 'I4':
            raw = bytes(i*23 % 256 for i in range(w*h//2))
        elif image == 1:
            raw = b''.join(struct.pack('>H', (i % 15)*2*2114 | 1) if i % 16 else b'\0\0'
                           for i in range(w*h))
        else:
            raw = b''.join(struct.pack('>H', (x % 32)*2048+(y % 32)*64+((x+7) % 32)*2+((x+y) % 2))
                           for y in range(h) for x in range(w))
        rows[symbol] = dict(type='TEXTURE' if fmt == 'I4' else 'COMPRESSED_TEXTURE',
                            format=fmt, width=w, height=h, offset=len(data))
        data += raw if fmt == 'I4' else literal_mio(raw)
        raws.append(raw)
    rows['end'] = dict(offset=len(data))
    return bytes(data), rows, raws, specs


def decode_native(payload, record):
    chunk = payload[record['offset']:record['offset']+record['bytes']]
    if record['clut']:
        return [record['palette'][index] for byte in chunk for index in (byte//16, byte % 16)]
    return [value for value, in struct.iter_unpack('>H', chunk)]


def expected_pixels(raw, spec):
    _, width, height, fmt = spec
    if fmt == 'I4':
        values = [0x8000+(n*17//8)*1057 if n else 0 for b in raw for n in (b//16, b % 16)]
    else:
        values = [0x8000+v//2048 % 32+(v//64 % 32)*32+(v//2 % 32)*1024 if v % 2 else 0
                  for v, in struct.iter_unpack('>H', raw)]
    stride = (width+7)//8*8
    return [pixel for row in range(height) for pixel in values[row*width:(row+1)*width]+[0]*(stride-width)]


def source_fixture():
    source = 'unk_80077D50 sHeaderCompTexInfo[] = COMP_TEX_INFO(4, aHeaderTex, 0);\n'
    source += 'unk_80077D50* sHeaderCompTexInfos[] = {sHeaderCompTexInfo};\n'
    source += 'unk_800792D8 D_Still[] = {{sHeaderCompTexInfo,60},{0}};\n'
    source += 'unk_800792D8* D_StillTable[] = {D_Still,D_Still};\n'
    fonts = ''
    rows = {'aHeaderTex': dict(type='TEXTURE', format='I4', width=160, height=24)}
    for number in (1, 2):
        fonts += 'unk_80077D50 sFont%dACompTexInfo[] = COMP_TEX_INFO_DEF(17, aFont%dATex);\n'%(number, number)
        fonts += 'unk_80077D50* sFont%dCompTexInfos[] = {%s};\n'%(number, ','.join(['sFont%dACompTexInfo'%number]*48))
        fonts += 'const s16 kFont%dCharWidths[] = {%s};\n'%(number, ','.join(['7']*48))
        rows['aFont%dATex'%number] = dict(type='COMPRESSED_TEXTURE', format='RGBA16', width=16, height=16)
    fonts += 'unk_80077D50 sStatCompTexInfo[] = COMP_TEX_INFO_DEF(17, aStatTex);\n'
    fonts += 'unk_80077D50* sMachineStatValueCompTexInfos[] = {%s};\n'%','.join(['sStatCompTexInfo']*5)
    fonts += 'unk_80077D50* sMachineWeightDigitCompTexInfos[] = {%s};\n'%','.join(['sStatCompTexInfo']*10)
    fonts += 'const s16 kMachineWeightDigitWidths[] = {%s};\n'%','.join(['9']*10)
    rows['aStatTex'] = dict(type='COMPRESSED_TEXTURE', format='RGBA16', width=16, height=16)
    return source, fonts, rows


def residency_fixture():
    records, atlas = [], bytearray()
    for image in range(66):
        size = (image % 4+1)*8
        records.append(dict(image=image, symbol='aImage%dTex'%image, offset=len(atlas), bytes=size,
                            width=8, height=size//16, source_width=8, clut=False, palette=[]))
        atlas += bytes([image])*size
    tables = dict(sPortraitCompTexInfos=list(range(30))+[30, 30, 30, 31, 32],
                  sNameCardCompTexInfos=list(range(33, 63))+[33])
    return bytes(atlas), records, tables


class MachineMenuAssetTests(unittest.TestCase):
    def test_exact_channels_clut_and_native_padding(self):
        data, rows, raws, specs = fixture()
        bank, records = extract(data, rows, specs)
        self.assertEqual((bank, records), extract(data, rows, specs))
        self.assertEqual([r['clut'] for r in records], [False, True, True, False])
        self.assertEqual([r['width'] for r in records], [184, 24, 8, 16])
        for record, raw, spec in zip(records, raws, specs):
            self.assertEqual(record['offset'] % 8, 0)
            expected = expected_pixels(raw, spec)
            self.assertEqual(decode_native(bank, record), expected)
            # Wrong channel order must fail the independently decoded fixture.
            if record['image'] == 0:
                swapped = [(v & 0x83e0) | (v % 32)*1024 | (v//1024 % 32) if v else 0 for v in expected]
                self.assertNotEqual(swapped, expected)
                # Appending all row padding at the image end loses source stride.
                wrong_stride = [v for y in range(3) for v in expected[y*184:y*184+180]]+[0]*12
                self.assertNotEqual(wrong_stride, expected)
        damaged = dict(records[1], palette=list(reversed(records[1]['palette'])))
        self.assertNotEqual(decode_native(bank, damaged), expected_pixels(raws[1], specs[1]))

    def test_malformed_declared_data_rejected(self):
        data, rows, _, specs = fixture()
        for key, value in [('type', 'TEXTURE'), ('format', 'I4'), ('width', 184), ('height', 4),
                           ('offset', -1), ('offset', len(data)+1)]:
            bad = copy.deepcopy(rows)
            bad[specs[0][0]][key] = value
            with self.assertRaises(ArtError):
                extract(data, bad, specs)
        bad = bytearray(data)
        bad[4:8] = b'NOPE'
        with self.assertRaises(ArtError):
            extract(bad, rows, specs)
        bad = bytearray(data)
        struct.pack_into('>I', bad, 8, 2)
        with self.assertRaises(ArtError):
            extract(bad, rows, specs)
        with self.assertRaises(ArtError):
            extract(data[:-1], rows, specs)

    def test_source_aliases_fonts_and_animation_tables(self):
        source, font, rows = source_fixture()
        result = census(source, font, rows)
        self.assertEqual(len(result['specs']), 4)
        self.assertEqual(result['aliases']['sHeaderCompTexInfo'], 0)
        self.assertEqual(result['tables']['D_StillTable'], [0, 0])
        self.assertEqual(result['fonts'][1]['images'], [1]*48)
        self.assertEqual(result['fonts'][2]['images'], [2]*48)
        self.assertEqual(result['fonts'][1]['baseline'], 16)
        self.assertEqual(result['fonts'][2]['baseline'], 8)
        self.assertEqual(result['weight_widths'], [9]*10)
        for bad in (font.replace('sFont1ACompTexInfo[]', 'sMissingCompTexInfo[]'),
                    font.replace('sFont1ACompTexInfo,sFont1ACompTexInfo', 'sFont1ACompTexInfo', 1),
                    font.replace('COMP_TEX_INFO_DEF(17,', 'COMP_TEX_INFO_DEF(19,'),
                    font.replace('kMachineWeightDigitWidths', 'missingWidths')):
            with self.assertRaises(ArtError):
                census(source, bad, rows)
        with self.assertRaises(ArtError):
            census(source.replace('D_Still,D_Still', 'D_Missing,D_Still'), font, rows)
        with self.assertRaises(ArtError):
            census('#ifdef NEW_FLAG\n'+source+'\n#endif', font, rows)

    def test_complete_residency_and_character_assignment(self):
        atlas, records, tables = residency_fixture()
        banks, packed = resident_banks(atlas, records, tables)
        self.assertEqual(len(banks), 34)
        self.assertEqual(banks[0]['images'], [63, 64, 65])
        self.assertEqual(banks[1]['images'], [0, 33])
        self.assertEqual(banks[30]['images'], [29, 62])
        self.assertEqual(banks[31]['images'], [30])
        self.assertEqual(banks[33]['filename'], 'MCH32.PAK')
        self.assertEqual(banks[0]['filename'], 'MACHUI.PAK')
        self.assertTrue(all(len(b['filename'].split('.')[0]) <= 8 for b in banks))
        for old, new in zip(records, packed):
            actual = banks[new['bank']]['payload'][new['offset']:new['offset']+new['bytes']]
            self.assertEqual(actual, atlas[old['offset']:old['offset']+old['bytes']])
        self.assertNotEqual(banks[1]['payload'], banks[2]['payload'])
        bad = copy.deepcopy(tables)
        bad['sPortraitCompTexInfos'][1] = bad['sPortraitCompTexInfos'][0]
        with self.assertRaises(ArtError):
            resident_banks(atlas, records, bad)
        with self.assertRaises(ArtError):
            resident_banks(atlas[:-1], records, tables)
        _, font, rows = source_fixture()
        catalog = census(source_fixture()[0], font, rows)
        header = identity_header(packed, banks, catalog).decode()
        self.assertIn('FZX_MACHINE_MENU_BANK_COUNT 34u', header)
        self.assertIn('fzx_machine_font1', header)

    @unittest.skipUnless(SOURCE.is_file(), 'pinned source submodule required')
    def test_stock_source_coverage(self):
        result = census()
        self.assertEqual(len(result['specs']), 241)
        self.assertEqual(result['tables']['sPortraitCompTexInfos'][0], result['aliases']['sPortraitCaptainFalconCompTexInfo'])
        self.assertEqual(result['tables']['sNameCardCompTexInfos'][5], result['aliases']['sNameCardMightyGazelleCompTexInfo'])
        self.assertEqual(result['tables']['sMachineStatValueCompTexInfos'],
                         [result['aliases']['sMachineStat%sCompTexInfo'%ch] for ch in 'ABCDE'])
        self.assertEqual(len(result['tables']['sTrophyCompTexInfos']), 21)
        self.assertEqual(result['fonts'][2]['advance'][8], 4)


if __name__ == '__main__':
    unittest.main()
