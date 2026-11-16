"""Timer wrap, exclusive nesting and independent-clock rejection controls."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from road_profile import decode_sample


class ProfileTests(unittest.TestCase):
    def test_independent_clock_controls(self):
        words = [0x52504631, 90000, 100, 1000, 0, 0, 0, 0] + [10000]*9 + [1]*9
        def row(w):
            return dict(tick=1, profile_hex=struct.pack('>26I', *w).hex(),
                        field_ticks=8000, profile_outer_fields=11, profile_outer_ticks=(90000+10)&65535)
        self.assertEqual(decode_sample(row(words))['total_ticks'], 90000)
        boundary=words.copy();boundary[1]=7990;boundary[8:17]=[7990]+[0]*8
        edge=row(boundary);edge.update(profile_outer_fields=2,profile_outer_ticks=8010)
        self.assertEqual(decode_sample(edge)['outer_ticks'],20)
        for index in (0, 1, 4, 5, 6, 7, 8):
            bad=words.copy();bad[index]+=1
            with self.assertRaises(ValueError):decode_sample(row(bad))
        # Preserve the partition and raw 16-bit result while dropping a whole
        # wrap. Only the independent video-field check can catch this control.
        bad=words.copy();bad[1]-=65536
        bad[8:17]=[bad[1]]+[0]*8
        with self.assertRaisesRegex(ValueError, 'video fields'):decode_sample(row(bad))

    @unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'), 'POSIX compilers')
    def test_clock_and_nesting(self):
        with tempfile.TemporaryDirectory() as td:
            for cc in ('gcc', 'clang'):
                out=Path(td)/cc
                subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=undefined', '-fno-sanitize-recover=all',
                                '-DFZX_ROAD_TILE_PROFILE=1', '-I'+str(ROOT/'include'),
                                str(ROOT/'src/fzx_road_profile.c'), str(ROOT/'tests/test_road_profile.c'),
                                '-o', str(out)], check=True)
                subprocess.run([str(out)], check=True)


if __name__ == '__main__':
    unittest.main()
