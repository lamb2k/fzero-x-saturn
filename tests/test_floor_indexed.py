"""Offline indexed-floor layout, bounded colour adaptation and receipt controls."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from floor_indexed import (ArtError, build, check_receipt, generate, rgb,
                           validate_reconstruction)


def floor_bytes(words):
    return struct.pack('>2048H', *(words*(2048//len(words))+words[:2048 % len(words)]))


class IndexedFloor(unittest.TestCase):
    def test_lossless_layout_palette_endpoints_and_repeat(self):
        floor = floor_bytes([0x8000+i for i in range(256)])
        bitmap, palette, metrics = build(floor)
        colours = struct.unpack('>256H', palette)
        self.assertEqual((len(bitmap), len(palette)), (131072, 512))
        self.assertEqual((colours[0], colours[255]), (0x8000, 0x80ff))
        self.assertEqual(metrics['changed_source_pixels'], 0)
        source = struct.unpack('>2048H', floor)
        for y in range(256):
            for x in range(512):
                self.assertEqual(colours[bitmap[y*512+x]], source[(y % 32)*64+x % 64])
        self.assertEqual(build(floor), (bitmap, palette, metrics))

    def test_over_256_merge_and_naive_top_256_control(self):
        # 255 separated colours plus one adjacent pair and an isolated rare colour.
        base = [0x8000 | r*4 | (g*4 << 5) | (b*4 << 10)
                for b in range(4) for g in range(8) for r in range(8)][:255]
        base += [0x8001]
        pixels = (base*8)[:2047]+[0xffff]
        floor = struct.pack('>2048H', *pixels)
        bitmap, palette, metrics = build(floor)
        self.assertEqual(metrics['source_colours'], 257)
        self.assertEqual(metrics['used_colours'], 256)
        self.assertLessEqual(max(metrics['max_rgb5_channel_error']), 1)
        self.assertIn(0xffff, struct.unpack('>256H', palette))
        self.assertEqual(build(floor), (bitmap, palette, metrics))
        naive = [word for word, count in Counter(pixels).most_common(256)]
        indices = [min(range(256), key=lambda i: sum((a-b)**2 for a, b in zip(rgb(p), rgb(naive[i]))))
                   for p in pixels]
        tile = bytes(indices)
        wrong = b''.join(tile[y*64:(y+1)*64]*8 for y in range(32))*8
        with self.assertRaisesRegex(ArtError, 'exceeds one'):
            validate_reconstruction(floor, wrong, struct.pack('>256H', *naive))

    def test_invalid_opacity_extent_and_unrepresentable_input(self):
        for floor in (b'', bytes(4095), bytes(4096), b'\x80\x00'*2047+b'\x00\x00'):
            with self.assertRaises(ArtError):
                build(floor)
        separated = [0x8000 | r*4 | (g*4 << 5) | (b*4 << 10)
                     for b in range(8) for g in range(8) for r in range(8)][:257]
        with self.assertRaisesRegex(ArtError, 'Cannot reduce'):
            build(floor_bytes(separated))

    def test_chained_merges_check_original_members(self):
        separated = [0x8000 | r*4 | (g*4 << 5) | (b*4 << 10)
                     for b in range(8) for g in range(8) for r in range(8)][1:256]
        floor = floor_bytes([0x8000, 0x8001, 0x8002]+separated)
        bitmap, palette, metrics = build(floor)
        self.assertEqual(metrics['source_colours'], 258)
        self.assertEqual(metrics['used_colours'], 256)
        colours = struct.unpack('>256H', palette)
        self.assertEqual([colours[index] for index in bitmap[:3]], [0x8001]*3)

    def test_error_bound_and_wrong_repeat_controls(self):
        floor = floor_bytes([0x8000, 0x801f])
        bitmap, palette, _ = build(floor)
        corrupted = bytearray(palette)
        corrupted[1] = 2  # Two RGB5 steps from original index zero.
        with self.assertRaisesRegex(ArtError, 'exceeds one'):
            validate_reconstruction(floor, bitmap, corrupted)
        wrong = bytearray(bitmap)
        wrong[512*128+128] = 1
        with self.assertRaisesRegex(ArtError, 'exceeds one'):
            validate_reconstruction(floor, wrong, palette)
        with self.assertRaisesRegex(ArtError, 'extent'):
            validate_reconstruction(floor, bitmap[:-1], palette)

    def test_receipt_manifest_binding_and_corruption_controls(self):
        (ROOT/'generated').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT/'generated') as temp:
            root = Path(temp)
            source = root/'source'
            source.mkdir()
            floor = floor_bytes([0x8000, 0xffff])
            (source/'floor.bin').write_bytes(floor)
            manifest = dict(bindings=dict(floor=dict(texture='synthetic')),
                            assets=[dict(symbol='synthetic', format='RGBA16', width=64, height=32,
                                         native_file='floor.bin', native_sha256=hashlib.sha256(floor).hexdigest())])

            def admit(omit=None):
                (source/'backdrop.json').write_bytes(json.dumps(manifest).encode())
                generator = ROOT/'tools/floor_indexed.py'
                lines = ['# fzx generation receipt v1',
                         'generator %s floor_indexed.py' % hashlib.sha256(generator.read_bytes()).hexdigest()]
                for name in ('floor.bin', 'backdrop.json'):
                    if name != omit:
                        lines.append('output %s %s' % (hashlib.sha256((source/name).read_bytes()).hexdigest(), name))
                (source/'backdrop.receipt').write_bytes(('\n'.join(lines)+'\n').encode())

            admit()
            out = root/'output'
            description = generate(source, out)
            self.assertEqual(description['metrics']['changed_source_pixels'], 0)
            self.assertEqual(len(check_receipt(out/'floor_indexed.receipt')), 4)
            original = {p.name: p.read_bytes() for p in out.iterdir()}
            generate(source, out)
            self.assertEqual(original, {p.name: p.read_bytes() for p in out.iterdir()})
            (out/'FLOORIDX.BIN').write_bytes(b'wrong')
            with self.assertRaisesRegex(ArtError, 'Stale'):
                check_receipt(out/'floor_indexed.receipt')
            (source/'floor.bin').write_bytes(bytes(4096))
            with self.assertRaisesRegex(ArtError, 'Stale'):
                generate(source, out)
            (source/'floor.bin').write_bytes(floor)
            admit('backdrop.json')
            with self.assertRaisesRegex(ArtError, 'manifest is not receipt-bound'):
                generate(source, out)
            admit('floor.bin')
            with self.assertRaisesRegex(ArtError, 'identity mismatch'):
                generate(source, out)
            manifest['assets'][0]['width'] = 32
            admit()
            with self.assertRaisesRegex(ArtError, 'Unexpected source floor'):
                generate(source, out)


if __name__ == '__main__':
    unittest.main()
