/* Duel reference driver (attacks lane, claude/attacks): the original race code with two pad-driven
 * racers (ref/duel_probe.c). HOST reference role only. MIT; see ../LICENSE.
 *
 *   fzx-duel --course RECORD --rom BOOT --scenario NAME [--machines c0,e0,c1,e1] [--ticks N]
 *            [--trace OUT.tsv] [--pads OUT.txt] [--bake-at T --bake0 SRC0 --bake1 SRC1]
 *
 * Scenarios (racer 0 the player P, racer 1 the opponent O; both closed-loop steered to a target
 * lateral, as the R01 driver with gains 0.02 and 2.5, A held from tick 460):
 *   bump    P steers onto O's line: plain contacts (shared impact, push while closing)
 *   side    P comes alongside O and double-taps toward it (side attacks), every 90 ticks
 *   spin    P holds the far lean and double-taps the near one beside O (spin attacks)
 *   cpu     O comes alongside P and side-attacks it (an opponent's attack: no x10)
 *   ko      side attacks until O spins out (KO credit), then both drive on to its retirement
 * --attack-from T: the first tick an attack may start (default 700). --length-out FILE: the lap length.
 * --pads writes every tick's integer pads for both racers ("t b0 x0 b1 x1"): the native windows
 * replay them. --trace: one line per tick and racer (see duel_state). --bake-at T writes both
 * racers' physics pack sources after tick T (the native window's start state). The course record
 * and the ROM are the owner's: everything written is local only.
 *
 * C-092 (the contact judge): --pairlog FILE writes, per tick, the exact pre-tick inputs of the pair
 * test (P: positions, velocities, attack states, flags, along gap, colliding bit; floats as %a) and
 * the original's own decisions from the hooked racer.c (T touch, C closing comparison, H push,
 * D damage calls with their source, K KO credit with victim and attacker). --force T:V replays the
 * same run with the closing comparison of tick T forced to V (0 or 1; a branch replay). --perturb
 * T:K:F:A:D adds D to racer K's position (F 0) or velocity (F 1), axis A, just before tick T
 * (a reference-only perturbation; repeatable up to 8 times). --replay-pads FILE drives both racers
 * from a recorded --pads file instead of the scripted drivers (as the native windows are driven).
 * --bake-range A:B --bake-dir DIR writes both racers' sources after every tick A..B
 * (DIR/p<k>-<tick>.src): the matched-state one-tick scan (tools/attacks_onestep.py).
 * --set-age T:K:A sets racer K's age since the last human contact (unk_288) to A just before tick T, and
 * --set-toucher T:K:W its toucher (W -1 none): reference-only settings for the falls card's boundary tests. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"

void ref_handling_race_features(void);
int duel_setup(int character0, float engine0, int character1, float engine1);
void duel_tick(unsigned short b0, signed char x0, signed char y0, unsigned short b1, signed char x1, signed char y1);
void duel_state(int k, float f[12], int n[8]);
typedef void (*DuelEmit)(const char* key, int a, int b, const float* v, int n);
void duel_bake(int k, DuelEmit emit);
float rb_course_length(void);
void duel_log_tick(int tick);
void duel_force(int tick, int value);
int duel_log_count(void);
void duel_log_get(int i, int* kind, int* tick, int* a, int* b, float f[10]);
void duel_pair(float* trail, int* colliding);
void duel_perturb(int k, int field, int axis, float delta);
void duel_set_age(int k, int age);
void duel_set_toucher(int k, int who);
int duel_load(int k, const char* key, const float* v, int n);
void duel_set_energy(int k, float e);
void duel_radius(int k, float* left, float* right);

static void die(const char* m) { fprintf(stderr, "fzx-duel: %s\n", m); exit(2); }
static unsigned char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) die("cannot open input");
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = malloc(*n ? *n : 1);
    if (!b || fread(b, 1, *n, f) != *n) die("short read");
    fclose(f);
    return b;
}

static FILE* bake_out;
static void bake_emit(const char* key, int a, int b, const float* v, int n) {
    fprintf(bake_out, "%s %d %d", key, a, b);
    for (int k = 0; k < n; k++) fprintf(bake_out, " %a", (double)v[k]);
    fputc('\n', bake_out);
}
static void bake(int k, const char* path, int tick) {
    if ((bake_out = fopen(path, "w")) == NULL) die("cannot write a bake source");
    fprintf(bake_out, "# fzx-physics-source v1 after tick %d\n", tick);
    duel_bake(k, bake_emit);
    fclose(bake_out);
}

/* --load-state T:SRC0:SRC1 (attacks-3): just before tick T, both racers' baked state fields are replaced by the values of the two
 * state sources (the bake format; the matched-start diagnostic gives it the native entry state converted back to floats). */
