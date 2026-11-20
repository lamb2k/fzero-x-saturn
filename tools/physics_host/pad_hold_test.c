/* E1 two-tick pad hold (plays-right item 2): the engine reads the Saturn pad once per presented
 * 30 Hz frame (fzx_phys_pad) and holds that input for both 60 Hz ticks, with pressed and released
 * cleared on the second tick (saturn/engine/engine_drive.c, fzx_drive_step). HOST only; MIT.
 *
 *   pad_hold_test PACK [--control]
 *
 * Cases (each a pass/fail line; exit 1 on any failure):
 *  1. L for one frame: one press edge, no side attack (a repeated edge would read as a double tap);
 *  2. L, two frames released, L again: a double tap within 15 ticks gives the side attack;
 *  3. B for one frame (boost unlocked): one boost, one boost cost;
 *  4. right held: the stick ramps 16, 32, 48, 63 a frame and is the same on both ticks;
 *  5. L released: one release edge, on the frame's first tick.
 * --control repeats the frame's pressed and released on the second tick: cases 1 and 5 must fail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_native.h"

static FzxPhysCourse course;
static sh2z_vd_body_t b;
static FzxPadState pad;
static int control;
static unsigned edges_l, release_l;

static void frame(uint16_t held, int8_t sticks[2]) {
    sh2z_vd_input_t in;
    int k;
    fzx_phys_pad(&pad, held, &in);
    for (k = 0; k < 2; k++) {
        if (in.pressed & SH2Z_VD_BTN_LEAN_L) edges_l++;
        if (in.released & SH2Z_VD_BTN_LEAN_L) release_l += (unsigned) (k + 1);
        if (sticks) sticks[k] = in.stick_x;
        sh2z_vd_step(&b, &course.track, &course.vehicle, &fzx_phys_rules, &in);
        if (!control) { in.pressed = 0; in.released = 0; }
    }
}

static void reset(void) {
    b = course.start;
    memset(&pad, 0, sizeof(pad));
    while (b.intro != 0) frame(0, NULL);
    for (int i = 0; i < 30; i++) frame(FZX_PAD_A, NULL); /* rolling, controllable */
    b.flags |= SH2Z_VD_CAN_BOOST;
    b.energy = course.vehicle.max_energy;
    edges_l = release_l = 0;
}

int main(int argc, char** argv) {
    static uint8_t image[1 << 20];
    FILE* f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    size_t n;
    int fails = 0, ok;
    if (!f) { fprintf(stderr, "usage: pad_hold_test PACK [--control]\n"); return 2; }
    n = fread(image, 1, sizeof(image), f);
    fclose(f);
    control = argc > 2 && strcmp(argv[2], "--control") == 0;
    if (fzx_phys_load(&course, image, n, 1) != 0) { fprintf(stderr, "bad pack\n"); return 2; }

    reset();
    frame(FZX_PAD_A | FZX_PAD_L, NULL);
    for (int i = 0; i < 10; i++) frame(FZX_PAD_A, NULL);
    ok = edges_l == 1 && b.attack == SH2Z_VD_ATTACK_NONE;
    printf("1 one-frame L: %u press edge(s), attack %d: %s\n", edges_l, (int) b.attack, ok ? "PASS" : "FAIL");
    fails += !ok;

    reset();
    frame(FZX_PAD_A | FZX_PAD_L, NULL);
    frame(FZX_PAD_A, NULL);
    frame(FZX_PAD_A, NULL);
    frame(FZX_PAD_A | FZX_PAD_L, NULL);
    ok = b.attack == SH2Z_VD_ATTACK_SIDE;
    printf("2 double tap 6 ticks apart: attack %d: %s\n", (int) b.attack, ok ? "PASS" : "FAIL");
    fails += !ok;

    reset();
    {
        const int32_t e0 = b.energy;
        frame(FZX_PAD_A | FZX_PAD_B, NULL);
        const int32_t after = b.boost_timer;
        for (int i = 0; i < 70; i++) frame(FZX_PAD_A, NULL);
        ok = after > 0 && e0 - b.energy >= course.vehicle.boost_cost && b.boost_timer < after;
        printf("3 one-frame B: boost timer %d after the frame, energy %+.3f: %s\n", (int) after,
               (b.energy - e0) / 65536.0, ok ? "PASS" : "FAIL");
        fails += !ok;
    }

    reset();
    {
        int8_t s[4][2];
        for (int i = 0; i < 4; i++) frame(FZX_PAD_A | FZX_PAD_RIGHT, s[i]);
        ok = 1;
        for (int i = 0; i < 4; i++) ok &= s[i][0] == s[i][1] && s[i][0] == (i < 3 ? 16 * (i + 1) : 63);
        printf("4 right held: stick %d/%d %d/%d %d/%d %d/%d: %s\n", s[0][0], s[0][1], s[1][0], s[1][1], s[2][0], s[2][1],
               s[3][0], s[3][1], ok ? "PASS" : "FAIL");
        fails += !ok;
    }

    reset();
    frame(FZX_PAD_A | FZX_PAD_L, NULL);
    frame(FZX_PAD_A | FZX_PAD_L, NULL);
    release_l = 0;
    frame(FZX_PAD_A, NULL);
    ok = release_l == 1;
    printf("5 L released: release edge on tick(s) sum %u (1 = first tick only): %s\n", release_l, ok ? "PASS" : "FAIL");
    fails += !ok;

    printf("pad hold%s: %d failure(s)\n", control ? " (control: edges repeated on the second tick)" : "", fails);
    return fails ? 1 : 0;
}
