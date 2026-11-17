"""Exhaustive HOST check of the C3 stream's exact division replacements.

div255 and div65025 in src/fzx_road_stream_draw.c replace libgcc divisions on
the per-corner fog path. They must equal C's truncating division over their
whole reachable domain: |n| <= 65534 (the path needs |n| <= 15810) and
0 <= n <= 2048287 = (255*255)*31+32512. Mutated constants must fail.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/fzx_road_stream_draw.c'
DRIVER = r'''
#include <stdint.h>
#include <stdio.h>
%s
int main(void){
    for(int32_t n=-65534;n<=65534;++n)if(div255(n)!=n/255){printf("div255 %%d\n",(int)n);return 1;}
    for(uint32_t n=0;n<=2048287u;++n)if(div65025(n)!=n/65025u){printf("div65025 %%u\n",(unsigned)n);return 1;}
    puts("ROAD_STREAM_ARITH_HOST_PASS div255 div65025 exhaustive");
    return 0;
}
'''


def functions(text):
    found = []
    for name in ('div255', 'div65025'):
        match = re.search(r'static [a-z0-9_]+ ' + name + r'\(.*?\n}\n', text, re.S)
        if not match:
            raise AssertionError('missing ' + name)
        found.append(match.group(0))
    return '\n'.join(found)


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'POSIX GCC/Clang required')
class RoadStreamArith(unittest.TestCase):
    def run_driver(self, body, cc):
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'arith.c'
            exe = Path(tmp) / 'arith'
            c.write_text(DRIVER % body)
            build = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-fsanitize=undefined',
                                    '-fno-sanitize-recover=all', str(c), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_exhaustive_and_controls(self):
        body = functions(SOURCE.read_text())
        for cc in ('gcc', 'clang'):
            result = self.run_driver(body, cc)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(cc + ': ' + result.stdout.strip())
        for before, after in (('2113633', '2113632'), ('>>37', '>>36'),
                              ('+1u)>>8', ')>>8'), ('(a>>8)', '(a>>9)')):
            self.assertEqual(body.count(before), 1, before)
            result = self.run_driver(body.replace(before, after), 'gcc')
            self.assertNotEqual(result.returncode, 0, before)
            print('rejected: ' + before)


if __name__ == '__main__':
    unittest.main()
