"""ROM-free native C3 packet adapter; API recorder, not Saturn timing evidence."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class RoadStreamCommands(unittest.TestCase):
    def test_packets_and_controls(self):
        source = (ROOT / 'saturn/road_stream_commands.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-road-stream-commands-') as directory:
            tmp = Path(directory)

            def run(text, cc):
                (tmp / 'adapter.c').write_text(text)
                cmd = [cc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wshadow',
                       '-fsanitize=undefined', '-fno-sanitize-recover=all']
                cmd += ['-I' + str(p) for p in (ROOT / 'tests/road_stream_commands_stub',
                    ROOT / 'saturn', ROOT / 'include', ROOT / 'ref', ROOT / 'third_party/SH2ZAM/saturn/include')]
                cmd += [str(tmp / 'adapter.c'), str(ROOT / 'tests/road_stream_commands_driver.c'), '-o', str(tmp / 'test')]
                build = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                self.assertEqual(build.returncode, 0, build.stderr)
                return subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=10)

            for cc in ('gcc', 'clang'):
                result = run(source, cc)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('ROAD_STREAM_COMMANDS_HOST_PASS', result.stdout)
                print(cc + ': ' + result.stdout.strip())
            controls = (
                ('.end_code_disable=1', '.end_code_disable=0'),
                ('.trans_pixel_disable=1', '.trans_pixel_disable=0'),
                ('VDP1_CMDT_CC_GOURAUD', 'VDP1_CMDT_CC_REPLACE'),
                ('0xc210', '0xffff'),
                ('c->cmd_xc=p->x[2]', 'c->cmd_xc=p->x[1]'),
                ('memcpy(g,p->shade,8)', 'memcpy(g,p->shade,6)'),
                ('packet_count>capacity', 'packet_count>capacity+1u'),
                ('(m->palette&7u)', '(m->palette&0u)'),
                ('32u>texture_bytes-m->palette', '8u>texture_bytes-m->palette'),
            )
            for before, after in controls:
                self.assertEqual(source.count(before), 1, before)
                result = run(source.replace(before, after), 'gcc')
                self.assertNotEqual(result.returncode, 0, before)
                self.assertIn('line ', result.stderr)
                print('rejected: ' + before)


if __name__ == '__main__':
    unittest.main()

