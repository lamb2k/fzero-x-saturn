"""HOST test of the FZX_SIMPIPE_PROFILE executed-tick accounting (C-038).

Compiles the real orig_tick, logged_tick and simpipe_dispatch_tick from saturn/main.c (sources off)
against stub pad/bridge globals, and checks: the colliding word 0xffff8080 via replay and via forced
input, consecutive identical words, replay exhaustion, and live input after the 12,288-entry log
saturates. A dispatch must count a tick exactly when ref_tick ran, hash the observed pad, and never
touch the pad on a freeze."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which('gcc') or shutil.which('cc')


def function(source, name):
    m = re.search(r'^(?:static\s+)?(?:void|uint32_t)(?:\s+__attribute__\(\(noinline\)\))?\s*\n' + name + r'\(', source, re.M)
    assert m, name
    end = source.index('\n}\n', m.start()) + 3
    return source[m.start():end]


HARNESS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define FZX_SOURCE_RESULTS 0
#define FZX_SOURCE_ROAD 0
#define FZX_SOURCE_HUD 0
#define FZX_SOURCE_CAMERA 0
#define FZX_SIMPIPE_PROFILE 1
#define PERIPHERAL_DIGITAL_A 0x0001u
#define PERIPHERAL_DIGITAL_C 0x0002u
#define PERIPHERAL_DIGITAL_START 0x0004u
#define PERIPHERAL_DIGITAL_B 0x0008u
#define PERIPHERAL_DIGITAL_L 0x0010u
#define PERIPHERAL_DIGITAL_R 0x0020u
#define PERIPHERAL_DIGITAL_Y 0x0040u
#define PERIPHERAL_DIGITAL_X 0x0080u
#define PERIPHERAL_DIGITAL_Z 0x0100u
#define PERIPHERAL_DIGITAL_LEFT 0x0200u
#define PERIPHERAL_DIGITAL_RIGHT 0x0400u
#define PERIPHERAL_DIGITAL_UP 0x0800u
#define PERIPHERAL_DIGITAL_DOWN 0x1000u
#define INPUT_LOG_TICKS 12288u
static volatile uint32_t fzx_input_log[INPUT_LOG_TICKS];
static volatile uint32_t fzx_input_count, fzx_debug_replay_count, fzx_debug_autopilot, fzx_debug_n64;
static uint16_t pad_down;
static int32_t fzx_steer_hint(uint32_t l) { (void)l; return 0; }
unsigned short ref_pad_button; signed char ref_pad_stick_x, ref_pad_stick_y;
static unsigned ref_tick_calls;
static void ref_tick(unsigned short b, signed char x, signed char y) { ++ref_tick_calls; ref_pad_button = b; ref_pad_stick_x = x; ref_pad_stick_y = y; }
static volatile uint32_t fzx_simpipe_ticks[4];
static uint32_t hashed[64]; static unsigned hashes;
static void simpipe_hash_tick(uint32_t word) { if (hashes < 64) hashed[hashes] = word; ++hashes; }
'''

MAIN = r'''
static void reset(void) {
    fzx_input_count = 0; fzx_debug_replay_count = 0; fzx_debug_autopilot = 0; fzx_debug_n64 = 0; pad_down = 0;
    ref_tick_calls = 0; hashes = 0; for (int i = 0; i < 4; ++i) fzx_simpipe_ticks[i] = 0;
    ref_pad_button = 0x1234; ref_pad_stick_x = 5; ref_pad_stick_y = -5;
}
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s line %d: %s\n", name, __LINE__, #c); return 1; } } while (0)
static int replay_collision(void) { const char *name = "replay_collision"; reset();
    fzx_input_log[0] = 0xffff8080u; fzx_debug_replay_count = 1;
    EXPECT(simpipe_dispatch_tick() == 1); EXPECT(ref_tick_calls == 1); EXPECT(hashes == 1 && hashed[0] == 0xffff8080u);
    EXPECT(fzx_simpipe_ticks[3] == 0); EXPECT(ref_pad_button == 0xffff && ref_pad_stick_x == -128 && ref_pad_stick_y == -128); return 0; }
static int forced_collision(void) { const char *name = "forced_collision"; reset();
    fzx_debug_n64 = 0xffff8080u;
    EXPECT(simpipe_dispatch_tick() == 1); EXPECT(ref_tick_calls == 1); EXPECT(hashes == 1 && hashed[0] == 0xffff8080u);
    EXPECT(fzx_simpipe_ticks[3] == 0); EXPECT(ref_pad_button == 0xffff && ref_pad_stick_x == -128); return 0; }
static int identical_words(void) { const char *name = "identical_words"; reset();
    for (int i = 0; i < 3; ++i) fzx_input_log[i] = 0x80003f00u; fzx_debug_replay_count = 3;
    unsigned ran = 0; for (int i = 0; i < 3; ++i) ran += simpipe_dispatch_tick();
    EXPECT(ran == 3 && ref_tick_calls == 3 && hashes == 3);
    EXPECT(hashed[0] == 0x80003f00u && hashed[1] == 0x80003f00u && hashed[2] == 0x80003f00u); EXPECT(fzx_input_count == 3); return 0; }
static int replay_exhausted(void) { const char *name = "replay_exhausted"; reset();
    fzx_input_log[0] = 0x00010203u; fzx_input_log[1] = 0x00040506u; fzx_debug_replay_count = 2;
    EXPECT(simpipe_dispatch_tick() == 1 && simpipe_dispatch_tick() == 1);
    const unsigned short b = ref_pad_button; const signed char x = ref_pad_stick_x, y = ref_pad_stick_y;
    EXPECT(simpipe_dispatch_tick() == 0); EXPECT(simpipe_dispatch_tick() == 0);
    EXPECT(ref_tick_calls == 2 && hashes == 2 && fzx_simpipe_ticks[3] == 2);
    EXPECT(ref_pad_button == b && ref_pad_stick_x == x && ref_pad_stick_y == y); return 0; }
static int live_after_saturation(void) { const char *name = "live_after_saturation"; reset();
    fzx_input_count = INPUT_LOG_TICKS; pad_down = PERIPHERAL_DIGITAL_A | PERIPHERAL_DIGITAL_RIGHT;
    EXPECT(simpipe_dispatch_tick() == 1); EXPECT(simpipe_dispatch_tick() == 1);
    EXPECT(ref_tick_calls == 2 && hashes == 2 && fzx_simpipe_ticks[3] == 0);
    EXPECT(hashed[0] == (0x8000u << 16 | (uint32_t)(uint8_t)63 << 8)); EXPECT(fzx_input_count == INPUT_LOG_TICKS); return 0; }
static int replay_past_log(void) { const char *name = "replay_past_log"; reset();
    fzx_input_count = INPUT_LOG_TICKS; fzx_debug_replay_count = INPUT_LOG_TICKS + 5;
    EXPECT(simpipe_dispatch_tick() == 0); EXPECT(ref_tick_calls == 0 && hashes == 0 && fzx_simpipe_ticks[3] == 1); return 0; }
int main(void) {
    int f = replay_collision() + forced_collision() + identical_words() + replay_exhausted() + live_after_saturation() + replay_past_log();
    if (!f) printf("PASS 6\n"); return f;
}
'''


@unittest.skipIf(CC is None, 'needs a host C compiler')
class DispatchTests(unittest.TestCase):
    def test_executed_tick_accounting(self):
        src = (ROOT / 'saturn' / 'main.c').read_text()
        body = '\n'.join(function(src, n) for n in ('logged_tick', 'orig_tick', 'simpipe_dispatch_tick'))
        self.assertNotIn('0xffffu; ref_pad_stick_x = -128', src, 'sentinel park must be gone')
        with tempfile.TemporaryDirectory() as t:
            c = Path(t) / 'd.c'; exe = Path(t) / 'd'
            c.write_text(HARNESS + body + MAIN)
            r = subprocess.run([CC, '-std=c11', '-Wall', '-Wno-unused-function', '-o', str(exe), str(c)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('PASS 6', r.stdout)


if __name__ == '__main__':
    unittest.main()
