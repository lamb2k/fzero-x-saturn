"""Native compact-pack admission against the offline synthetic source bake."""
from pathlib import Path
import os
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_road_bake import fixture, rehash
import road_bake as bake

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'), 'GCC/Clang required')
class RoadStreamTests(unittest.TestCase):
    def test_admission_and_corruption(self):
        metadata, textures, _ = bake.build(fixture(30), bytes(range(32)), [252, 192, 253])
        counts, offsets = bake.validate(metadata, textures)
        source = (ROOT/'src/fzx_road_stream.c').read_text()
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            def compile(text, cc):
                (d/'stream.c').write_text(text)
                subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wconversion', '-Wshadow', '-Wpedantic',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS', '')),
                    '-I'+str(ROOT/'include'), '-I'+str(ROOT/'ref'),
                    '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(d/'stream.c'), str(ROOT/'src/fzx_road_pack.c'),
                    str(ROOT/'tests/road_stream_driver.c'), '-o', str(d/'test')], check=True, capture_output=True)
            def run(data, texture_bytes=len(textures), wrong_hash=0):
                (d/'pack').write_bytes(data)
                return subprocess.run([str(d/'test'), str(d/'pack'), str(texture_bytes), str(wrong_hash)], capture_output=True)
            mutations = []
            def mutate(at, fmt, value):
                changed = bytearray(metadata)
                struct.pack_into(fmt, changed, at, value)
                mutations.append(rehash(changed))
            mutate(120, '>I', 168)  # section offset
            mutate(88, '>I', 0xffffffff)  # section extent overflow
            mutate(offsets[1], '>H', counts[0])  # vertex bound
            mutate(offsets[1]+2, '>H', 0)  # repeated vertex
            mutate(offsets[1]+10, '>H', 4)  # unknown quad flag
            mutate(offsets[1]+12, '>H', 3)  # source triangle identity
            mutate(offsets[2], '>I', 1)  # repetition differs from source UV
            wrong_repeat = mutations[-1]
            mutate(offsets[3], '>I', 1)  # missing source coverage
            mutate(offsets[4]+4, '>H', 7)  # unsupported period
            mutate(offsets[5]+6, '>H', 8)  # wrong tier
            mutate(offsets[5]+8, '>I', len(textures))  # pixels outside bank
            mutate(offsets[5]+20, '>I', 65)  # misaligned palette
            mutate(offsets[6]+10, '>H', counts[1])  # far provenance outside source
            mutate(156, '>I', 1)  # reserved header
            mutations += [metadata[:-1], metadata+bytes(8), bytes(160)]
            for cc in ('gcc', 'clang'):
                compile(source, cc)
                got = run(metadata)
                self.assertEqual(got.returncode, 0, got.stderr)
                self.assertEqual(list(map(int, got.stdout.split())),
                                 [counts[0], counts[1], counts[1], *counts[2:7]])
                self.assertEqual(run(metadata, wrong_hash=1).returncode, 1)
                self.assertEqual(run(metadata, texture_bytes=len(textures)-8).returncode, 1)
                for i, changed in enumerate(mutations):
                    got = run(changed)
                    self.assertEqual(got.returncode, 1, (cc, i, got.stderr))
            mutant = source.replace('if(repeat!=expected)return false;',
                                    'if(repeat!=expected && bytes==0)return false;')
            self.assertNotEqual(mutant, source)
            compile(mutant, 'gcc')
            self.assertEqual(run(wrong_repeat).returncode, 0, 'control must demonstrate wrong-repeat admission')


if __name__ == '__main__':
    unittest.main()
