"""Stream-enabled GfxPool placement; complements unchanged knob-off suite."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
from tests.test_machine_menu_workspace import YAUL, ROAD, REF

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class RoadStreamWorkspace(unittest.TestCase):
    def test_stream_allocation(self):
        original = (ROOT / 'saturn/race_workspace_native.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-stream-workspace-') as directory:
            tmp = Path(directory)
            for name, text in [('yaul.h', YAUL), ('road_native.h', ROAD), ('ref.h', REF),
                ('road_stream_native.h', '#include <stdint.h>\n#include <stdbool.h>\n'
                    'extern volatile uint32_t fzx_road_stream_status,fzx_road_stream_cart_end;\n'
                    'bool fzx_road_stream_guards(void);\n')]:
                (tmp / name).write_text(text)

            def run(source, cc):
                (tmp / 'workspace.c').write_text(source)
                cmd = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wshadow', '-Wpedantic',
                       '-fsanitize=undefined', '-fno-sanitize-recover=all', '-DFZX_SOURCE_ROAD_STREAM=1']
                cmd += ['-I' + str(p) for p in (tmp, ROOT / 'saturn', ROOT / 'include')]
                cmd += [str(tmp / 'workspace.c'), str(ROOT / 'tests/road_stream_workspace_driver.c'), '-o', str(tmp / 'test')]
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=10)

            for cc in ('gcc', 'clang'):
                result = run(original, cc)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(cc + ': ' + result.stdout.strip())
            controls = (
                ('+95u', '+63u'),
                ('fzx_road_stream_status!=1', 'false'),
                ('!fzx_road_stream_guards()', 'false'),
                ('fzx_road_stream_cart_end<base+fzx_native_road.bytes', 'false'),
            )
            for before, after in controls:
                self.assertEqual(original.count(before), 1, before)
                result = run(original.replace(before, after), 'gcc')
                self.assertNotEqual(result.returncode, 0, before)
                self.assertIn('line ', result.stderr)
                print('rejected: ' + before)
            # Upper admission has an independent downstream offset/pool bound.
            # Removing only the early upper check must not bypass that bound;
            # this is a redundancy check, NOT a rejected mutation claim.
            upper = 'fzx_road_stream_cart_end>base+size-GUARD_BYTES'
            self.assertEqual(original.count(upper), 1)
            result = run(original.replace(upper, 'false'), 'gcc')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('boundary beyond cart limit: rejected unchanged=1', result.stdout)
            self.assertIn('boundary UINT32_MAX: rejected unchanged=1', result.stdout)
            print('redundant upper admission removed: downstream bounds still reject without writes')


if __name__ == '__main__':
    unittest.main()
