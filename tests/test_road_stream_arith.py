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


FOG_DRIVER = r'''
#include "fzx_road_stream_draw.c"
#include <stdio.h>
static unsigned reference(const fzx_road_stream_draw_work_t *w,int32_t z){
    const uint32_t d=z>0?(uint32_t)z:0;unsigned r=0;
    for(unsigned i=1;i<256;++i)if(w->fog_depth[i]<=d)r=i;
    return r;
}
static unsigned full_search(const fzx_road_stream_draw_work_t *w,int32_t z){
    fzx_road_stream_draw_work_t copy=*w;copy.fog_shift=255;return fog_lookup(&copy,z);
}
int main(void){
    static fzx_road_stream_draw_work_t w;unsigned tables=0;unsigned long long checks=0;
    const int32_t pas[]={1073741824,1100000000,1200000000,1500000000};
    const int32_t pbs[]={-1,-8192,-40000,-81920,-500000};
    const int starts[]={0,250,500,750,990,996};
    const int32_t fars[]={4000,48000,128000,400000,4000000};
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<5;++b)for(unsigned s=0;s<6;++s)for(unsigned f=0;f<5;++f){
        RefRoadPlan plan;memset(&plan,0,sizeof(plan));plan.fog_start=starts[s];
        sh2z_view_t view;memset(&view,0,sizeof(view));view.near_z=16;view.far_z=fars[f];
        fzx_road_stream_draw_stats_t st;memset(&st,0,sizeof(st));w.fog_valid=false;
        fog_prepare(&w,&view,&plan,pas[a],pbs[b],&st);
        if(w.fog_shift==255u){puts("unexpected non-monotonic table");return 1;}
        ++tables;
        for(unsigned i=0;i<256;++i)for(int d=-2;d<=2;++d){const int64_t z=(int64_t)w.fog_depth[i]+d;
            if(z<INT32_MIN||z>INT32_MAX)continue;
            ++checks;if(fog_lookup(&w,(int32_t)z)!=reference(&w,(int32_t)z)||full_search(&w,(int32_t)z)!=reference(&w,(int32_t)z)){printf("table %u z %lld\n",tables,(long long)z);return 1;}}
        const int32_t extreme[]={INT32_MIN,-1,0,1,INT32_MAX};
        for(unsigned i=0;i<5;++i){++checks;if(fog_lookup(&w,extreme[i])!=reference(&w,extreme[i])){puts("extreme");return 1;}}
        for(int64_t z=-64;z<=(int64_t)fars[f]+64;z+=1+fars[f]/40000){
            ++checks;if(fog_lookup(&w,(int32_t)z)!=reference(&w,(int32_t)z)){printf("sweep %lld\n",(long long)z);return 1;}}
    }
    printf("ROAD_STREAM_FOG_BUCKET_HOST_PASS tables=%u checks=%llu\n",tables,checks);
    return 0;
}
'''


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'POSIX GCC/Clang required')
class RoadStreamFogBucket(unittest.TestCase):
    """Bucketed fog lookup equals the full search for every tested table and depth."""
    def run_driver(self, source, cc):
        lib = ROOT / 'third_party/SH2ZAM/saturn'
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            src.mkdir()
            (src / 'fzx_road_stream_draw.c').write_text(source)
            c = Path(tmp) / 'fog.c'
            exe = Path(tmp) / 'fog'
            c.write_text(FOG_DRIVER)
            files = [c, ROOT / 'src/fzx_road_stream.c', ROOT / 'src/fzx_road_pack.c',
                     ROOT / 'src/fzx_view.c', ROOT / 'src/fzx_view16.c', ROOT / 'src/fzx_view16_center.c',
                     lib / 'src/sh2zam_view.c', lib / 'src/sh2zam_view_round.c',
                     lib / 'src/sh2zam_view_guard_src.c', lib / 'src/sh2zam_view_center.c',
                     lib / 'src/sh2zam_view_guard_src_center.c',
                     lib / 'src/sh2zam_view_merge_tier.c']
            build = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-misleading-indentation', '-fsanitize=undefined',
                                    '-fno-sanitize-recover=all', '-DSH2Z_FORCE_PORTABLE=1',
                                    '-DFZX_SOURCE_CAMERA=1', '-I' + str(src), '-I' + str(ROOT / 'include'),
                                    '-I' + str(ROOT / 'ref'), '-I' + str(lib / 'include'),
                                    *map(str, files), '-lm', '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=600)

    def test_bucket_equals_search(self):
        source = SOURCE.read_text()
        for cc in ('gcc', 'clang'):
            result = self.run_driver(source, cc)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(cc + ': ' + result.stdout.strip())
        for before, after in (('hi=w->fog_bucket[b+1u]', 'hi=w->fog_bucket[b]'),
                              ('if(b>255u)return 255;', 'if(b>255u)return 254;'),
                              ('if(w->fog_depth[middle]<=depth)lo=middle;', 'if(w->fog_depth[middle]<depth)lo=middle;')):
            self.assertEqual(source.count(before), 1, before)
            result = self.run_driver(source.replace(before, after), 'gcc')
            self.assertNotEqual(result.returncode, 0, before)
            print('rejected: ' + before)


if __name__ == '__main__':
    unittest.main()
