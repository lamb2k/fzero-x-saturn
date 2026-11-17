"""ROM-free native countdown pack admission, selection and corruption controls."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from test_countdown_pack import fixture, build, fnv

ROOT = Path(__file__).resolve().parents[1]


def rehash(data):
    data = bytearray(data)
    data[44:48] = bytes(4)
    struct.pack_into('>I', data, 44, fnv(data))
    return bytes(data)


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
class CountdownNativePackTests(unittest.TestCase):
    def test_admission_and_selections(self):
        meta, pixels, _ = build(*fixture())
        source = (ROOT/'src/fzx_countdown_pack.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            def compile(text, cc):
                (directory/'pack.c').write_text(text)
                exe = directory/cc
                subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wconversion', '-Wshadow', '-Wpedantic', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', '-I'+str(ROOT/'include'), '-I'+str(ROOT/'ref'),
                    '-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(directory/'pack.c'), str(ROOT/'tests/countdown_pack_driver.c'),
                    '-o', str(exe)], check=True, capture_output=True)
                return exe
            def run(exe, data, wrong_hash=False, texture_hash=fnv(pixels)):
                path = directory/'pack';path.write_bytes(data)
                return subprocess.run([str(exe), str(path), str(len(pixels)), str(texture_hash),
                                       str(fnv(data) ^ int(wrong_hash))], capture_output=True)
            mutations = []
            for at, fmt, value in ((20, '>I', 91), (36, '>I', 1544), (48, '>I', 1),
                    (64+14, '>H', 1), (1536, '>H', 92), (1536+2, '>H', 0),
                    (1536+8, '>H', 1), (1536+10, '>H', 2), (1536+14, '>H', 1),
                    (1904, '>H', 7), (1904+2, '>H', 129), (1904+4, '>I', 0xfffffff8),
                    (1904+8, '>H', 1), (1904+10, '>H', 1), (1904+12, '>H', 8),
                    (1904+14, '>H', 1)):
                data = bytearray(meta);struct.pack_into(fmt, data, at, value)
                mutations.append(rehash(data))
            outputs = []
            for cc in ('gcc', 'clang'):
                exe = compile(source, cc)
                result = run(exe, meta);self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append(result.stdout)
                for data in (*mutations, meta[:-1], meta+bytes(8), bytes(64)):
                    result = run(exe, data);self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(run(exe, meta, wrong_hash=True).returncode, 1)
                self.assertEqual(run(exe, meta, texture_hash=fnv(pixels)^1).returncode, 1)
            self.assertEqual(*outputs)
            altered = source.replace('(unsigned)phase->panel-1', '(unsigned)phase->panel%4')
            self.assertNotEqual(source, altered)
            self.assertNotEqual(run(compile(altered, 'gcc'), meta).returncode, 0)


if __name__ == '__main__':
    unittest.main()
