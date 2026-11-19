"""FZX_ROAD_PLAN_FIXED (ref/road_plan_fixed.inc): HOST edge contract under UBSan, and
random frames against the binary32 source test (no ROM or course data)."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'), 'POSIX GCC/Clang required')
class RoadPlanFixedTests(unittest.TestCase):
    def build(self, directory, cc):
        exe = directory / ('plan-fixed-' + cc)
        args = [cc, '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-fsanitize=undefined', '-fsanitize=float-cast-overflow',
                '-fno-sanitize-recover=all', '-I' + str(ROOT / 'ref'), str(ROOT / 'tests/road_plan_fixed_driver.c'), '-lm',
                '-o', str(exe)]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return exe

    def test_edges_and_random(self):
        with tempfile.TemporaryDirectory() as temp:
            for cc in ('gcc', 'clang'):
                exe = self.build(Path(temp), cc)
                edges = subprocess.run([str(exe), 'edges'], capture_output=True, text=True)
                self.assertEqual(edges.returncode, 0, edges.stdout + edges.stderr)
                self.assertNotIn('runtime error', edges.stderr)
                out = edges.stdout
                for line in ('zero depth column -> fallback', 'max scale 2^100 -> fallback', 'min scale 2^-120 -> fallback',
                             'huge translation -> fallback', 'inf entry -> fallback', 'nan entry -> fallback',
                             'inf far -> fallback', 'huge far -> fallback', 'nan position -> fallback',
                             'negative-depth chunks ok', 'normal handled', 'edges pass'):
                    self.assertIn(line, out, out)
                rnd = subprocess.run([str(exe), 'random', '3000', '7'], capture_output=True, text=True)
                self.assertEqual(rnd.returncode, 0, rnd.stdout + rnd.stderr)
                self.assertNotIn('runtime error', rnd.stderr)
                print(cc, rnd.stdout.strip())


if __name__ == '__main__':
    unittest.main()
