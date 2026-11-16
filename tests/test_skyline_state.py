"""Pinned source skyline boundaries; no retail texture data is used."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import backdrop_state
import skyline_state


class SkylineTests(unittest.TestCase):
    def setUp(self):
        if not backdrop_state.SOURCE.is_file():
            self.skipTest('source submodule not initialized')
        self.source = backdrop_state.SOURCE.read_text()

    def test_native_storage_and_binding(self):
        result = skyline_state.native(self.source)
        for removed in ('gGfxPool', 'D_8024DCE0', 'func_80078104', 'Math_Rand1'):
            self.assertNotIn(removed, result)
        self.assertIn('native_skyline_tint[4] = {200,130,240,255}', result)
        self.assertIn('native_skyline_matrices[native_skyline_pool][i]', result)
        self.assertIn('native_skyline_vertices[i][3]', result)

    def test_changed_source_is_rejected(self):
        for old, new in [('vec.x <= 23000.0f', 'vec.x < 23000.0f'),
                         ('BG_SPRITE_CITY_SKYLINE_4, 150.0f', 'BG_SPRITE_CITY_SKYLINE_4, 180.0f'),
                         ('200, 130, 240, SKYBOX_CLOUDY', '200, 131, 240, SKYBOX_CLOUDY')]:
            changed = self.source.replace(old, new)
            self.assertNotEqual(changed, self.source)
            with self.assertRaisesRegex(ValueError, 'Source skyline changed'):
                skyline_state.native(changed)


if __name__ == '__main__':
    unittest.main()
