"""FZX_MEM_NO_PROTO_RINGS (C-048): with the native core empty, the frame headers are
identical with and without the placeholder rings; the rings build's autopilot steers
into a turn; the no-rings build rejects an autopilot request before steering."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
class MemRingsTests(unittest.TestCase):
    def build(self, tmp, cc, rings_out):
        exe = tmp/f'check-{cc}-{rings_out}'
        r = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=undefined',
            '-fno-sanitize-recover=all', '-DSH2Z_FORCE_PORTABLE=1', '-DFZX_SIM=2',
            '-DFZX_SOURCE_CAMERA=1', '-DFZX_SOURCE_MACHINE=1', '-DFZX_SOURCE_ROAD_TILES=1',
            '-DFZX_NATIVE_CORE_EMPTY=1', '-DFZX_SOURCE_ROAD_STREAM=1',
            '-DFZX_MEM_NO_PROTO_RINGS='+str(rings_out), '-I'+str(ROOT/'include'),
            '-I'+str(ROOT/'src'), '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
            str(ROOT/'src/fzx.c'), str(ROOT/'src/fzx_view.c'), str(ROOT/'tests/mem_rings_driver.c'),
            *map(str, (ROOT/'third_party/SH2ZAM/saturn/src').glob('*.c')),
            '-lm', '-o', str(exe)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return exe

    def test_frames_identical_steering_and_rejection(self):
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td); frames = []
            for cc in ('gcc', 'clang'):
                with_rings, without = self.build(tmp, cc, 0), self.build(tmp, cc, 1)
                for exe in (with_rings, without):
                    run = subprocess.run([str(exe), 'f'], capture_output=True)
                    self.assertEqual(run.returncode, 0, run.stderr.decode())
                    self.assertEqual(len(run.stdout), 1024*16)
                    frames.append(run.stdout)
                # Rings build: on the straight's centreline the near hint is small;
                # looking round the corner it steers into the turn.
                turn = subprocess.run([str(with_rings), 't'], capture_output=True, text=True)
                self.assertEqual(turn.returncode, 0, turn.stderr)
                near, far = map(int, turn.stdout.split())
                self.assertLessEqual(abs(near), 8, (near, far))
                self.assertGreaterEqual(abs(far), 16, (near, far))
                # No-rings build: the request is rejected, never answered with 0.
                rej = subprocess.run([str(without), 't'], capture_output=True, text=True)
                self.assertEqual(rej.returncode, 42, (rej.returncode, rej.stdout, rej.stderr))
                self.assertIn('REJECTED', rej.stderr)
                self.assertEqual(rej.stdout, '')
            self.assertTrue(all(f == frames[0] for f in frames))


if __name__ == '__main__':
    unittest.main()
