"""FZX_RBG_FAST (include/fzx_rbg_fast.h): HOST checks against the per-line originals.

The edge-walked line window is compared with the original per-line window
(extracted from src/fzx_floor_rbg.c at test time) on random convex polygons,
including ones that leave the screen and cross the clip lines. The divider
projection is compared with fzx_view_project (on HOST both divide portably;
the SH-2 divider path is the floor window's existing one).
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fzx_view.h"
#include "fzx_rbg_fast.h"
typedef struct { uint16_t window[224][2]; uint32_t visible_lines; } out_t;
static int64_t divide(int64_t n,int32_t d){return n/d;}
@ORIGINAL@
static uint64_t seed=0x9e3779b97f4a7c15u;
static int32_t rnd(int32_t lo,int32_t hi){seed=seed*6364136223846793005u+1442695040888963407u;
    return lo+(int32_t)((seed>>33)%(uint64_t)(hi-lo+1));}
static int cmp_angle(const void *a,const void *b,void *c);
int main(void){
    fzx_view_t v;memset(&v,0,sizeof v);
    v.clip[0]=0;v.clip[1]=8;v.clip[2]=320;v.clip[3]=216;v.focal[0]=v.focal[1]=200<<16;
    v.center[0]=160<<16;v.center[1]=112<<16;v.near_plane=16<<16;v.far_plane=30000*65536;
    long lines=0,diff_lines=0,bad=0,polys=0;
    for(int t=0;t<20000;++t){
        /* Convex polygon: points on an ellipse, ascending angle. */
        const int n=3+rnd(0,7);int32_t xy[10][2];
        const int64_t cx=rnd(-200,520)*65536LL,cy=rnd(-150,370)*65536LL,rx=rnd(1,900)*65536LL,ry=rnd(1,600)*65536LL;
        int angles[10];for(int i=0;i<n;++i)angles[i]=rnd(0,3599);
        for(int i=0;i<n;++i)for(int j=i+1;j<n;++j)if(angles[j]<angles[i]){int s=angles[i];angles[i]=angles[j];angles[j]=s;}
        for(int i=0;i<n;++i){const double a=angles[i]*3.14159265358979/1800.0;
            xy[i][0]=(int32_t)(cx+(int64_t)(rx*__builtin_cos(a)))+rnd(-65535,65535);
            xy[i][1]=(int32_t)(cy+(int64_t)(ry*__builtin_sin(a)))+rnd(-65535,65535);}
        out_t a,b;memset(&a,0,sizeof a);memset(&b,0,sizeof b);
        window(&v,(const int32_t (*)[2])xy,(unsigned)n,(void *)&a);
        fzx_rbg_window(&v,(const int32_t (*)[2])xy,(unsigned)n,b.window,&b.visible_lines);
        ++polys;
        for(int y=0;y<224;++y){
            if(a.window[y][0]!=640||b.window[y][0]!=640)++lines;
            if(a.window[y][0]!=b.window[y][0]||a.window[y][1]!=b.window[y][1]){
                ++diff_lines;
                const int d0=abs((int)a.window[y][0]-(int)b.window[y][0]),d1=abs((int)a.window[y][1]-(int)b.window[y][1]);
                /* Allowed: one pixel (2 units) on an edge; never an empty/non-empty change except at one pixel. */
                /* A one-pixel span on one side and none on the other is also a one-pixel difference. */
                const int sliver=(a.window[y][0]==640&&b.window[y][0]==b.window[y][1])||(b.window[y][0]==640&&a.window[y][0]==a.window[y][1]);
                if((d0>2||d1>2)&&!sliver){++bad;if(getenv("RBG_DEBUG"))printf("bad y=%d orig %u..%u fast %u..%u\n",y,a.window[y][0],a.window[y][1],b.window[y][0],b.window[y][1]);}
            }
        }
    }
    /* Projection: identical values. */
    long proj=0,proj_diff=0;
    for(int t=0;t<200000;++t){
        const int32_t p[3]={rnd(-2000000,2000000)*64,rnd(-2000000,2000000)*64,rnd(-5,4000000)*64};
        int32_t o1[2]={0,0},o2[2]={0,0};
        const bool r1=fzx_view_project(&v,p,o1),r2=fzx_rbg_project(&v,p,o2);
        ++proj;if(r1!=r2||o1[0]!=o2[0]||o1[1]!=o2[1])++proj_diff;
    }
    /* Clip: identical vertex counts and coordinates. */
    long clips=0,clip_diff=0,clip_nonempty=0;
    for(int t=0;t<200000;++t){
        int32_t q[4][3];const int32_t span=rnd(1,4)==1?2000000:200000;
        for(int i=0;i<4;++i){q[i][0]=rnd(-span,span)*64;q[i][1]=rnd(-span,span)*64;q[i][2]=rnd(-span/4,span)*64;}
        int32_t o1[10][3],o2[10][3];memset(o1,0,sizeof o1);memset(o2,0,sizeof o2);
        const int n1=fzx_view_clip(&v,(const int32_t (*)[3])q,4,o1),n2=fzx_rbg_clip(&v,(const int32_t (*)[3])q,4,o2);
        ++clips;if(n1>0)++clip_nonempty;
        if(n1!=n2||(n1>0&&memcmp(o1,o2,sizeof(o1[0])*(size_t)n1)))++clip_diff;
    }
    printf("clips %ld nonempty %ld clip_differences %ld\n",clips,clip_nonempty,clip_diff);
    if(clip_diff)return 1;
    printf("polygons %ld visible_lines %ld differing_lines %ld over_one_pixel %ld projections %ld projection_differences %ld\n",
        polys,lines,diff_lines,bad,proj,proj_diff);
    return (bad||proj_diff||diff_lines*1000>lines)?1:0;
}
'''


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc'), 'POSIX GCC required')
class RbgFastTests(unittest.TestCase):
    def original_window(self):
        source = (ROOT / 'src/fzx_floor_rbg.c').read_text()
        m = re.search(r'static void window\(const fzx_view_t \*v,const int32_t xy\[10\]\[2\],unsigned count,fzx_floor_rbg_t \*out\) \{.*?\n\}\n', source, re.S)
        self.assertIsNotNone(m)
        return m.group(0).replace('fzx_floor_rbg_t *out', 'out_t *out')

    def run_driver(self, original):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            (d / 'driver.c').write_text(DRIVER.replace('@ORIGINAL@', original).replace(
                'static int cmp_angle(const void *a,const void *b,void *c);', ''))
            exe = d / 'driver'
            r = subprocess.run(['gcc', '-std=gnu11', '-O2', '-Wall', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                                '-I' + str(ROOT / 'include'), str(d / 'driver.c'), str(ROOT / 'src/fzx_view.c'),
                                '-lm', '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_window_and_projection_match_originals(self):
        run = self.run_driver(self.original_window())
        print(run.stdout.strip())
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_control_detects_a_broken_window(self):
        # Control: shift the original's right edge by two pixels; the comparison must fail.
        broken = self.original_window().replace('hi=(int)((right-32768)/65536);', 'hi=(int)((right-32768)/65536)+2;')
        self.assertNotEqual(broken, self.original_window())
        self.assertNotEqual(self.run_driver(broken).returncode, 0)


if __name__ == '__main__':
    unittest.main()