static void load_state(int k, const char* path) {
    char line[4096];
    FILE* f = fopen(path, "r");
    if (!f) die("cannot open a load-state source");
    while (fgets(line, sizeof line, f)) {
        char key[64], *p = line;
        float v[32];
        int n = 0, a, b, used;
        if (line[0] == '#' || sscanf(p, "%63s %d %d%n", key, &a, &b, &used) != 3) continue;
        p += used;
        for (;;) {
            char* e;
            double d = strtod(p, &e);
            if (e == p || n >= 32) break;
            v[n++] = (float)d;
            p = e;
        }
        duel_load(k, key, v, n);
    }
    fclose(f);
}

#define N64_A 0x8000u
#define N64_Z 0x2000u
#define N64_R 0x0010u
#define ST_SPIN (1 << 14)
#define ST_RETIRED (1 << 18)

static signed char steer(const float f[12], float target, float kl) {
    float s = kl * (f[8] - target) + 2.5f * f[9];
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return (signed char)(s >= 0.0f ? (int)(s * 63.0f + 0.5f) : -(int)(-s * 63.0f + 0.5f));
}

int main(int argc, char** argv) {
    const char *course = 0, *rom = 0, *scenario = "bump", *trace = 0, *pads = 0, *bake0 = 0, *bake1 = 0;
    int ticks = 2400, bake_at = -1, c0 = 0, c1 = 9, attack_from = 700;
    const char *length_out = 0, *pairlog = 0, *replay = 0, *bake_dir = 0;
    int bake_from = -1, bake_to = -1;
    int force_tick = -1, force_value = -1, npert = 0, pert_t[8], pert_k[8], pert_f[8], pert_a[8];
    int age_t = -1, age_k = 0, age_v = 0, tch_t = -1, tch_k = 0, tch_v = -1, load_t = -1;
    const char *load0 = 0, *load1 = 0;
    float pert_d[8];
    float e0 = 0.0f, e1 = 0.0f;
    /* outcome judge (attacks-4) episode parameters; the defaults are the scripted drivers' constants, so no flag changes a run */
    float lane_frac = 0.0f, near = 38.0f, bump_off = 0.0f, nrg_v[2] = { 0, 0 };
    int att_side = 0, tap_int = 90, launch[2] = { 0, 0 }, nrg_t[2] = { -1, -1 }, nrg_k[2] = { 0, 1 }, nrg_n = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--scenario")) scenario = argv[i + 1];
        else if (!strcmp(argv[i], "--ticks")) ticks = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--pads")) pads = argv[i + 1];
        else if (!strcmp(argv[i], "--bake-at")) bake_at = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--attack-from")) attack_from = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--length-out")) length_out = argv[i + 1];
        else if (!strcmp(argv[i], "--pairlog")) pairlog = argv[i + 1];
        else if (!strcmp(argv[i], "--replay-pads")) replay = argv[i + 1];
        else if (!strcmp(argv[i], "--force")) { if (sscanf(argv[i + 1], "%d:%d", &force_tick, &force_value) != 2) die("--force T:V"); }
        else if (!strcmp(argv[i], "--perturb")) {
            if (npert >= 8 || sscanf(argv[i + 1], "%d:%d:%d:%d:%f", &pert_t[npert], &pert_k[npert], &pert_f[npert], &pert_a[npert],
                                     &pert_d[npert]) != 5 || pert_k[npert] < 0 || pert_k[npert] > 1 || pert_a[npert] < 0 || pert_a[npert] > 2)
                die("--perturb T:K:F:A:D");
            npert++;
        }
        else if (!strcmp(argv[i], "--load-state")) {
            if (sscanf(argv[i + 1], "%d:", &load_t) != 1) die("--load-state T:SRC0:SRC1");
            char* c1 = strchr(argv[i + 1], ':'); char* c2 = c1 ? strchr(c1 + 1, ':') : 0;
            if (!c2) die("--load-state T:SRC0:SRC1");
            *c2 = 0; load0 = c1 + 1; load1 = c2 + 1;
        }
        else if (!strcmp(argv[i], "--set-age")) { if (sscanf(argv[i + 1], "%d:%d:%d", &age_t, &age_k, &age_v) != 3) die("--set-age T:K:A"); }
        else if (!strcmp(argv[i], "--set-toucher")) { if (sscanf(argv[i + 1], "%d:%d:%d", &tch_t, &tch_k, &tch_v) != 3) die("--set-toucher T:K:W"); }
        else if (!strcmp(argv[i], "--lane-frac")) lane_frac = (float)atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--near")) near = (float)atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--bump-off")) bump_off = (float)atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--att-side")) att_side = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--tap-int")) tap_int = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--launch0")) launch[0] = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--launch1")) launch[1] = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--set-energy")) { if (nrg_n >= 2 || sscanf(argv[i + 1], "%d:%d:%f", &nrg_t[nrg_n], &nrg_k[nrg_n], &nrg_v[nrg_n]) != 3) die("--set-energy T:K:E (at most 2)"); nrg_n++; }
        else if (!strcmp(argv[i], "--bake0")) bake0 = argv[i + 1];
        else if (!strcmp(argv[i], "--bake1")) bake1 = argv[i + 1];
        else if (!strcmp(argv[i], "--bake-range")) { if (sscanf(argv[i + 1], "%d:%d", &bake_from, &bake_to) != 2) die("--bake-range A:B"); }
        else if (!strcmp(argv[i], "--bake-dir")) bake_dir = argv[i + 1];
        else if (!strcmp(argv[i], "--machines")) { if (sscanf(argv[i + 1], "%d,%f,%d,%f", &c0, &e0, &c1, &e1) != 4) die("--machines c0,e0,c1,e1"); }
        else die("unknown argument");
    }
    if (!course || !rom) die("usage: see duel_main.c");
    size_t n;
    unsigned char* rec = read_file(course, &n);
    if (n != REF_COURSE_RECORD_SIZE) die("course record must be 0x7E0 bytes");
    unsigned char* r = read_file(rom, &n);
    if (n < REF_ROM_BOOT_SIZE) die("short ROM boot");
    ref_set_rom_boot(r);
    if (ref_load_course(rec, 1) != 0 || ref_race_init() != 0) die("race set-up failed");
    ref_handling_race_features();
    if (duel_setup(c0, e0, c1, e1) != 2) die("the duel did not set up two racers");
    const int sc = !strcmp(scenario, "bump") ? 0 : !strcmp(scenario, "side") ? 1 : !strcmp(scenario, "spin") ? 2 :
                   !strcmp(scenario, "cpu") ? 3 : !strcmp(scenario, "ko") ? 4 : !strcmp(scenario, "free") ? 5 : -1;
    if (sc < 0) die("unknown scenario");
    const float length = rb_course_length();
    if (length_out) { FILE* lf = fopen(length_out, "w"); if (!lf) die("cannot write the length"); fprintf(lf, "%.6f\n", length); fclose(lf); }
    FILE* tf = trace ? fopen(trace, "w") : NULL;
    FILE* pf = pads ? fopen(pads, "w") : NULL;
    FILE* lf = pairlog ? fopen(pairlog, "w") : NULL;
    if ((trace && !tf) || (pads && !pf) || (pairlog && !lf)) die("cannot write an output");
    duel_force(force_tick, force_value);
    static unsigned short rb[2][20000];
    static signed char rx[2][20000];
    int rlast = -1;
    if (replay) {
        FILE* rf = fopen(replay, "r");
        int rt; unsigned rb0, rb1; int rx0, rx1;
        if (!rf) die("cannot open the replay pads");
        while (fscanf(rf, "%d %u %d %u %d", &rt, &rb0, &rx0, &rb1, &rx1) == 5 && rt >= 0 && rt < 20000) {
            rb[0][rt] = (unsigned short)rb0; rx[0][rt] = (signed char)rx0; rb[1][rt] = (unsigned short)rb1; rx[1][rt] = (signed char)rx1;
            rlast = rt;
        }
        fclose(rf);
        if (rlast < ticks - 1) die("the replay pads are shorter than --ticks");
    }
    float f0[12], f1[12]; int n0[8], n1[8];
    duel_state(0, f0, n0); duel_state(1, f1, n1);
    int tap_at = -1000, tap_dir = 0, attacker = sc == 3 ? 1 : 0;
    for (int t = 0; t < ticks; ++t) {
        unsigned short b[2] = { 0, 0 };
        signed char x[2] = { 0, 0 };
        if (t >= 460 + launch[0]) b[0] = N64_A;
        if (t >= 460 + launch[1]) b[1] = N64_A;
        /* gap: O ahead of P along the lap (+), wrapped */
        float gap = f1[10] - f0[10];
        if (gap > length * 0.5f) gap -= length;
        if (gap < -length * 0.5f) gap += length;
        float lane_o = 60.0f;   /* O keeps 60 left of the line */
        if (lane_frac != 0.0f) {   /* a fraction of O's local half width, signed (+ left) */
            float rl, rr; duel_radius(1, &rl, &rr);
            lane_o = lane_frac * (lane_frac > 0.0f ? rl : rr);
        }
        float target0 = 0.0f, target1 = lane_o;
        const float* fa = attacker ? f1 : f0;   /* the attacker's and the victim's state */
        const float* fv = attacker ? f0 : f1;
        const float vgap = attacker ? -gap : gap;
        switch (sc) {
        case 0: target0 = f1[8] + (att_side ? att_side * bump_off : 0.0f); break;   /* onto O */
        case 1: case 2: case 4: target0 = f1[8] + (att_side ? att_side * near : (f0[8] > f1[8] ? near : -near)); break; /* beside O */
        case 3: target0 = 0.0f; target1 = f0[8] + (f1[8] > f0[8] ? 38.0f : -38.0f); break;  /* O beside P */
        }
        if (sc >= 1 && t >= attack_from) {
            /* Attack when beside the victim: two presses two ticks apart toward it, every 90 ticks. */
            const int k = attacker;
            if (t - tap_at >= tap_int && fabsf(vgap) < 25.0f && fabsf(fv[8] - fa[8]) < 75.0f && !(n0[0] & ST_SPIN) && !(n1[0] & ST_SPIN) &&
                !(n1[0] & ST_RETIRED)) {
                tap_at = t + (sc == 2 ? 2 : 0); tap_dir = fv[8] > fa[8] ? -1 : 1;   /* victim to the left (+lateral) -> Z, else R */
            }
            const int dt = t - tap_at;
            if (dt == 0 || dt == 2) b[k] |= tap_dir < 0 ? N64_Z : N64_R;
            if (sc == 2 && dt >= -2 && dt <= 4) b[k] |= tap_dir < 0 ? N64_R : N64_Z;   /* hold the far one: spin */
        }
        /* Both keep level: whichever is more than 15 ahead lifts off the throttle. */
        if (sc <= 4 && t >= 460 && gap > 15.0f) b[1] &= (unsigned short)~N64_A;
        if (sc <= 4 && t >= 460 && gap < -15.0f) b[0] &= (unsigned short)~N64_A;
        if (n0[0] & ST_SPIN) target0 = f0[8];
        x[0] = steer(f0, target0, 0.004f);
        x[1] = steer(f1, target1, 0.004f);
        if (replay) { b[0] = rb[0][t]; x[0] = rx[0][t]; b[1] = rb[1][t]; x[1] = rx[1][t]; }
        for (int q = 0; q < npert; ++q)
            if (pert_t[q] == t) duel_perturb(pert_k[q], pert_f[q], pert_a[q], pert_d[q]);
        if (load_t == t) { load_state(0, load0); load_state(1, load1); }
        if (age_t == t) duel_set_age(age_k, age_v);
        if (tch_t == t) duel_set_toucher(tch_k, tch_v);
        if (lf) {
            float trail; int colliding;
            duel_pair(&trail, &colliding);
            duel_state(0, f0, n0); duel_state(1, f1, n1);
            fprintf(lf, "P %d %a %d", t, (double)trail, colliding);
            for (int k = 0; k < 2; ++k) {
                const float* fs = k ? f1 : f0; const int* ns = k ? n1 : n0;
                for (int j = 0; j < 6; ++j) fprintf(lf, " %a", (double)fs[j]);
                fprintf(lf, " %d %d", ns[1], ns[0]);
            }
            fputc('\n', lf);
        }
        duel_log_tick(t);
        duel_tick(b[0], x[0], 0, b[1], x[1], 0);
        if (lf) {
            for (int q = 0; q < duel_log_count(); ++q) {
                int kind, tk, a, bb; float v[10];
                duel_log_get(q, &kind, &tk, &a, &bb, v);
                fprintf(lf, "%c %d %d %d", kind, tk, a, bb);
                for (int j = 0; j < 10; ++j) fprintf(lf, " %a", (double)v[j]);
                fputc('\n', lf);
            }
        }
        if (pf) fprintf(pf, "%d %u %d %u %d\n", t, b[0], x[0], b[1], x[1]);
        duel_state(0, f0, n0); duel_state(1, f1, n1);
        if (tf) {
            const float* fs[2] = { f0, f1 }; const int* ns[2] = { n0, n1 };
            for (int k = 0; k < 2; ++k)
                fprintf(tf, "%d\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%d\t%d\t%d\t%d\t%d\t%d\t%.6f\n", t, k,
                        fs[k][0], fs[k][1], fs[k][2], fs[k][3], fs[k][4], fs[k][5], fs[k][6], fs[k][7], fs[k][8], fs[k][9], fs[k][10],
                        ns[k][0], ns[k][1], ns[k][2], ns[k][3], ns[k][4], ns[k][5], fs[k][11]);
        }
        for (int q = 0; q < nrg_n; ++q)
            if (t == nrg_t[q]) { duel_set_energy(nrg_k[q], nrg_v[q]); duel_state(0, f0, n0); duel_state(1, f1, n1); }
        if (t == bake_at && bake0 && bake1) { bake(0, bake0, t); bake(1, bake1, t); }
        if (bake_dir && t >= bake_from && t <= bake_to) {
            char path[1024];
            for (int k = 0; k < 2; ++k) {
                snprintf(path, sizeof path, "%s/p%d-%05d.src", bake_dir, k, t);
                bake(k, path, t);
            }
        }
    }
    if (tf) fclose(tf);
    if (pf) fclose(pf);
    if (lf) fclose(lf);
    printf("duel %s: ticks %d kos %d O energy %.2f flags %08x P energy %.2f\n", scenario, ticks, n1[3], f1[7], (unsigned)n1[0], f0[7]);
    return 0;
}
