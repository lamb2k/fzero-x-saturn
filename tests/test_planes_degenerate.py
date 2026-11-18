"""HOST checks of FZX_PLANES_DEGENERATE_UV (planes fault at the eye crossing the cloud plane).

Knob 0 keeps the existing behaviour: a collapsed UV basis is fault 1. Knob 1 skips that layer
and counts it; every non-degenerate draw is byte-identical between the two builds."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_and_run(compiler, knob, out):
    exe = out / f'degenerate-{compiler}-{knob}'
    r = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', f'-DFZX_PLANES_DEGENERATE_UV={knob}',
                        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'ref'), str(ROOT / 'src/fzx_planes.c'),
                        str(ROOT / 'src/fzx_view.c'), str(ROOT / 'tests/planes_degenerate_driver.c'), '-o', str(exe)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    rows = {}
    for line in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines():
        name, *rest = line.split()
        rows[name] = dict(zip(rest[0::2], rest[1::2]))
    return rows


@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'), 'GCC/Clang required')
class PlanesDegenerateUvTests(unittest.TestCase):
    def test_knob_off_faults_knob_on_skips(self):
        with tempfile.TemporaryDirectory() as d:
            for cc in ('gcc', 'clang'):
                off, on = build_and_run(cc, 0, Path(d)), build_and_run(cc, 1, Path(d))
                # Non-degenerate scene: identical in both builds.
                self.assertEqual(off['normal'], {**on['normal'], 'skipped': '0'})
                self.assertEqual(on['normal']['ok'], '1')
                self.assertGreater(int(on['normal']['draws']), 4)
                # Collapsed cloud UVs: knob 0 is fault 1 with no draws of that layer; knob 1 draws the floor only.
                self.assertEqual((off['cloud-collapsed']['ok'], off['cloud-collapsed']['fault']), ('0', '1'))
                self.assertEqual((on['cloud-collapsed']['ok'], on['cloud-collapsed']['fault']), ('1', '0'))
                self.assertGreater(int(on['cloud-collapsed']['draws']), 0)
                self.assertLess(int(on['cloud-collapsed']['draws']), int(on['normal']['draws']))
                self.assertEqual(on['cloud-collapsed']['skipped'], '1')
                # Collapsed floor UVs: same rule for the other layer.
                self.assertEqual((off['floor-collapsed']['ok'], off['floor-collapsed']['fault']), ('0', '1'))
                self.assertEqual((on['floor-collapsed']['ok'], on['floor-collapsed']['fault']), ('1', '0'))
                self.assertEqual(on['floor-collapsed']['skipped'], '2')


if __name__ == '__main__':
    unittest.main()
