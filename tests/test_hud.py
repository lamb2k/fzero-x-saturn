"""Differential native HUD tests against the actual pinned source functions.

The graphics macros record rectangles instead of drawing. No ROM assets or
authored replacement HUD enter the oracle. PORT and EXPANSION_KIT stay undefined.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "upstream/G-Diffuser/decomp/src/overlays/ovl_i3/hud.c"

PRELUDE = r'''
#include "fzx_hud.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int s32;
typedef float f32;
typedef unsigned Gfx;
#define UNUSED
#define MAX_TIMER (60*60*1000-1)
#define TIMER_DIGIT_PRIME 10
#define TIMER_DIGIT_DOUBLE_PRIME 11
#define GAMEMODE_PRACTICE 1
#define GAMEMODE_DEATH_RACE 2
#define RACER_STATE_FINISHED 1
static struct { float speed; int stateFlags; } gRacers[4];
static struct { int lapFlashTimer; } D_i3_80141EA8[4];
static int gGameMode, gTotalLapCount, gPlayerLapNumbers[4], gPlayerMinimapLapCounterToggle[4];
static s32 Math_Round(float x) { return (s32)(x+0.5f); }
static fzx_hud_atlas_t atlas;
static fzx_hud_list_t expected;
static unsigned current_texture;
static void capture_tex(int l,int t,int r,int b,int uv_y,int step) {
    const fzx_hud_texture_t *tex=&atlas.textures[current_texture];
    const unsigned height=(unsigned)((b-t)/4+(step==4096));
    const unsigned width=(unsigned)((r-l)/4+(step==4096));
    /* Native normal sprites include only transparent row padding. */
    assert(((width+7u)&~7u)==tex->stride);
    assert(expected.count<FZX_HUD_MAX_DRAWS);
    expected.draws[expected.count++]=(fzx_hud_draw_t){
        tex->offset+(unsigned)(uv_y/32)*tex->stride*2u,
        (int16_t)(l/4),(int16_t)(t/4),tex->stride,(uint16_t)height,0};
}
static void capture_fill(int l,int t,int r,int b) {
    assert(expected.count<FZX_HUD_MAX_DRAWS);
    expected.draws[expected.count++]=(fzx_hud_draw_t){0,(int16_t)l,(int16_t)t,
        (uint16_t)(r-l+1),(uint16_t)(b-t+1),0x8000};
}
#define gSPDisplayList(...) ((void)0)
#define gDPPipeSync(...) ((void)0)
#define gDPSetCycleType(...) ((void)0)
#define gDPSetFillColor(...) ((void)0)
#define gDPFillRectangle(g,l,t,r,b) capture_fill(l,t,r,b)
#define gSPTextureRectangle(g,l,t,r,b,tile,s,uv_y,ds,dt) capture_tex(l,t,r,b,uv_y,ds)
#define gDPLoadTextureBlock(g,img,...) (current_texture=(img))
#define aSpeedDigitsTex FZX_HUD_SPEED
#define aKmhTex FZX_HUD_KMH
'''

DRIVER = r'''
static void compare(unsigned speed,unsigned time,unsigned lap,unsigned total,bool finished) {
    fzx_hud_snapshot_t state={time,speed,(uint16_t)lap,(uint16_t)total,finished};
    fzx_hud_list_t actual;
    assert(fzx_hud_build(&atlas,&state,&actual));
    memset(&expected,0,sizeof(expected));
    gRacers[0].speed=(float)speed/21.6f;
    gRacers[0].stateFlags=finished?RACER_STATE_FINISHED:0;
    gTotalLapCount=(int)total;
    gPlayerLapNumbers[0]=(int)lap;
    Gfx dummy=0;
    current_texture=FZX_HUD_TIME;
    Hud_DrawTimeRectangle(&dummy,0,0);
    current_texture=FZX_HUD_TIMER;
    Hud_DrawPlayerTimer(&dummy,(int)time,0,0);
    current_texture=FZX_HUD_LAP;
    Hud_DrawLapRectangle(&dummy,0,0);
    current_texture=FZX_HUD_LAP_SYMBOLS;
    Hud_DrawLapCounter(&dummy,0,0);
    Hud_DrawPlayerSpeed(&dummy,0,0);
#ifdef FZX_HUD_CONTROL
    if(expected.count>1) expected.draws[1].offset+=256u; /* wrong timer glyph */
#endif
    if(actual.count!=expected.count) { fprintf(stderr,"HUD draw count differs\n"); exit(1); }
    for(unsigned i=0;i<actual.count;++i) {
        const fzx_hud_draw_t *a=&actual.draws[i],*e=&expected.draws[i];
        if(a->offset!=e->offset || a->x!=e->x || a->y!=e->y || a->width!=e->width ||
           a->height!=e->height || a->color!=e->color) {
            fprintf(stderr,"HUD differs speed=%u time=%u lap=%u at draw=%u\n",speed,time,lap,i);
            exit(1);
        }
    }
}
int main(void) {
    const unsigned size[7][2]={{24,16},{72,16},{8,72},{12,160},{20,16},{8,224},{16,12}};
    for(unsigned i=0;i<7;++i) {
        const unsigned stride=(size[i][0]+7u)&~7u;
        atlas.textures[i]=(fzx_hud_texture_t){atlas.bytes,(uint16_t)size[i][0],(uint16_t)stride,(uint16_t)size[i][1]};
        atlas.bytes+=stride*size[i][1]*2u;
    }
    unsigned cases=0;
    for(unsigned speed=0;speed<10000;++speed) {
        compare(speed,(speed*7919u)%7200000u,1u+speed%6u,1u+speed%5u,(speed&1u)!=0);
        ++cases;
    }
    const unsigned times[]={0,9,10,999,1000,59999,60000,3599998,3599999,3600000};
    for(unsigned i=0;i<sizeof(times)/sizeof(times[0]);++i) {
        compare(0,times[i],1,3,false); ++cases;
    }
    fzx_hud_snapshot_t state={0,0,1,3,false};
    fzx_hud_list_t output,sentinel;
    memset(&sentinel,0x5a,sizeof(sentinel)); output=sentinel;
    fzx_hud_atlas_t bad=atlas;
    bad.textures[FZX_HUD_TIMER].offset+=1u;
    assert(!fzx_hud_build(&bad,&state,&output));
    assert(memcmp(&output,&sentinel,sizeof(output))==0);
    bad=atlas; --bad.bytes;
    assert(!fzx_hud_build(&bad,&state,&output));
    state.speed_kmh=10000;
    assert(!fzx_hud_build(&atlas,&state,&output));
    assert(!fzx_hud_build(NULL,&state,&output));
    printf("HOST HUD source differential: %u cases, bounds controls PASS\n",cases);
    return 0;
}
'''


def oracle_source():
    text = SOURCE.read_text()
    tables = []
    for name in ("sPlayerTimerPositions", "sPlayerTimePositions", "sSpeedPositions", "sLapPositions"):
        table = re.search(r"s32 " + name + r"\[\]\[4\]\[2\] = \{.*?\n\};", text, re.S)
        if not table:
            raise ValueError("Missing source HUD position table")
        tables.append(table[0])
    functions = []
    for name in ("Hud_DrawTimerDigitRectangle", "Hud_DrawPlayerTimer", "Hud_DrawTimeRectangle",
                 "Hud_DrawPlayerSpeed", "Hud_DrawLapRectangle", "Hud_DrawLapCounter"):
        function = re.search(r"Gfx\* " + name + r"\(.*?\n\}", text, re.S)
        if not function:
            raise ValueError("Missing source HUD function")
        functions.append(function[0])
    return '#include <stdlib.h>\n' + PRELUDE + '\n'.join(tables + functions) + DRIVER


@unittest.skipUnless(SOURCE.exists() and shutil.which("cc"), "Needs pinned source and host C compiler")
class HudSourceTests(unittest.TestCase):
    def test_source_differential_and_wrong_glyph_control(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "oracle.c").write_text(oracle_source())
            for control in (False, True):
                command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                           # Source switches assume valid player indices; this oracle uses 0.
                           "-Wno-unused-variable", "-Wno-unused-parameter", "-Wno-maybe-uninitialized",
                           "-I", str(ROOT / "include"),
                           str(path / "oracle.c"), str(ROOT / "src/fzx_hud.c"), "-o", str(path / "hud")]
                if control:
                    command.append("-DFZX_HUD_CONTROL=1")
                compiled = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                result = subprocess.run([str(path / "hud")], capture_output=True, text=True)
                if control:
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("HUD differs", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("10010 cases", result.stdout)


if __name__ == "__main__":
    unittest.main()
