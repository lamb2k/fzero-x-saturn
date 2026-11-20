/* Native duel window (attacks lane, claude/attacks): two native bodies (SH2ZAM vehicle-dynamics-3) in
 * contact through SH2ZAM body-contact-1, started from the duel reference's state after tick T and
 * driven by the reference's recorded pads from T + 1. The engine's rules (engine_attacks.c) for a
 * human player (racer 0: heavy, credited) against an opponent (racer 1). HOST only. MIT; see ../../LICENSE.
 *
 *   fzx-duel-native --pack0 P0 --pack1 P1 --pads PADS --from T --ticks N --machines c0,c1
 *                   [--since S --toucher K --touching 0|1] [--trace OUT.tsv] [--mutate NAME]
 *
 * --spin-side0/1 S (+1 or -1) gives a spin-out the side the reference drew (a random choice there),
 * from the tick after the spin-out starts (the round-2 scorer's form). --spin-draws FILE ("t k sign"
 * per reference draw) gives every draw the reference's side on the tick it is drawn (C-092 scorer).
 * The trace has the reference's columns (ref/duel_main.c). --mutate plants one rule change for the
 * class controls: impact1.2, push2.5, share-swap, spin140, credit-off, heavy9, heavy5, reach45, and
 * opp-player (racer 1 with the player's spline search, as --opponent-search player).
 *
 * --pairlog FILE (C-092): per tick, the pair test as the body-contact rule decided it, from the
 * inputs it read (logging only; d2 and closing repeat sh2z_bc_pair's integer terms):
 *   N t along_ok d2_q32 closing_q32 touch first push hit0 hit1 damage0 damage1 (damage Q16 raw)
 *   K t victim attacker      a KO credited at the victim's spin-out start
 *
 * Racer 1 is stepped as the reference's racer 1 is: an id at or above gNumPlayers, whose spline
 * search runs with the computer racers' bounds (3 iterations, squared tolerance 5) where the human
 * player's has 100 and 1 (tools/attacks_host/duel_build.sh builds that step from the library source).
 * --opponent-search player gives racer 1 the player's search (the control; the runner before C-092
 * round 4). --regain-pending 1: racer 0 starts with the KO energy regain the reference credited on
 * tick T still pending (it is applied at the start of the next tick; the bake does not carry it). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_native.h"
#include "sh2zam/body_contact_experimental.h"
#include "attacks_tables.h"

static void die(const char *m) { fprintf(stderr, "fzx-duel-native: %s\n", m); exit(2); }
static unsigned char *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open input");
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 8);
    if (!b || fread(b, 1, *n, f) != *n) die("short read");
    fclose(f);
    return b;
}
#define Q16(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))

/* The opponent's step (duel_build.sh): the library step with the computer racers' spline search. */
#if FZX_PHYS_TUBES
void fzx_duel_opp_step2_contact(sh2z_vd_body_t *b, const sh2z_vd_track2_t *track, const sh2z_vd_vehicle_t *v,
                                const sh2z_vd_rules_t *r, const sh2z_vd_input_t *in, const sh2z_vd_contact_t *c);
#else
void fzx_duel_opp_step_contact(sh2z_vd_body_t *b, const sh2z_vd_track_t *track, const sh2z_vd_vehicle_t *v,
                               const sh2z_vd_rules_t *r, const sh2z_vd_input_t *in, const sh2z_vd_contact_t *c);
#endif

static FzxPhysCourse course[2];
static int opp_cpu = 1;   /* racer 1 searches the spline with the computer bounds */

static void step(int k, sh2z_vd_body_t *b, const sh2z_vd_rules_t *rules, const sh2z_vd_input_t *in, const sh2z_vd_contact_t *c) {
#if FZX_PHYS_TUBES
    if (k == 1 && opp_cpu) fzx_duel_opp_step2_contact(b, &course[k].track2, &course[k].vehicle, rules, in, c);
    else sh2z_vd_step2_contact(b, &course[k].track2, &course[k].vehicle, rules, in, c);
#else
    if (k == 1 && opp_cpu) fzx_duel_opp_step_contact(b, &course[k].track, &course[k].vehicle, rules, in, c);
    else sh2z_vd_step_contact(b, &course[k].track, &course[k].vehicle, rules, in, c);
#endif
}
static int pads_b[2][20000], pads_x[2][20000];
static signed char draw[2][20000];   /* --spin-draws: the reference's spin side per tick and racer (0 none) */

