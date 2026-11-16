"""HOST tier boundaries, source traversal and fixed-point projection controls."""
from pathlib import Path
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

import numpy as np
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import road_lod_census as lod


def camera():
    data = bytearray(252)
    struct.pack_into('<3f', data, 76, 60, 1, 10000)
    struct.pack_into('<4f', data, 88, 160, 112, 160, 120)
    view = [1., 0, 0, 0, 0, 1., 0, 0, 0, 0, 1., 0, 0, 0, 0, 1.]
    projection = [1.3, 0, 0, 0, 0, 1.7, 0, 0, .01, -.02, -1., -1., 0, 0, -2., 0]
    struct.pack_into('<16f', data, 120, *view)
    struct.pack_into('<16f', data, 184, *projection)
    return bytes(data)


class CensusTests(unittest.TestCase):
    def test_repetition_phase_and_threshold_edges(self):
        self.assertEqual([lod.tier(x) for x in (0, 3.999, 4, 7.999, 8, 15.999, 16)], [0, 0, 4, 4, 2, 2, 1])
        for lo, hi, unit, expected in [(0, 32, 32, 1), (1, 33, 32, 2), (-33, 1, 32, 3),
                                        (-32, 0, 32, 1), (0, 128, 128, 1), (1, 128, 128, 1)]:
            self.assertEqual(lod.pieces(lo, hi, unit), expected)
        for args in ((0, 0, 32), (3, 2, 32), (0, 32, 0)):
            with self.assertRaises(lod.ArtError):
                lod.pieces(*args)
        with self.assertRaises(lod.ArtError):
            lod.tier(1, (4, 8, 16))

    def test_source_group_wrapping_and_rejection(self):
        plan = bytearray(796)
        struct.pack_into('<4I', plan, 0, 0, 1, 3, 1)
        struct.pack_into('<HHi', plan, 28, 3, 1, 1)
        self.assertEqual(lod.chunks(plan, 5), ([0, 4, 3], True))
        struct.pack_into('<HHi', plan, 28, 3, 1, -1)
        self.assertEqual(lod.chunks(plan, 5)[0], [3, 4, 0])
        struct.pack_into('<I', plan, 8, 5)
        for direction in (-1, 1):
            struct.pack_into('<HHi', plan, 28, 2, 2, direction)
            self.assertEqual(set(lod.chunks(plan, 5)[0]), set(range(5)))
        struct.pack_into('<I', plan, 8, 4)
        with self.assertRaises(lod.ArtError):
            lod.chunks(plan, 5)

    def test_reference_adapter_is_explicit_and_fail_closed(self):
        code = (ROOT/'src/fzx_road_tiles.c').read_text()
        adapted = lod.reference_variant(code)
        self.assertIn('period*32*(1<<census_tier)', adapted)
        self.assertIn('!census_forced && front', adapted)
        self.assertIn('if(census_tier){row=0;height=1;}', adapted)
        with self.assertRaises(lod.ArtError):
            lod.reference_variant(code.replace('const int32_t unit=(int32_t)period*32;', '/* changed reference */'))
        with self.assertRaises(lod.ArtError):
            lod.census({}, b'', b'', expected_views=1)

    def test_texture_budget_does_not_assume_lossless_palette(self):
        sizes = lod.texture_sizes([dict(width=8, period=16, unique_rgb1555=17)])
        self.assertEqual(sizes['base_strips_lossless_clut16'], 0)
        self.assertEqual(sizes['rgb1555_total'], 4*8*16*2+8*2)
        self.assertEqual(sizes['rgb1555_three_fog_variants'], sizes['rgb1555_total']*3)
        self.assertEqual(lod.disjoint_chains([[0, 1], [1, 2], [2, 3]], {0, 1, 2, 3}), 2)
        self.assertEqual(lod.disjoint_chains([[0, 1], [1, 2]], {0, 2}), 0)

    @unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'), 'POSIX compilers required')
    def test_fixed_view_and_positions_match_frozen_c(self):
        # Source camera to native Q formats must match exactly. Arithmetic uses
        # signed truncation, including negative translations and rotated points.
        helper = '''
#include "fzx_view.h"
#include "ref.h"
int census_test(const RefCamera *c,fzx_view_t *v) {
    const float crop[4]={12,16,308,224};
    return fzx_view_from_source(v,c->view,c->projection,c->viewport,crop,c->near_plane,c->far_plane);
}
'''
        rng = np.random.default_rng(947)
        with tempfile.TemporaryDirectory() as td:
            temp = Path(td)
            (temp/'check.c').write_text(helper)
            for cc in ('gcc', 'clang'):
                libpath = temp/(cc+'.so')
                subprocess.run([cc, '-shared', '-fPIC', '-O2', '-DFZX_SOURCE_CAMERA=1',
                                '-I'+str(ROOT/'include'), '-I'+str(ROOT/'ref'), str(temp/'check.c'),
                                str(ROOT/'src/fzx_view.c'), '-o', str(libpath)], check=True)
                lib = ctypes.CDLL(str(libpath))
                lib.census_test.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
                lib.fzx_view_transform.argtypes = [ctypes.c_void_p]*3
                for trial in range(20):
                    data = bytearray(camera())
                    matrix = rng.uniform(-.5, .5, (4, 4)).astype(np.float32)
                    matrix[:, 3] = [0, 0, 0, 1]
                    matrix[3, :3] *= 10000
                    struct.pack_into('<16f', data, 120, *matrix.ravel())
                    positions = rng.integers(-32768, 32768, (40, 3), dtype=np.int64)
                    points, _, _, _, _, expected = lod.project(bytes(data), positions)
                    source = ctypes.create_string_buffer(bytes(data));native = ctypes.create_string_buffer(80)
                    self.assertEqual(lib.census_test(source, native), 1)
                    self.assertEqual(native.raw, expected)
                    for position, expected_point in zip(positions, points):
                        world = (ctypes.c_int32*3)(*[int(v)*4096 for v in position])
                        result = (ctypes.c_int32*3)()
                        self.assertTrue(lib.fzx_view_transform(native, world, result))
                        self.assertEqual(list(result), list(expected_point))


if __name__ == '__main__':
    unittest.main()
