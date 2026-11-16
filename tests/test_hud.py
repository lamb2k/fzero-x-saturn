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
typedef unsigned u32;
typedef float f32;
typedef unsigned Gfx;
#define UNUSED
#define MAX_TIMER (60*60*1000-1)
#define TIMER_DIGIT_PRIME 10
#define TIMER_DIGIT_DOUBLE_PRIME 11
#define GAMEMODE_PRACTICE 1
#define GAMEMODE_DEATH_RACE 2
#define RACER_STATE_FINISHED 1
#define RACER_STATE_CAN_BOOST 2
#define GPACK_RGBA5551(r,g,b,a) ((((r)>>3)<<11)|(((g)>>3)<<6)|(((b)>>3)<<1)|(a))
static struct { float speed, energy, maxEnergy; int stateFlags; } gRacers[4];
static struct { int lapFlashTimer; } D_i3_80141EA8[4];
static int gGameMode, gTotalLapCount, gPlayerLapNumbers[4], gPlayerMinimapLapCounterToggle[4];
static s32 Math_Round(float x) { return (s32)(x+0.5f); }
static fzx_hud_atlas_t atlas;
static fzx_hud_list_t expected;
static unsigned current_texture;
static u32 fill_color;
static uint16_t expected_pixels[320*224], actual_pixels[320*224];
static uint16_t native_color(unsigned c) {
    return (uint16_t)(0x8000u | ((c>>11)&31u) | (((c>>6)&31u)<<5) | (((c>>1)&31u)<<10));
}
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
    for(int y=t;y<=b;++y) for(int x=l;x<=r;++x) {
        assert(x>=0 && x<320 && y>=0 && y<224);
        expected_pixels[y*320+x]=native_color((x&1)?fill_color&0xffffu:fill_color>>16);
    }
}
#define gSPDisplayList(...) ((void)0)
#define gDPPipeSync(...) ((void)0)
#define gDPSetCycleType(...) ((void)0)
#define gDPSetFillColor(g,c) (fill_color=(c))
#define gDPFillRectangle(g,l,t,r,b) capture_fill(l,t,r,b)
#define gSPTextureRectangle(g,l,t,r,b,tile,s,uv_y,ds,dt) capture_tex(l,t,r,b,uv_y,ds)
#define gDPLoadTextureBlock(g,img,...) (current_texture=(img))
#define aSpeedDigitsTex FZX_HUD_SPEED
#define aKmhTex FZX_HUD_KMH
'''

DRIVER = r'''
static void compare(unsigned speed,unsigned time,unsigned lap,unsigned total,bool finished,
                    unsigned energy_width,bool boost) {
    fzx_hud_snapshot_t state={time,speed,(uint16_t)lap,(uint16_t)total,finished,
                             (uint16_t)energy_width,boost,false};
    fzx_hud_list_t actual;
    assert(fzx_hud_build(&atlas,&state,&actual));
    memset(&expected,0,sizeof(expected));
    memset(expected_pixels,0,sizeof(expected_pixels));
    memset(actual_pixels,0,sizeof(actual_pixels));
    gRacers[0].speed=(float)speed/21.6f;
    gRacers[0].energy=(float)energy_width;
    gRacers[0].maxEnergy=68.0f;
    gRacers[0].stateFlags=(finished?RACER_STATE_FINISHED:0)|(boost?RACER_STATE_CAN_BOOST:0);
    gTotalLapCount=(int)total;
    gPlayerLapNumbers[0]=(int)lap;
    Gfx dummy=0;
    current_texture=FZX_HUD_TIME;
    Hud_DrawTimeRectangle(&dummy,0,0);
    current_texture=FZX_HUD_TIMER;
    Hud_DrawPlayerTimer(&dummy,(int)time,0,0);
    current_texture=FZX_HUD_ENERGY;
    Hud_DrawEnergyOutlineRectangle(&dummy,0,0);
    current_texture=FZX_HUD_LAP;
    Hud_DrawLapRectangle(&dummy,0,0);
    current_texture=FZX_HUD_LAP_SYMBOLS;
    Hud_DrawLapCounter(&dummy,0,0);
    Hud_DrawEnergyBar(&dummy,0,0);
    Hud_DrawPlayerSpeed(&dummy,0,0);
#ifdef FZX_HUD_CONTROL
    if(expected.count>1) expected.draws[1].offset+=256u; /* wrong timer glyph */
#endif
    unsigned sprite=0;
    assert(actual.count<=FZX_HUD_MAX_DRAWS);
    for(unsigned i=0;i<actual.count;++i) {
        const fzx_hud_draw_t *a=&actual.draws[i];
        if(a->color) {
            for(unsigned y=0;y<a->height;++y) for(unsigned x=0;x<a->width;++x) {
                assert(a->x+(int)x<320 && a->y+(int)y<224);
                actual_pixels[(a->y+y)*320+a->x+x]=a->color;
            }
            continue;
        }
        assert(sprite<expected.count);
        const fzx_hud_draw_t *e=&expected.draws[sprite++];
        if(a->offset!=e->offset || a->x!=e->x || a->y!=e->y || a->width!=e->width ||
           a->height!=e->height || a->color!=e->color) {
            fprintf(stderr,"HUD differs speed=%u time=%u lap=%u at draw=%u\n",speed,time,lap,i);
            exit(1);
        }
    }
    assert(sprite==expected.count);
#ifdef FZX_HUD_COLOR_CONTROL
    expected_pixels[28*320+222]^=1u;
#endif
    if(memcmp(actual_pixels,expected_pixels,sizeof(actual_pixels))) {
        fprintf(stderr,"HUD fill differs energy=%u boost=%u finished=%u\n",energy_width,boost,finished);
        exit(1);
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
        compare(speed,(speed*7919u)%7200000u,1u+speed%6u,1u+speed%5u,(speed&1u)!=0,
                speed%69u,(speed&2u)!=0);
        ++cases;
    }
    const unsigned times[]={0,9,10,999,1000,59999,60000,3599998,3599999,3600000};
    for(unsigned i=0;i<sizeof(times)/sizeof(times[0]);++i) {
        compare(0,times[i],1,3,false,68,false); ++cases;
    }
    for(unsigned energy=0;energy<=68;++energy)
        for(unsigned flags=0;flags<4;++flags) {
            compare(9999,0,1,3,(flags&1u)!=0,energy,(flags&2u)!=0); ++cases;
        }
    fzx_hud_snapshot_t state={0,0,1,3,false,0,false,false};
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
    state.speed_kmh=0; state.energy_width=69;
    assert(!fzx_hud_build(&atlas,&state,&output));
    state.energy_width=0;
    assert(fzx_hud_build(&atlas,&state,&output));
    unsigned normal_count=output.count;
    state.hide_timer=true;
    assert(fzx_hud_build(&atlas,&state,&output));
    assert(output.count+8u==normal_count);
    assert(output.draws[0].offset==atlas.textures[FZX_HUD_TIME].offset);
    for(unsigned i=0;i<output.count;++i) {
        const fzx_hud_draw_t *d=&output.draws[i];
        assert(d->color || d->offset<atlas.textures[FZX_HUD_TIMER].offset ||
               d->offset>=atlas.textures[FZX_HUD_LAP].offset);
    }
    printf("HOST HUD source differential: %u cases, bounds controls PASS\n",cases);
    return 0;
}
'''


def oracle_source():
    text = SOURCE.read_text()
    tables = []
    for name in ("sPlayerTimerPositions", "sPlayerTimePositions", "sSpeedPositions", "sLapPositions",
                 "sEnergyOutlinePositions", "sEnergyBarPositions"):
        table = re.search(r"s32 " + name + r"\[\]\[4\]\[2\] = \{.*?\n\};", text, re.S)
        if not table:
            raise ValueError("Missing source HUD position table")
        tables.append(table[0])
    colors = re.search(r"u32 sEnergyBarFillColors\[\] = \{.*?\n\};", text, re.S)
    if not colors:
        raise ValueError("Missing source energy colors")
    tables.append(colors[0])
    functions = []
    for name in ("Hud_DrawTimerDigitRectangle", "Hud_DrawPlayerTimer", "Hud_DrawTimeRectangle",
                 "Hud_DrawPlayerSpeed", "Hud_DrawLapRectangle", "Hud_DrawLapCounter",
                 "Hud_DrawEnergyOutlineRectangle", "Hud_DrawEnergyBar"):
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
            for control in (None, "FZX_HUD_CONTROL", "FZX_HUD_COLOR_CONTROL"):
                command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                           # Source switches assume valid player indices; this oracle uses 0.
                           "-Wno-unused-variable", "-Wno-unused-parameter", "-Wno-maybe-uninitialized",
                           "-I", str(ROOT / "include"),
                           str(path / "oracle.c"), str(ROOT / "src/fzx_hud.c"), "-o", str(path / "hud")]
                if control:
                    command.append(f"-D{control}=1")
                compiled = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                result = subprocess.run([str(path / "hud")], capture_output=True, text=True)
                if control:
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("HUD", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("10286 cases", result.stdout)


if __name__ == "__main__":
    unittest.main()
