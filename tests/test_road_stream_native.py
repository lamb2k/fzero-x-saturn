"""Actual C3 loader/wrapper TU with synthetic collaborators; HOST only."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class RoadStreamNative(unittest.TestCase):
    def test_load_draw_and_controls(self):
        source = (ROOT / 'saturn/road_stream_native.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-road-stream-native-') as directory:
            tmp = Path(directory)
            (tmp / 'road_lod_identity.h').write_text('#define FZX_ROAD_LOD_BYTES 256u\n'
                '#define FZX_ROAD_LOD_FILE_HASH 11u\n#define FZX_ROAD_LOD_TEXTURE_BYTES 512u\n'
                '#define FZX_ROAD_LOD_TEXTURE_FILE_HASH 22u\n')

            def run(text, cc):
                (tmp / 'road_stream_under_test.c').write_text(text)
                cmd = [cc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wshadow',
                       '-fsanitize=undefined', '-fno-sanitize-recover=all', '-no-pie',
                       '-DFZX_SOURCE_MACHINE_MENU=1', '-Wl,--defsym,sat_stub_data_end=0x00200000',
                       '-Wl,--defsym,fzx_input_log=0x00203000']
                cmd += ['-I' + str(p) for p in (tmp, ROOT / 'tests/road_stream_native_stub',
                    ROOT / 'saturn', ROOT / 'include', ROOT / 'ref', ROOT / 'third_party/SH2ZAM/saturn/include')]
                cmd += [str(ROOT / 'tests/road_stream_native_driver.c'), '-o', str(tmp / 'test')]
                build = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                self.assertEqual(build.returncode, 0, build.stderr)
                return subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=10)

            for cc in ('gcc', 'clang'):
                result = run(source, cc)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('ROAD_STREAM_NATIVE_HOST_PASS', result.stdout)
                print(cc + ': ' + result.stdout.strip())
            controls = (
                ('dram_cart_id_get()!=DRAM_CART_ID_4MIB', 'false'),
                ('fzx_road_load_status.status!=1', 'false'),
                ('FZX_ROAD_LOD_TEXTURE_FILE_HASH))', 'FZX_ROAD_LOD_FILE_HASH))'),
                ('capacity=FZX_ROAD_STREAM_MAX_DRAWS+1', 'capacity=FZX_ROAD_STREAM_MAX_DRAWS+2'),
                ('pa,pb,0,packets', 'pa,pb,1,packets'),
                ('||emitted!=n', ''),
                ('view->clip[2]-1', 'view->clip[2]'),
                ('a[(int)i-8]!=GUARD', 'a[(int)i-8]==UINT32_MAX'),
                ('d[i]!=GUARD', 'd[i]==UINT32_MAX'),
            )
            for before, after in controls:
                self.assertEqual(source.count(before), 1, before)
                result = run(source.replace(before, after), 'gcc')
                self.assertNotEqual(result.returncode, 0, before)
                self.assertIn('line ', result.stderr)
                print('rejected: ' + before)


if __name__ == '__main__':
    unittest.main()