int main(int argc, char **argv) {
    const char *p0 = 0, *p1 = 0, *pads = 0, *trace = 0, *mutate = "", *pairlog = 0, *draws = 0;
    int from = -1, ticks = 120, c0 = 0, c1 = 0, since = -1, toucher = -1, touching = 0, side[2] = { 0, 0 }, nudge = 0, regain_pending = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--pack0")) p0 = argv[i + 1];
        else if (!strcmp(argv[i], "--pack1")) p1 = argv[i + 1];
        else if (!strcmp(argv[i], "--pads")) pads = argv[i + 1];
        else if (!strcmp(argv[i], "--from")) from = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) ticks = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--since")) since = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--toucher")) toucher = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--touching")) touching = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--pairlog")) pairlog = argv[i + 1];
        else if (!strcmp(argv[i], "--nudge")) nudge = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--spin-side0")) side[0] = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--spin-side1")) side[1] = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--spin-draws")) draws = argv[i + 1];
        else if (!strcmp(argv[i], "--mutate")) mutate = argv[i + 1];
        else if (!strcmp(argv[i], "--regain-pending")) regain_pending = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--opponent-search")) {
            if (!strcmp(argv[i + 1], "cpu")) opp_cpu = 1;
            else if (!strcmp(argv[i + 1], "player")) opp_cpu = 0;
            else die("--opponent-search cpu|player");
        }
        else if (!strcmp(argv[i], "--machines")) { if (sscanf(argv[i + 1], "%d,%d", &c0, &c1) != 2) die("--machines c0,c1"); }
        else die("unknown argument");
    }
    if (!p0 || !p1 || !pads || from < 0) die("usage: see duel_native.c");
    size_t n;
    unsigned char *i0 = read_all(p0, &n);
    if (fzx_phys_load(&course[0], i0, n, 1) != 0) die("bad pack 0");
    unsigned char *i1 = read_all(p1, &n);
    if (fzx_phys_load(&course[1], i1, n, 1) != 0) die("bad pack 1");
    if (draws) {
        FILE *df = fopen(draws, "r");
        int dt, dk, ds;
        if (!df) die("cannot open the spin draws");
        while (fscanf(df, "%d %d %d", &dt, &dk, &ds) == 3)
            if (dt >= 0 && dt < 20000 && (dk == 0 || dk == 1)) draw[dk][dt] = ds > 0 ? 1 : -1;
        fclose(df);
    }
    FILE *pf = fopen(pads, "r");
    if (!pf) die("cannot open the pads");
    int t, b0, x0, b1, x1, last = -1;
    while (fscanf(pf, "%d %d %d %d %d", &t, &b0, &x0, &b1, &x1) == 5 && t < 20000) {
        pads_b[0][t] = b0; pads_x[0][t] = x0; pads_b[1][t] = b1; pads_x[1][t] = x1; last = t;
    }
    fclose(pf);
    sh2z_vd_rules_t rules = fzx_phys_rules;
    sh2z_bc_rules_t br;
    memset(&br, 0, sizeof(br));
    br.reach_long = 92 * 256; br.reach = Q16(46.0); br.near = Q16(0.01); br.impact_gain = Q16(1.1);
    br.attack_push = Q16(3.0); br.attack_min = Q16(3.0); br.weak_share = 21823; br.heavy_gain = Q16(10.0);
    br.credit_window = 60;
    int share_swap = 0, credit = 1;
    if (!strcmp(mutate, "impact1.2")) br.impact_gain = Q16(1.2);
    else if (!strcmp(mutate, "push2.5")) br.attack_push = Q16(2.5);
    else if (!strcmp(mutate, "share-swap")) share_swap = 1;
    else if (!strcmp(mutate, "spin140")) rules.spin_ticks = 140;
    else if (!strcmp(mutate, "credit-off")) credit = 0;
    else if (!strcmp(mutate, "reach45")) br.reach = Q16(45.0);
    else if (!strcmp(mutate, "heavy9")) br.heavy_gain = Q16(9.0);
    else if (!strcmp(mutate, "heavy5")) br.heavy_gain = Q16(5.0);
    else if (!strcmp(mutate, "opp-player")) opp_cpu = 0;   /* the control for the opponent's search */
    else if (*mutate) die("unknown mutation");
    sh2z_vd_body_t b[2] = { course[0].start, course[1].start };
    /* --nudge N: racer 0 starts N Q16 LSB to the side (the sensitivity probe: native against itself). */
    for (int k = 0; k < 3; ++k) b[0].pos[k] += (int32_t)(((int64_t)b[0].body[2][k] * nudge) >> 30);
    sh2z_bc_body_t bc[2];
    memset(bc, 0, sizeof(bc));
    bc[0].strength = fzx_atk_machine[c0][0]; bc[1].strength = fzx_atk_machine[c1][0];
    if (share_swap) { const uint32_t s = bc[0].strength; bc[0].strength = bc[1].strength; bc[1].strength = s; }
    bc[0].toucher = -1; bc[0].since = UINT32_MAX;
    bc[1].toucher = (int16_t)(toucher == 0 ? 0 : -1);
    bc[1].since = since > 0 ? (uint32_t)since - 1u : UINT32_MAX;   /* the reference's unk_288 counts the contact tick as 1 */
    if (!credit) bc[1].toucher = -1;   /* the control: no contact is credited, the seeded one included */
    bool was_touching = touching != 0;
    int32_t regain = regain_pending ? course[0].vehicle.max_energy / 8 : 0;
    int kos = 0;
    /* The buttons held on tick T (the duel bake carries no pad state): edges from T + 1 on are the reference's. */
    uint16_t prev[2] = { (uint16_t)pads_b[0][from], (uint16_t)pads_b[1][from] };
    FILE *tf = trace ? fopen(trace, "w") : NULL;
    if (trace && !tf) die("cannot write the trace");
    FILE *lf = pairlog ? fopen(pairlog, "w") : NULL;
    if (pairlog && !lf) die("cannot write the pair log");
    for (t = from + 1; t <= from + ticks && t <= last; ++t) {
        /* Contact first, on the state after the previous tick (Racer_Update's order). */
        for (int k = 0; k < 2; ++k) {
            sh2z_bc_body_t *c = &bc[k];
            if (c->since != UINT32_MAX) c->since++;
            c->push[0] = c->push[1] = c->push[2] = 0; c->damage = 0; c->hit = 0; c->first = 0;
            for (int j = 0; j < 3; ++j) { c->pos[j] = b[k].pos[j]; c->vel[j] = b[k].vel[j]; }
            c->flags = (b[k].flags & SH2Z_VD_ACTIVE) ? SH2Z_BC_ACTIVE : 0u;
            if (b[k].attack != SH2Z_VD_ATTACK_NONE) c->flags |= SH2Z_BC_ATTACK;
            if (b[k].flags & SH2Z_VD_CRASHED) c->flags |= SH2Z_BC_WEAK;
        }
        bc[0].flags |= SH2Z_BC_HEAVY | (credit ? SH2Z_BC_CREDIT : 0u);
        int64_t gap = (int64_t)b[1].lap_dist - b[0].lap_dist;   /* Q12, wrapped */
        const int64_t len = course[0].track.length;
        if (gap > len / 2) gap -= len;
        if (gap < -len / 2) gap += len;
        uint32_t ev = 0;
        const int along_ok = (bc[0].flags & bc[1].flags & SH2Z_BC_ACTIVE) && (gap < 0 ? -gap : gap) <= ((int64_t)92 << 12);
        int64_t d2 = 0, closing = 0;
        for (int k = 0; k < 3; ++k) {   /* the rule's own terms: dx = b - a (Q16), dv saturated, both exact in int64 */
            const int64_t dx = (int64_t)bc[1].pos[k] - bc[0].pos[k];
            int64_t dv = (int64_t)bc[1].vel[k] - bc[0].vel[k];
            if (dv > INT32_MAX) dv = INT32_MAX;
            if (dv < INT32_MIN) dv = INT32_MIN;
            d2 += dx * dx;
            closing += dx * dv;
        }
        if (along_ok)
            ev = sh2z_bc_pair(&bc[0], &bc[1], 0, 1, was_touching, &br);
        if (lf)
            fprintf(lf, "N %d %d %lld %lld %d %d %d %d %d %ld %ld\n", t, along_ok, (long long)d2, (long long)closing,
                    (ev & SH2Z_BC_EV_TOUCH) != 0, (ev & SH2Z_BC_EV_FIRST) != 0, (ev & SH2Z_BC_EV_PUSH) != 0, bc[0].hit, bc[1].hit,
                    (long)bc[0].damage, (long)bc[1].damage);
        was_touching = (ev & SH2Z_BC_EV_TOUCH) != 0;
        const uint32_t spin_before = b[1].flags & SH2Z_VD_SPINNING, spin_before0 = b[0].flags & SH2Z_VD_SPINNING;
        for (int k = 0; k < 2; ++k) {
            const uint16_t btn = (uint16_t)pads_b[k][t];
            sh2z_vd_input_t in;
            in.held = fzx_phys_buttons_n64(btn);
            in.pressed = fzx_phys_buttons_n64((uint16_t)((prev[k] ^ btn) & btn));
            in.released = fzx_phys_buttons_n64((uint16_t)((prev[k] ^ btn) & prev[k]));
            in.stick_x = (int8_t)pads_x[k][t]; in.stick_y = 0;
            prev[k] = btn;
            sh2z_vd_contact_t c;
            c.push[0] = bc[k].push[0]; c.push[1] = bc[k].push[1]; c.push[2] = bc[k].push[2];
            c.damage = bc[k].damage; c.hit = bc[k].hit; c.regain = k == 0 ? regain : 0;
            if (k == 0) regain = 0;
            const sh2z_vd_body_t pre = b[k];
            const sh2z_vd_contact_t c_in = c;
            step(k, &b[k], &rules, &in, &c);
            /* --spin-draws: where the reference drew a spin side on this tick, the native takes the
             * same side at its own draw, inside the step (the step turns by it at once). The native
             * side is the parity of race_ticks: the step is redone with the parity flipped, and the
             * clock kept from the first run (race_ticks is read only by the draw and the clock). */
            const int want = t < 20000 ? draw[k][t] : 0;
            if (want && b[k].spin_steer != 0 && ((b[k].spin_steer > 0) != (want > 0))) {
                sh2z_vd_body_t alt = pre;
                sh2z_vd_contact_t c2 = c_in;
                alt.race_ticks ^= 1;
                step(k, &alt, &rules, &in, &c2);
                if (alt.spin_steer != 0 && ((alt.spin_steer > 0) == (want > 0))) {
                    alt.race_ticks = b[k].race_ticks; alt.race_time = b[k].race_time;
                    b[k] = alt;
                }
            }
        }
        /* The spin-out side is a random draw in the original: take the reference's (--spin-side*). */
        for (int k = 0; k < 2; ++k) {
            const uint32_t was = k ? spin_before : spin_before0;
            if (!was && (b[k].flags & SH2Z_VD_SPINNING) && side[k])
                b[k].spin_steer = side[k] * (b[k].spin_steer < 0 ? -b[k].spin_steer : b[k].spin_steer);
        }
        /* KO credit at the opponent's spin-out start (contact damage or walls this tick). */
        if (!spin_before && (b[1].flags & SH2Z_VD_SPINNING) && bc[1].toucher == 0 && bc[1].since <= br.credit_window &&
            !(b[0].flags & (SH2Z_VD_FINISHED | SH2Z_VD_CRASHED | SH2Z_VD_SPINNING))) {
            ++kos;
            regain += course[0].vehicle.max_energy / 8;
            if (lf) fprintf(lf, "K %d 1 %d\n", t, bc[1].toucher);
        }
        if (tf) {
            for (int k = 0; k < 2; ++k) {
                const sh2z_vd_body_t *v = &b[k];
                const double heading = ((double)v->body[0][0] * v->road[2][0] + (double)v->body[0][1] * v->road[2][1] +
                                        (double)v->body[0][2] * v->road[2][2]) / 1073741824.0 / 1073741824.0;
                const int flags = (int)((v->flags & ~UINT32_C(0x08000000)) | ((v->flags & SH2Z_VD_CRASHED) ? 0x08040000u : 0u));
                fprintf(tf, "%d\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%d\t%d\t%d\t%d\t%u\t%d\t%.6f\n", t, k,
                        v->pos[0] / 65536.0, v->pos[1] / 65536.0, v->pos[2] / 65536.0, v->vel[0] / 65536.0, v->vel[1] / 65536.0,
                        v->vel[2] / 65536.0, v->speed / 65536.0, v->energy / 65536.0, sh2z_vd_body_lateral(v) / 65536.0, heading,
                        v->lap_dist / 4096.0, flags, v->attack, v->spin_timer, kos, bc[k].since, bc[k].toucher, v->spin_steer / 65536.0);
            }
        }
    }
    if (tf) fclose(tf);
    if (lf) fclose(lf);
    return 0;
}
