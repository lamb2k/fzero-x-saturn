"""HOST: the cup classification against the original functions, and the scripted cup manager.

SCENES.md section 11. The original func_8008AA8C, Racer_RetireRacer and Racer_UpdateRacePositions
are compiled from the pinned decompilation into a temporary file (nothing from it is committed or
printed) and run on the same inputs as saturn/engine/engine_cup.c; tests/cup_driver.c compares the
ordered rows, times and scoring places exactly, then scripts a six-round cup. Negative controls
change engine_cup.c and must make the comparison fail.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from import_source_art import DECOMP  # noqa: E402
from records_source import function  # noqa: E402

RACER = DECOMP / 'src/game/racer.c'
RACER_H = DECOMP / 'include/fzx_racer.h'
GAME_H = DECOMP / 'include/fzx_game.h'
CUP = ROOT / 'saturn/engine/engine_cup.c'


def reference_source():
    racer = RACER.read_text()
    defines = [l for l in RACER_H.read_text().splitlines()
               if re.match(r'#define RACER_STATE_(FINISHED|RETIRED|CRASHED|FALLING_OFF_TRACK|ACTIVE|CPU_CONTROLLED)\b', l)]
    if len(defines) != 6:
        raise AssertionError('racer state flags changed')
    game = GAME_H.read_text()
    modes = {}
    for name in ('GAMEMODE_GP_RACE', 'GAMEMODE_DEATH_RACE'):
        m = re.search(r'/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*' + name + r',', game)
        if not m:
            raise AssertionError('game mode changed: ' + name)
        modes[name] = int(m[1], 16)
    points = re.search(r'^s32 gRacePositionPoints\[\] = \{.*?\};', racer, re.M | re.S)
    if not points:
        raise AssertionError('points table changed')
    prelude = '\n'.join([
        '#include <stdint.h>', '#include <string.h>', '#include <stddef.h>', '#include <stdbool.h>',
        'typedef int32_t s32; typedef int16_t s16; typedef uint32_t u32; typedef float f32;',
        *defines,
        '#define GAMEMODE_GP_RACE %d' % modes['GAMEMODE_GP_RACE'],
        '#define GAMEMODE_DEATH_RACE %d' % modes['GAMEMODE_DEATH_RACE'],
        'typedef struct Racer { s32 id; s32 stateFlags; s16 points; f32 energy, maxEnergy, raceDistance,'
        ' raceDistancePosition; s32 lapTimes[3]; s32 raceTime; s32 position; } Racer;',
        'typedef struct { f32 length; } CourseInfo;',
        'static CourseInfo course_info; static CourseInfo* gCurrentCourseInfo = &course_info;',
        'static Racer gRacers[30]; static Racer* sLastRacer; static Racer* sRivalRacer; static Racer* gRacersByPosition[30];',
        'static s32 gTotalRacers, gTotalLapCount, sRaceFrameCount, gGameMode, gNumPlayers, gRacersRetired,'
        ' gPlayerRacersRetired, gCpuRacersRetired, D_800E5FBC, D_800F5DE8, D_800F80C4, D_800E5FC0, D_800E5FD4, D_800E5FD6;',
        'static s32 Math_Rand2(void) { return 0; }',
        'static void Audio_PlayerFinished(s32 id) { (void)id; }',
        'static void Audio_PlayerEngineStop(s32 id) { (void)id; }',
        'static void func_8007E0CC(void) {}',
        'static void Racer_UpdateRivalRacer(void) {}',
        'static void Racer_UpdateRacePositions(void);', 'static void Racer_RetireRacer(Racer* racer);',
        'static ' + points[0],
    ])
    body = '\n'.join(function(racer, n) for n in ('Racer_RetireRacer', 'Racer_UpdateRacePositions', 'func_8008AA8C'))
    wrapper = r'''
typedef struct { int state, position, retire_frame, retire_position; s32 race_ms, lap_ms[3]; float dist; } RefIn;
int ref_run(const RefIn* in, int n, int laps, float length, int frame, int* entrant, s32* time, int* scoring, int* fin) {
    memset(gRacers, 0, sizeof(gRacers)); memset(gRacersByPosition, 0, sizeof(gRacersByPosition));
    gTotalRacers = n; sLastRacer = &gRacers[n - 1]; gTotalLapCount = laps; course_info.length = length;
    gGameMode = GAMEMODE_GP_RACE; gNumPlayers = 1; sRivalRacer = NULL;
    for (int i = 0; i < n; i++) {
        Racer* r = &gRacers[i];
        r->id = i; r->raceTime = in[i].race_ms; memcpy(r->lapTimes, in[i].lap_ms, sizeof(r->lapTimes));
        r->raceDistance = in[i].dist; r->position = in[i].position; r->stateFlags = RACER_STATE_ACTIVE;
        if (in[i].state == 1) r->stateFlags |= RACER_STATE_FINISHED;
        if (in[i].state == 2) r->stateFlags |= RACER_STATE_FALLING_OFF_TRACK;
        if (in[i].state == 3) {
            sRaceFrameCount = in[i].retire_frame; r->position = in[i].retire_position;
            Racer_RetireRacer(r);
            r->stateFlags &= ~RACER_STATE_ACTIVE; r->stateFlags |= RACER_STATE_CRASHED; r->position = in[i].position;
        }
    }
    sRaceFrameCount = frame;
    func_8008AA8C();
    for (int p = 0; p < n; p++) {
        Racer* r = gRacersByPosition[p];
        if (!r) return 0;
        entrant[p] = (int)(r - gRacers); time[p] = r->raceTime; scoring[p] = r->points > 0;
        fin[p] = (r->stateFlags & RACER_STATE_FINISHED) != 0;
    }
    return 1;
}
s32 ref_points(int place) { return gRacePositionPoints[place]; }
'''
    return prelude + '\n' + body + '\n' + wrapper


CONTROLS = [
    # equal times: the higher entrant first (the scan from the last racer)
    ('if (fin < 0 || time[r] < best)', 'if (fin < 0 || time[r] <= best)'),
    # the lap correction d
    ('if (e->lap_ms[1] != 0) { d = e->lap_ms[0] - e->lap_ms[1]; if (d < 0) d = 0; }', 'd = 0;'),
    # the retirement order value
    ('/ 10.0f)) - 600000.0f;', '/ 9.0f)) - 600000.0f;'),
    # a falling machine retires at the trigger frame
    ('rdp[r] = retire_position_value(key->frame, n, e->position);', 'rdp[r] = retire_position_value(0, n, e->position);'),
    # scoring through the last timed row
    ('for (int p = 0; p <= last_timed; ++p) out->row[p].scoring = 1u;', 'for (int p = 0; p < last_timed; ++p) out->row[p].scoring = 1u;'),
    # the pause-retry lock before any change
    ('if (cup.live.menu_lives <= 0) return false;\n        --cup.live.balance;', '--cup.live.balance;'),
    # END keyed to the accepted attempt
    ('cup.state != FZX_CUP_OVER) || run_id != cup.accepted_run_id) return false;', 'cup.state != FZX_CUP_OVER)) return false;'),
    # the 5th KO raises the menu's lives too
    ('++cup.live.balance; ++cup.live.menu_lives;', '++cup.live.balance;'),
]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc'), 'Linux GCC required')
@unittest.skipUnless(RACER.is_file(), 'pinned source required')
class Cup(unittest.TestCase):
    def build_run(self, tmp, cup_source):
        (tmp / 'reference.c').write_text(reference_source())
        (tmp / 'engine_cup.c').write_text(cup_source)
        command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-fno-fast-math',
                   '-ffp-contract=off', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                   '-I' + str(ROOT / 'saturn/engine'), str(tmp / 'reference.c'), str(tmp / 'engine_cup.c'),
                   str(ROOT / 'tests/cup_driver.c'), '-o', str(tmp / 'cup')]
        got = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        return subprocess.run([str(tmp / 'cup')], capture_output=True, text=True)

    def test_reference_cases_and_scripted_cup(self):
        with tempfile.TemporaryDirectory(prefix='fzx-cup-') as td:
            got = self.build_run(Path(td), CUP.read_text())
            self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
            self.assertIn('cup: ok', got.stdout)

    def test_negative_controls(self):
        source = CUP.read_text()
        for old, new in CONTROLS:
            with self.subTest(control=old), tempfile.TemporaryDirectory(prefix='fzx-cup-') as td:
                self.assertEqual(source.count(old), 1, old)
                got = self.build_run(Path(td), source.replace(old, new))
                self.assertNotEqual(got.returncode, 0, 'control passed: ' + old)


if __name__ == '__main__':
    unittest.main()
