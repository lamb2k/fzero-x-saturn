/* Matched-input pair probe (attacks lane, C-092): the native contact rule (SH2ZAM body-contact-1,
 * sh2z_bc_pair) on the reference's own pre-tick inputs, tick by tick, against the reference's own
 * decisions. HOST only. MIT; see ../../LICENSE.
 *
 *   fzx-pair-probe --pairlog REF.pairlog --machines c0,c1 [--from T0 --to T1]
 *
 * REF.pairlog is a duel reference run with --pairlog (ref/duel_main.c). For every tick the reference
 * tested the pair (along gap at most 92), both racers' positions and velocities are rounded to Q16
 * (the native representation), the pair gets the reference's colliding bit, attack states and crashed
 * flags, and the rules of tools/attacks_host/duel_native.c (racer 0 the human: HEAVY and CREDIT). One
 * line per tested tick:
 *   M t ref_touch nat_touch ref_first nat_first ref_push nat_push ref_closing nat_closing_q32
 *     push_err0 push_err1 dmg_ref0 dmg_nat0 dmg_ref1 dmg_nat1
 * and a summary line: tests, touch/first/push decision mismatches, worst push and damage errors on
 * ticks where both decisions agree. The reference's racer is id 1 and racer2 id 0; its closing
 * product (p0 - p1).(v0 - v1) equals the native's (p1 - p0).(v1 - v0). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sh2zam/body_contact_experimental.h"
#include "attacks_tables.h"

#define ST_CRASHED (1 << 27)
#define ST_ACTIVE (1 << 30)

static void die(const char *m) { fprintf(stderr, "fzx-pair-probe: %s\n", m); exit(2); }
static int32_t q16(double x) { return (int32_t)llround(x * 65536.0); }
#define Q16C(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))

typedef struct {
    int have_p, colliding, att[2], flags[2], have_t, was, have_c, natural, have_h;
    double trail, pos[2][3], vel[2][3], closing, k1, k2, p[3], dmg[2];
} tick_t;

static tick_t *ticks;
static int nticks;

static tick_t *at(int t) {
    if (t < 0) die("bad tick");
    if (t >= nticks) {
        int n = t + 1024;
        ticks = realloc(ticks, (size_t)n * sizeof(*ticks));
        if (!ticks) die("out of memory");
        memset(ticks + nticks, 0, (size_t)(n - nticks) * sizeof(*ticks));
        nticks = n;
    }
    return &ticks[t];
}

int main(int argc, char **argv) {
    const char *path = 0;
    int c0 = 0, c1 = 0, from = 0, to = 1 << 30;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--pairlog")) path = argv[i + 1];
        else if (!strcmp(argv[i], "--from")) from = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--to")) to = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--machines")) { if (sscanf(argv[i + 1], "%d,%d", &c0, &c1) != 2) die("--machines c0,c1"); }
        else die("unknown argument");
    }
    if (!path) die("usage: see pair_probe.c");
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open the pair log");
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char kind;
        int t, a, b, n;
        if (line[0] == 'P') {
            double v[13];
            int i0, i1, i2, i3;
            if (sscanf(line, "P %d %la %d %la %la %la %la %la %la %d %d %la %la %la %la %la %la %d %d", &t, &v[0], &n, &v[1], &v[2], &v[3],
                       &v[4], &v[5], &v[6], &i0, &i1, &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &i2, &i3) != 19)
                die("bad P line");
            tick_t *k = at(t);
            k->have_p = 1; k->trail = v[0]; k->colliding = n;
            for (int j = 0; j < 3; ++j) {
                k->pos[0][j] = v[1 + j]; k->vel[0][j] = v[4 + j]; k->pos[1][j] = v[7 + j]; k->vel[1][j] = v[10 + j];
            }
            k->att[0] = i0; k->flags[0] = i1; k->att[1] = i2; k->flags[1] = i3;
            continue;
        }
        double v[10];
        if (sscanf(line, "%c %d %d %d %la %la %la %la %la %la %la %la %la %la", &kind, &t, &a, &b, &v[0], &v[1], &v[2], &v[3], &v[4],
                   &v[5], &v[6], &v[7], &v[8], &v[9]) != 14)
            die("bad record line");
        tick_t *k = at(t);
        if (kind == 'T') { k->have_t = 1; k->was = (int)v[0]; }
        else if (kind == 'C') { k->have_c = 1; k->closing = v[0]; k->natural = (int)v[1]; }
        else if (kind == 'H') { k->have_h = 1; k->k1 = v[0]; k->k2 = v[1]; k->p[0] = v[2]; k->p[1] = v[3]; k->p[2] = v[4]; }
        else if (kind == 'D' && b == 1 && (a == 0 || a == 1)) k->dmg[a] += v[0];
    }
    fclose(f);

    sh2z_bc_rules_t br;
    memset(&br, 0, sizeof(br));
    br.reach_long = 92 * 256; br.reach = Q16C(46.0); br.near = Q16C(0.01); br.impact_gain = Q16C(1.1);
    br.attack_push = Q16C(3.0); br.attack_min = Q16C(3.0); br.weak_share = 21823; br.heavy_gain = Q16C(10.0);
    br.credit_window = 60;
    long tests = 0, mis_touch = 0, mis_first = 0, mis_push = 0, agree = 0;
    double worst_push = 0, worst_dmg = 0, worst_dmg_rel = 0;
    for (int t = from; t < nticks && t <= to; ++t) {
        const tick_t *k = &ticks[t];
        if (!k->have_p) continue;
        if (!((k->flags[0] & ST_ACTIVE) && (k->flags[1] & ST_ACTIVE)) || k->trail > 92.0f) continue;
        sh2z_bc_body_t bc[2];
        memset(bc, 0, sizeof(bc));
        for (int r = 0; r < 2; ++r) {
            for (int j = 0; j < 3; ++j) { bc[r].pos[j] = q16(k->pos[r][j]); bc[r].vel[j] = q16(k->vel[r][j]); }
            bc[r].flags = SH2Z_BC_ACTIVE | (k->att[r] ? SH2Z_BC_ATTACK : 0u) | ((k->flags[r] & ST_CRASHED) ? SH2Z_BC_WEAK : 0u);
            bc[r].toucher = -1; bc[r].since = UINT32_MAX;
        }
        bc[0].flags |= SH2Z_BC_HEAVY | SH2Z_BC_CREDIT;
        bc[0].strength = fzx_atk_machine[c0][0]; bc[1].strength = fzx_atk_machine[c1][0];
        int64_t closing = 0;
        for (int j = 0; j < 3; ++j) closing += ((int64_t)bc[1].pos[j] - bc[0].pos[j]) * ((int64_t)bc[1].vel[j] - bc[0].vel[j]);
        const uint32_t ev = sh2z_bc_pair(&bc[0], &bc[1], 0, 1, k->colliding != 0, &br);
        const int nt = (ev & SH2Z_BC_EV_TOUCH) != 0, nf = (ev & SH2Z_BC_EV_FIRST) != 0, np = (ev & SH2Z_BC_EV_PUSH) != 0;
        const int rt = k->have_t, rf = k->have_t && !k->was, rp = k->have_c && k->natural;
        double pe0 = 0, pe1 = 0;
        for (int j = 0; j < 3; ++j) {   /* reference: racer2 (id 0) += k2 P, racer (id 1) -= k1 P */
            const double r0 = rp ? k->k2 * k->p[j] : 0.0, r1 = rp ? -k->k1 * k->p[j] : 0.0;
            pe0 = fmax(pe0, fabs(bc[0].push[j] / 65536.0 - r0));
            pe1 = fmax(pe1, fabs(bc[1].push[j] / 65536.0 - r1));
        }
        ++tests;
        mis_touch += rt != nt; mis_first += rf != nf; mis_push += rp != np;
        if (rt == nt && rf == nf && rp == np) {
            ++agree;
            worst_push = fmax(worst_push, fmax(pe0, pe1));
            for (int r = 0; r < 2; ++r) {
                const double e = fabs(bc[r].damage / 65536.0 - k->dmg[r]);
                worst_dmg = fmax(worst_dmg, e);
                if (k->dmg[r] > 0.01) worst_dmg_rel = fmax(worst_dmg_rel, e / k->dmg[r]);
            }
        }
        printf("M %d %d %d %d %d %d %d %.9g %lld %.6g %.6g %.6f %.6f %.6f %.6f\n", t, rt, nt, rf, nf, rp, np, k->closing,
               (long long)closing, pe0, pe1, k->dmg[0], bc[0].damage / 65536.0, k->dmg[1], bc[1].damage / 65536.0);
    }
    printf("S tests %ld agree %ld touch_mismatch %ld first_mismatch %ld push_mismatch %ld worst_push %.6g worst_damage %.6g "
           "worst_damage_rel %.6g\n", tests, agree, mis_touch, mis_first, mis_push, worst_push, worst_dmg, worst_dmg_rel);
    return 0;
}
