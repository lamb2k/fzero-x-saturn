import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import asset_coverage


class CoverageTests(unittest.TestCase):
    def fixture(self, root, families=33, lods=5):
        yaml = root / asset_coverage.YAML
        yaml.mkdir(parents=True)
        (yaml / 'synthetic.yaml').write_text('vertices:\n  type: VTX\n  count: 3\n')
        racer = root / 'src/game/racer.c'
        racer.parent.mkdir(parents=True)
        rows = ['{ ' + ', '.join(f'Machine_DrawFixture{i}Lod{j}' for j in range(1, lods+1))
                + f' }}, // PILOT_{i}' for i in range(families)]
        racer.write_text('Gfx* (*sMachineDrawFuncs[][5])(Gfx*) = {\n' + '\n'.join(rows) + '\n};')
        title = root / 'src/overlays/ovl_i4/title.c'
        title.parent.mkdir(parents=True)
        title.write_text('aTitleBackgroundMainTex aTitleLogoTex')

    def test_inventory_does_not_claim_conversion(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            result = asset_coverage.census(root)
            self.assertEqual(len(result['machine_lods']), 165)
            self.assertEqual(result['counts'], {'VTX': 1})
            self.assertTrue(all(row['status'] == 'unqualified' and not row['evidence']
                                for row in result['assets'] + result['machine_lods']))

    def test_missing_family_control(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root, families=32)
            with self.assertRaisesRegex(ValueError, 'machine families'):
                asset_coverage.census(root)

    def test_missing_lod_control(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root, lods=4)
            with self.assertRaisesRegex(ValueError, 'machine LOD'):
                asset_coverage.census(root)


if __name__ == '__main__':
    unittest.main()
