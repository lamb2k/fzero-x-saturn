"""Synthetic compact-road round trip, phase/coverage and rehashed corruption."""
from pathlib import Path
import copy
import hashlib
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from test_road_tiles import synthetic
import road_bake as bake
from road_pack import fnv
from road_tiles import strip


def fixture(bend=0):
    mesh, _ = synthetic()
    tex = mesh['textures']['synthetic_checker']
    rgba = bytes.fromhex(tex['rgba_hex'])*2
    tex.update(height=8, rgba_hex=rgba.hex())
    # The pack tools require the source fingerprint to describe original RGBA16.
    raw = b''.join(struct.pack('>H', (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | (a >> 7))
                   for r, g, b, a in zip(rgba[::4], rgba[1::4], rgba[2::4], rgba[3::4]))
    tex['source_sha256'] = hashlib.sha256(raw).hexdigest()
    material = mesh['materials'][0]
    material['sizes']['0'][1] = 0x0000c01c
    mesh['vertices'], mesh['triangles'], mesh['origins'] = [], [], []
    mesh['chunks'] = [copy.deepcopy(mesh['chunks'][0]) for _ in range(5)]
    for c in range(4):
        lo, hi = c*200, (c+1)*200
        y0, y1 = (bend if c == 2 else 0), (bend if c+1 == 2 else 0)
        # Equal endpoint positions but deliberately separate original row IDs.
        mesh['vertices'] += [[-200, y1, hi, 0, 2048, 0, 0, 0, 255],
                             [-200, y0, lo, 0, 0, 0, 0, 0, 255],
                             [200, y0, lo, 96, 0, 0, 0, 0, 255],
                             [200, y1, hi, 96, 2048, 0, 0, 0, 255]]
        mesh['triangles'] += [[4*c, 4*c+1, 4*c+2, 0], [4*c, 4*c+2, 4*c+3, 0]]
        mesh['origins'] += [dict(chunk=c, conditional_outer=False)]*2
    return mesh


def rehash(data, offset=20):
    result = bytearray(data)
    result[offset:offset+4] = bytes(4)
    struct.pack_into('>I', result, offset, fnv(result))
    return bytes(result)


def decode_image(metadata, textures, image_index, fog_variant):
    offset = struct.unpack_from('>I', metadata, 120+5*4)[0]
    mode, width, height, repetitions, *addresses = struct.unpack_from('>4H6I', metadata, offset+image_index*32)
    count = width*height
    at = addresses[fog_variant]
    if not mode:
        return list(struct.unpack_from('>%dH' % count, textures, at))
    palette = struct.unpack_from('>16H', textures, addresses[3+fog_variant])
    result = []
    for b in textures[at:at+count//2]:
        result.extend((palette[b >> 4], palette[b & 15]))
    return result


class RoadBakeTests(unittest.TestCase):
    def test_complete_source_rows_far_provenance_and_repeat(self):
        mesh = fixture()
        metadata, textures, report = bake.build(mesh, bytes(range(32)), [252, 192, 253])
        counts, offsets = bake.validate(metadata, textures)
        self.assertEqual(counts[:3], (16, 4, 4))
        self.assertEqual(counts[5:7], (3, 1))
        self.assertEqual(report['far']['4']['maximum_chord_error'], 0)
        for i, vertex in enumerate(mesh['vertices']):
            self.assertEqual(struct.unpack_from('>5h', metadata, offsets[0]+10*i), tuple(vertex[:5]))
        for i in range(4):
            repeat = struct.unpack_from('>I', metadata, offsets[2]+4*i)[0]
            self.assertEqual(bake.decode_repeat(repeat, 256), (0, 8, 0, 2048, False))
        far = struct.unpack_from('>8H', metadata, offsets[7])
        self.assertEqual(far[:4], (12, 1, 2, 15))
        self.assertEqual(far[5:], (0, 0, 0))
        bent, _, bent_report = bake.build(fixture(100), bytes(range(32)), [252, 192, 253])
        self.assertEqual(bent_report['far']['4']['maximum_chord_error'], 100)
        self.assertNotEqual(metadata, bent)
        _, bent_offsets = bake.validate(bent, textures)
        bad_error = bytearray(bent)
        struct.pack_into('>H', bad_error, bent_offsets[7]+12, 0)
        with self.assertRaises(bake.ArtError):
            bake.validate(rehash(bad_error), textures)
        self.assertEqual(bake.build(mesh, bytes(range(32)), [252, 192, 253])[:2], (metadata, textures))

    def test_periodic_filter_and_source_color_bound(self):
        pixels = [bake.word((v, 0, 0)) for v in range(8)]
        expected = [0, 2, 4, 6, 0, 2, 4, 6]  # ties-to-even averages of adjacent rows
        self.assertEqual([bake.rgb(p)[0] for p in bake.periodic_tier(pixels, 1, 8, 2)], expected)
        self.assertNotEqual(bake.periodic_tier(pixels, 1, 8, 2), pixels)  # wrong repetition control
        mesh, fog = fixture(), [252, 192, 253]
        metadata, textures, report = bake.build(mesh, bytes(32), fog)
        raw, info = strip(mesh, 0, 0, 96, fog)
        values = struct.unpack('>%dH' % (len(raw)//2), raw)
        width, height = info['width'], info['period']
        stride = width*(height+1)
        for t, repetitions in enumerate((1, 2, 4, 8, 0)):
            for v in range(3):
                bank = values[v*stride:(v+1)*stride]
                source = bake.periodic_tier(bank[:width*height], width, height, repetitions) if repetitions else bank[width*height:]
                stored = decode_image(metadata, textures, t, v)
                self.assertEqual(len(source), len(stored))
                self.assertLessEqual(max(abs(a-b) for p, q in zip(source, stored) for a, b in zip(bake.rgb(p), bake.rgb(q))), 1)
        self.assertLess(report['texture_bytes'], report['unshared_texture_bytes'])

    def test_negative_phase_and_uneven_edges(self):
        mesh = fixture()
        # Preserve both edge lengths, but give the right edge a different phase.
        for vertex in mesh['vertices']:
            vertex[4] -= 33 if vertex[0] < 0 else 1
        metadata, textures, _ = bake.build(mesh, bytes(32), [0, 0, 0])
        _, offsets = bake.validate(metadata, textures)
        value = struct.unpack_from('>I', metadata, offsets[2])[0]
        self.assertEqual(bake.decode_repeat(value, 256), (223, 9, 255, 2080, True))
        with self.assertRaises(bake.ArtError):
            bake.decode_repeat(value | (1 << 31), 256)

    def test_rehashed_invalid_records_and_palette_fail(self):
        metadata, textures, _ = bake.build(fixture(), bytes(32), [0, 0, 0])
        _, offsets = bake.validate(metadata, textures)
        mutations = [(offsets[2], '>I', 1024),  # count disagrees with original UVs
                     (offsets[1], '>H', 65535), (offsets[3]+4, '>I', 3),
                     (offsets[5]+8, '>I', len(textures)), (offsets[7]+8, '>H', 5)]
        for at, fmt, value in mutations:
            bad = bytearray(metadata)
            struct.pack_into(fmt, bad, at, value)
            with self.assertRaises(bake.ArtError):
                bake.validate(rehash(bad), textures)
        bad = bytearray(textures)
        palette_at = struct.unpack_from('>I', metadata, offsets[5]+20)[0]
        bad[palette_at] &= 0x7f
        repaired_texture = rehash(bad, 16)
        repaired_meta = bytearray(metadata)
        repaired_meta[56:88] = hashlib.sha256(repaired_texture).digest()
        with self.assertRaises(bake.ArtError):
            bake.validate(rehash(repaired_meta), repaired_texture)
        with self.assertRaises(bake.ArtError):
            bake.validate(metadata[:-1], textures)

    def test_constant_shade_and_quality_fallback(self):
        mesh = fixture()
        mesh['vertices'][0][5] = 1
        with self.assertRaises(bake.ArtError):
            bake.build(mesh, bytes(32), [0, 0, 0])
        pixels = [bake.word((r, g, b)) for r in (0, 10, 20, 31) for g in (0, 10, 20, 31) for b in (0, 10, 20, 31)]
        _, _, error = bake.quantize16(pixels)
        self.assertGreater(error['max_channel_error'], 1)  # must use the RGB fallback
        self.assertEqual(bake.quantize16(pixels), bake.quantize16(pixels))


if __name__ == '__main__':
    unittest.main()
