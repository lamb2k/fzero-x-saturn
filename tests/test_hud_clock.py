"""Time Attack clock sequences against the pinned original HUD update function."""
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
#include <stdlib.h>
#include <string.h>
typedef int s32;
typedef short s16;
typedef signed char s8;
typedef unsigned Gfx;
#define UNUSED __attribute__((unused))
#define GAMEMODE_DEATH_RACE 2
#define GAMEMODE_PRACTICE 1
#define GAMEMODE_TIME_ATTACK 3
#define NA_SE_17 17
#define NA_SE_18 18
#define gDPPipeSync(...) ((void)0)
#define gDPSetRenderMode(...) ((void)0)
static struct { int lap, startNewPracticeLap, lapTimes[3], raceTime, position; } gRacers[4];
static struct { int lapFlashTimer, lapIntervalCounter, unk_0C; } D_i3_80141EA8[4];
static int gGameMode=GAMEMODE_TIME_ATTACK, gTotalLapCount=3, gPlayerLapNumbers[4], sPreviousLapTimes[4];
static int D_i3_8013EFFC=180, sSecondLapStarted, sFinalLapStarted;
s16 D_800E5FBC;
s16 gEnableRaceSfx;
s8 gGamePaused;
static unsigned selected, calls;
static void Audio_TriggerSystemSE(int se) { (void)se; }
static void func_i3_8012F450(int *x) { (void)x; }
static Gfx *Hud_DrawPlayerTimer(Gfx *gfx, int time, int n, int p) {
    (void)n; (void)p; selected=(unsigned)time; ++calls; return gfx;
}
'''
DRIVER = r'''
static fzx_hud_clock_t clock_state;
static unsigned checks;
static void reset(void) {
    memset(D_i3_80141EA8,0,sizeof(D_i3_80141EA8));
    memset(sPreviousLapTimes,0,sizeof(sPreviousLapTimes));
    gPlayerLapNumbers[0]=1;
    fzx_hud_clock_reset(&clock_state);
}
static void step(int lap, bool paused) {
    fzx_hud_clock_input_t input={lap,123456,{11111,22222,33333},paused};
    gRacers[0].lap=(int)lap; gRacers[0].raceTime=(int)input.race_time_ms;
    for(unsigned i=0;i<3;++i) gRacers[0].lapTimes[i]=(int)input.lap_times_ms[i];
    gGamePaused=(s8)paused; calls=0; selected=0;
    Gfx gfx=0;
    Hud_UpdatePlayerHudInfo(&gfx,0,0);
    assert(fzx_hud_clock_step(&clock_state,&input));
    if(clock_state.lap!=(unsigned)gPlayerLapNumbers[0] ||
       clock_state.flash_ticks!=(unsigned)D_i3_80141EA8[0].lapFlashTimer ||
       clock_state.visible!=(calls==1) || (calls && clock_state.time_ms!=selected)) {
        fprintf(stderr,"clock divergence at %u lap=%d flash=%u/%d visible=%u/%u time=%u/%u\n",
            checks,lap,clock_state.flash_ticks,D_i3_80141EA8[0].lapFlashTimer,
            clock_state.visible,calls,clock_state.time_ms,selected); exit(1);
    }
    ++checks;
}
int main(void) {
    reset(); step(0,false); step(0,true); step(1,false);
    for(int lap=2;lap<=4;++lap) {
        step(lap,true); /* paused at 180 still captures the right lap slot */
        for(unsigned i=0;i<185;++i) {
            step(lap,false);
            step(lap,true); /* pause at every counter, including 5,1,0 */
        }
    }
    for(unsigned n=0;n<=180;++n) {
        reset(); step(2,false);
        for(unsigned i=0;i<n;++i) step(2,false);
        reset(); step(0,false); step(1,false); /* retry at every flash phase */
    }
    reset();
    const int reversed[]={0,0,1,0,-1,-2,0,1,2,1,0,1,2,3,2,3,4};
    for(unsigned i=0;i<sizeof(reversed)/sizeof(reversed[0]);++i) step(reversed[i],false);
    fzx_hud_clock_t before=clock_state;
    fzx_hud_clock_input_t bad={5,0,{0,0,0},false};
    assert(!fzx_hud_clock_step(&clock_state,&bad));
    assert(memcmp(&before,&clock_state,sizeof(before))==0);
    puts("source clock sequences and retry/pause bounds PASS");
    return 0;
}
'''


@unittest.skipUnless(SOURCE.exists() and shutil.which("cc"), "Needs pinned source and compiler")
class HudClockTests(unittest.TestCase):
    def test_source_sequences_and_controls(self):
        match = re.search(r"Gfx\* Hud_UpdatePlayerHudInfo\(.*?\n\}", SOURCE.read_text(), re.S)
        self.assertIsNotNone(match)
        source = PRELUDE + match[0] + DRIVER
        native = (ROOT / "src/fzx_hud.c").read_text()
        variants = [native,
                    native.replace("%20u >= 5u", "%20u >= 4u"),
                    native.replace("(input->lap+1)%3", "input->lap%3"),
                    native.replace("--out.flash_ticks", "out.flash_ticks -= out.flash_ticks > 1u ? 2u : 1u")]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "oracle.c").write_text(source)
            for index, variant in enumerate(variants):
                if index:
                    self.assertNotEqual(native, variant)
                (path / "native.c").write_text(variant)
                command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                           "-I", str(ROOT / "include"), str(path / "oracle.c"),
                           str(path / "native.c"), "-o", str(path / "check")]
                built = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(path / "check")], capture_output=True, text=True)
                if index:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("clock divergence", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
