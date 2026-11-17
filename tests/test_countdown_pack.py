"""Synthetic original-style geometry only; no retail content in these tests."""
from pathlib import Path
import copy
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from countdown_pack import build, chart_geometry, bake_chart, uv_at, mirror, ArtError
from road_pack import fnv


def fixture():
    vertices, quads, triangles = [], [], []
    for i in range(23):
        start = len(vertices)
        # The fixture is a generated square, unrelated to source game geometry.
        for x, y, u, v in ((0, 0, 0, 0), (32, 0, 992, 0), (32, 32, 992, 992), (0, 32, 0, 992)):
            vertices.append([x, y, i*3, u, v, 0, 0, 127, 255])
        material = i%3 if i < 20 else 3 if i < 22 else 4
        quads.append([start, start+1, start+2, start+3, material])
        triangles += [[start, start+1, start+2, material], [start, start+2, start+3, material]]
    textures = [dict(symbol='texture'+str(i), width=32, height=32, file=str(i)+'.rgb1555') for i in range(9)]
    materials = [dict(geometry=0x800405, combine=[0xfcffffff, 0xfffcf279], tile=0, scale=[65535, 65535],
                      tiles={'0': [0xf5100000, 0x54150]}, texture={'name': 'texture'+str(i)}) for i in range(5)]
    model = dict(vertices=vertices, quads=quads, triangles=triangles, materials=materials,
                 sections=dict(frame=[0, 20], mouth=[20, 2], digit=[22, 1]))
    data = dict(schema=1, model=model, textures=textures, mouth_textures=['texture3', 'texture4'],
                digit_textures=['texture5', 'texture6', 'texture7', 'texture8'])
    files = {str(i)+'.rgb1555': b''.join(struct.pack('>H', 0 if x == y == 0 else 0x8000 | (i<<10) | (y<<5) | x)
                                      for y in range(32) for x in range(32)) for i in range(9)}
    return data, files


class CountdownPackTests(unittest.TestCase):
    def test_full_pack_and_independent_pixels(self):
        manifest, files = fixture()
        meta, pixels, report = build(manifest, files)
        self.assertEqual((meta, pixels, report), build(manifest, files))
        self.assertEqual(meta[:8], b'FZXCDN01')
        size, tb, th, nv, nq, ni, va, qa, ia, mh = struct.unpack_from('>10I', meta, 8)
        zeroed = bytearray(meta); zeroed[44:48] = bytes(4)
        self.assertEqual((size, tb, th, mh), (len(meta), len(pixels), fnv(pixels), fnv(zeroed)))
        self.assertEqual((nv, nq, ni), (92, 23, 28))
        for i, v in enumerate(manifest['model']['vertices']):
            self.assertEqual(struct.unpack_from('>5h4B2x', meta, va+i*16), tuple(v))
        for i in range(nq):
            row = struct.unpack_from('>8H', meta, qa+i*16)
            self.assertEqual(row[:4], tuple(manifest['model']['quads'][i][:4]))
            self.assertEqual(row[5:], ((1, 0, i) if i < 20 else (2, 1, i) if i < 22 else (4, 2, i)))
        for i in range(ni):
            w, h, at, q, state, tex, zero = struct.unpack_from('>2HI4H', meta, ia+i*16)
            self.assertEqual((w, h, zero), (32, 32, 0))
            self.assertLess(q, 23); self.assertLess(state, 4)
            self.assertEqual(at % 8, 0)
            self.assertEqual(pixels[at:at+w*h*2], files[str(tex)+'.rgb1555'])
        self.assertEqual(report['lighting'], False)

    def test_uv_is_original_triangles_not_bilinear_uv(self):
        # Square geometry with a deliberately non-affine UV field: diagonal
        # 0->2 has U=0 at both ends; bilinear UV wrongly produces U=1/4 there.
        v = [[0,0,0,0,0],[8,0,0,32,0],[8,8,0,0,32],[0,8,0,0,0]]
        points = chart_geometry(v)
        uv, den = uv_at(v, points, 1, 1, 3, 3)
        self.assertEqual(uv, [0,16*den])
        uv, den = uv_at(v, points, 3, 1, 5, 5)
        self.assertEqual(uv, [16*den,8*den])
        # The second triangle and reversed winding must preserve the same map.
        uv, den = uv_at(v, points, 1, 3, 5, 5)
        self.assertEqual(uv, [0,8*den])
        reverse = [v[i] for i in (0,3,2,1)]
        u2, d2 = uv_at(reverse, chart_geometry(reverse), 3, 1, 5, 5)
        self.assertEqual([x*den for x in u2], [x*d2 for x in uv])
        self.assertEqual([mirror(x, 4) for x in range(-4, 12)], [3,2,1,0,0,1,2,3,3,2,1,0,0,1,2,3])

    def test_mirror_and_wrong_uv_control(self):
        manifest, files = fixture()
        vertices = copy.deepcopy(manifest['model']['vertices'][:4])
        texture = manifest['textures'][0]
        for v in vertices:
            v[3] -= 1024
        w, h, actual = bake_chart(vertices, texture, files['0.rgb1555'])
        expected = b''.join(files['0.rgb1555'][(y*32+31-x)*2:(y*32+32-x)*2] for y in range(h) for x in range(w))
        self.assertEqual(actual, expected)
        broken = copy.deepcopy(vertices)
        for v in broken:
            v[3] += 1024
        self.assertNotEqual(actual, bake_chart(broken, texture, files['0.rgb1555'])[2])

    def test_rejection_controls(self):
        manifest, files = fixture()
        mutations = [
            lambda m: m['model']['vertices'][3].__setitem__(2, 1),
            lambda m: m['model']['vertices'][1].__setitem__(0, -2),
            lambda m: m['model']['vertices'][1].__setitem__(3, 32767),
            lambda m: m['model']['materials'][0].__setitem__('geometry', 0x820405),
            lambda m: m['model']['materials'][0].__setitem__('combine', [0,0]),
            lambda m: m['model']['materials'][0]['tiles'].__setitem__('0', [0xf5100000,0]),
            lambda m: m['model']['triangles'][0].__setitem__(2, 3),
            lambda m: m['model']['quads'][20].__setitem__(4, 0),
            lambda m: m['model']['quads'][0].__setitem__(0, 92),
            lambda m: m['mouth_textures'].pop(),
        ]
        for mutate in mutations:
            bad = copy.deepcopy(manifest); mutate(bad)
            with self.assertRaises(ArtError):
                build(bad, files)
        files['0.rgb1555'] = files['0.rgb1555'][:-1]
        with self.assertRaises(ArtError):
            build(manifest, files)


if __name__ == '__main__':
    unittest.main()
