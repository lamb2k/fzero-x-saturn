/* HOST driver for the engine's attacks (lane claude/attacks): the same engine sources the Saturn image
 * runs for one race (saturn/physics_native.c, saturn/engine/engine_field.c, saturn/engine/engine_attacks.c,
 * SH2ZAM), stepped tick by tick as engine_drive.c's driver mode does. HOST only. MIT; see ../../LICENSE.
 *
 *   fzx-atk --phys PHYS.PAK --rivals RIVALS.PAK [--driver 1|2] [--ticks N] [--vis 0|1] [--ai 0|1]
 *           [--difficulty D] [--report OUT.json] [--trace OUT.tsv] [--mutate NAME]
 *           [--order canonical|saturn] [--frame N] [--clear-check 1] [--gate 1] [--credit-test 1]
 *
 * --driver 1 is the R01 closed-loop driver (the image's E1_DRIVER=1), 2 the scripted hunter.
 * Prints "chk N HASH" every 256 field ticks (fzx_atk_chk, the image keeps the same words) and
 * "body N HASH" every 256 steps (fzx_e1_checkpoints). --mutate plants one rule change for the
 * class controls (see attacks_host.h). The packs are ROM-derived: local only.
 *
 * --order saturn steps as the image does with ENG_ATK_DEFER (lane claude/attacks-5), N ticks a frame (default 2):
 * contacts that fzx_atk_player_clear clears wait behind the queued field tick, the queue runs at the frame's
 * end (after the kick), then the next tick's contact runs ahead with its KO hooks held. Its output must equal
 * --order canonical. --clear-check 1 (canonical order) tests fzx_atk_player_clear on every tick, with and
 * without a pending field tick, against the contact that follows: "clear ... violations 0" is required, and
 * a build with -DFZX_ATK_CLEAR_NOMOTION=1 (the travel bound dropped) is its failing control.
 *
 * --gate 1 prints the reference the engine gate reads for an image built with ENG_ATTACKS (tools/engine_gate.py): "chk N H" the body
 * hash every 256 ticks (the image's fzx_e1_checkpoints), "achk N H" the attack and field words (fzx_atk_chk), "ticks T final H hash 0" the
 * final body hash, and "finish_ms F retire_tick R" (the player's finish time, or its first crashed tick). A tube course is built with
 * -DFZX_PHYS_TUBES=1 (tools/attacks_host/build.sh TUBES=1).
 *
 * --credit-test 1 runs the KO credit rules of the engine (engine_attacks.c) on a freshly loaded field, one rule input at a time, and exits
 * 0 when every case gives what the scored native gives (tools/attacks_host/duel_native.c: the player's flags from before the tick's steps,
 * a 60-tick window at a spin-out start, a 120-tick window at a fall start, one credit a fall). Each rule has its failing control, a build
 * with one rule changed: -DFZX_ATK_CREDIT_POST=1 (the flags after the step, as before 964381e), -DFZX_ATK_FALL_WINDOW=119 and =121. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "physics_native.h"
#include "engine_field.h"
#include "engine_attacks.h"
#include "texture_cd.h"

#include "rivals_course.h"

static const char *rivals_path;
static void swap16(uint8_t *p) { const uint8_t t = p[0]; p[0] = p[1]; p[1] = t; }
static void swap32(uint8_t *p) { uint8_t t = p[0]; p[0] = p[3]; p[3] = t; t = p[1]; p[1] = p[2]; p[2] = t; }

bool fzx_texture_cd_load(const char *name, uint32_t bytes, uint32_t hash, void *dst, void *scratch, uint32_t scratch_bytes) {
    (void)hash; (void)scratch; (void)scratch_bytes;
    if (strcmp(name, "RIVALS.PAK") != 0 || !rivals_path) return false;
    FILE *f = fopen(rivals_path, "rb");
    if (!f) return false;
    const size_t n = fread(dst, 1, bytes, f);
    fclose(f);
    if (n != bytes || bytes < (uint32_t)FZX_RIVALS_NODE_COUNT * 48u) return false;
    /* The pack holds big-endian words (the Saturn's): 16-byte line nodes {s32, s16 x4, u32}, then
     * 32-byte station poses {s32 x3, s16 x10}. Swap them to this host's order. */
    uint8_t *p = dst;
    for (uint32_t i = 0; i < FZX_RIVALS_NODE_COUNT; ++i, p += 16) {
        swap32(p); swap16(p + 4); swap16(p + 6); swap16(p + 8); swap16(p + 10); swap32(p + 12);
    }
    for (uint32_t i = 0; i < FZX_RIVALS_NODE_COUNT; ++i, p += 32) {
        swap32(p); swap32(p + 4); swap32(p + 8);
        for (unsigned k = 0; k < 10u; ++k) swap16(p + 12 + 2 * k);
    }
    return true;
}

static void die(const char *m) { fprintf(stderr, "fzx-atk: %s\n", m); exit(2); }

static uint64_t fnv64_body(const sh2z_vd_body_t *b) {
    /* The image hashes the body's bytes as stored (big-endian words): hash each word's bytes high first. */
    uint64_t h = 0xCBF29CE484222325ull;
    const uint32_t *w = (const uint32_t *)(const void *)b;
    for (unsigned i = 0; i < sizeof(*b) / 4u; ++i)
        for (int k = 24; k >= 0; k -= 8) h = (h ^ ((w[i] >> k) & 0xFFu)) * 0x100000001B3ull;
    return h;
}
static double q30d(int32_t v) { return v / 1073741824.0; }
static signed char driver_stick(const sh2z_vd_body_t *b) {
    const float lateral = (float)(sh2z_vd_body_lateral(b) / 65536.0);
    const double h = q30d(b->body[0][0]) * q30d(b->road[2][0]) + q30d(b->body[0][1]) * q30d(b->road[2][1]) +
                     q30d(b->body[0][2]) * q30d(b->road[2][2]);
    const float heading = (float)h;
    float steer = 0.004f * (lateral - 0.0f) + 2.5f * heading;
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;
    return (signed char)(steer >= 0.0f ? (int)(steer * 63.0f + 0.5f) : -(int)(-steer * 63.0f + 0.5f));
}

static unsigned char *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open a pack");
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 8);
    if (!b || fread(b, 1, *n, f) != *n) die("short read");
    fclose(f);
    return b;
}

static uint64_t ns_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static uint64_t t_contact, t_step, t_field;
static uint32_t ko_hook_calls;
/* --order saturn: the image's queue (engine_drive.c), field ticks and waiting contacts in order. */
static sh2z_vd_body_t q_body[8];
static uint8_t q_contact[8];
static unsigned q_n, q_contacts;
static uint32_t q_bad;
static void q_run(void) {
    for (unsigned k = 0; k < q_n; ++k) {
        if (q_contact[k]) {
            sh2z_vd_contact_t c;
            fzx_atk_contact(&q_body[k], &c);
            if (c.push[0] | c.push[1] | c.push[2] | c.damage | c.hit | c.regain) ++q_bad;
        } else fzx_field_tick(&q_body[k]);
    }
    q_n = 0; q_contacts = 0;
}
static int player_clear(const sh2z_vd_body_t *b, bool pending) {
    int32_t d, l;
    fzx_field_player_line(b, &d, &l);
    return fzx_atk_player_clear(b, d, l, pending);
}
static int nonzero(const sh2z_vd_contact_t *c) { return (c->push[0] | c->push[1] | c->push[2] | c->damage | c->hit | c->regain) != 0; }
static void ko_hook(uint32_t victim) { (void)victim; ++ko_hook_calls; }

/* ---- the KO credit rules, case by case (--credit-test) ---- */
typedef struct { const char *name; int fall; int touch; uint32_t since; uint32_t pre, post; unsigned expect; } credit_case_t;
static int credit_tests(const sh2z_vd_body_t *start) {
    const uint32_t CLEAN = SH2Z_VD_ACTIVE, SPIN = SH2Z_VD_ACTIVE | SH2Z_VD_SPINNING, CRASH = SH2Z_VD_ACTIVE | SH2Z_VD_CRASHED,
                   FIN = SH2Z_VD_ACTIVE | SH2Z_VD_FINISHED;
    /* since is the counter before the tick's contact ages it by one: 59 is a 60-tick-old contact, 60 is 61 */
    static const credit_case_t cases[] = {
        { "spin-out: touched 5 ticks ago, player clean", 0, 1, 5, CLEAN, CLEAN, 1 },
        { "spin-out: contact 60 ticks old (window edge)", 0, 1, 59, CLEAN, CLEAN, 1 },
        { "spin-out: contact 61 ticks old", 0, 1, 60, CLEAN, CLEAN, 0 },
        { "spin-out: never touched by the player", 0, 0, 0, CLEAN, CLEAN, 0 },
        { "spin-out: player spinning before the tick", 0, 1, 5, SPIN, SPIN, 0 },
        { "spin-out: player crashed before the tick", 0, 1, 5, CRASH, CRASH, 0 },
        { "spin-out: player finished before the tick", 0, 1, 5, FIN, FIN, 0 },
        { "spin-out: both spin out on this tick (clean before, spinning after)", 0, 1, 5, CLEAN, SPIN, 1 },
        { "spin-out: player spinning before, clean after", 0, 1, 5, SPIN, CLEAN, 0 },
        { "fall: touched 5 ticks ago, player clean", 1, 1, 5, CLEAN, CLEAN, 1 },
        { "fall: contact 120 ticks old (window edge)", 1, 1, 118, CLEAN, CLEAN, 1 },
        { "fall: contact 121 ticks old", 1, 1, 119, CLEAN, CLEAN, 0 },
        { "fall: never touched by the player", 1, 0, 0, CLEAN, CLEAN, 0 },
        { "fall: player spinning before the tick", 1, 1, 5, SPIN, SPIN, 0 },
        { "fall: player clean before, spinning after", 1, 1, 5, CLEAN, SPIN, 1 },
    };
    int bad = 0;
    const uint32_t regain_q16 = (uint32_t)(((fzx_atk_field()->config[3]) << 8) / 8);
    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
        const credit_case_t *c = &cases[k];
        fzx_atk_field_t f = *fzx_atk_field();
        fzx_atk_init(&f, 1);
        f.veh[1].energy = 256; f.veh[1].flags = 0;
        fzx_atk_vis_gate = 0; fzx_atk_ai = 0; ko_hook_calls = 0;
        sh2z_vd_body_t pre = *start, post = *start;
        pre.flags = c->pre; post.flags = c->post;
        if (c->touch) fzx_atk_test_touch(1, c->since);
        sh2z_vd_contact_t out;
        fzx_atk_contact(&pre, &out);                               /* the tick's contact: ages the counter, sees the flags before the step */
        if (c->fall) {
            f.veh[1].flags |= SH2Z_VEH_FALLING;                    /* the field's step flags the rival falling */
            fzx_atk_after(&post);
            fzx_atk_after(&post);                                  /* a second look at the same fall: one credit only */
        } else {
            fzx_atk_test_damage(&post, 1, 50 * 65536);             /* damage enough to spin it out, with the state after the step */
        }
        const unsigned kos = fzx_atk_report[3];
        fzx_atk_contact(&pre, &out);                               /* the next tick's contact carries the energy regain */
        const int regain_ok = kos == 0 ? out.regain == 0 : (uint32_t)out.regain == regain_q16;
        const int ok = kos == c->expect && ko_hook_calls == c->expect && fzx_atk_ko_count[0] == c->expect && regain_ok;
        printf("credit %-3s %2u %s: %u KO (want %u), hook %u, regain %s\n", ok ? "ok" : "BAD", k + 1, c->name, kos, c->expect, ko_hook_calls,
               regain_ok ? "ok" : "BAD");
        bad += !ok;
    }
    printf("credit-test %s: %d of %u cases wrong\n", bad ? "FAIL" : "PASS", bad, (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *phys = NULL, *report = NULL, *trace = NULL;
    int driver = 1, ticks_max = 9460, vis = 1, ai = 1, diff = 1, saturn = 0, frame = 2, clear_check = 0, gate = 0, credit_test = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--phys")) phys = argv[i + 1];
        else if (!strcmp(argv[i], "--rivals")) rivals_path = argv[i + 1];
        else if (!strcmp(argv[i], "--driver")) driver = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) ticks_max = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--vis")) vis = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ai")) ai = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--difficulty")) diff = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--order")) saturn = !strcmp(argv[i + 1], "saturn");
        else if (!strcmp(argv[i], "--frame")) frame = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--clear-check")) clear_check = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--gate")) gate = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--credit-test")) credit_test = atoi(argv[i + 1]);
        else die("unknown argument");
    }
    if (!phys || !rivals_path) die("usage: fzx-atk --phys PHYS.PAK --rivals RIVALS.PAK ...");
    size_t n;
    unsigned char *image = read_all(phys, &n);
    static FzxPhysCourse course;
    if (fzx_phys_load(&course, image, n, 1) != 0) die("bad physics pack");
    sh2z_vd_body_t body = course.start;
    static uint32_t scratch[64];
    if (!fzx_field_pack_load(scratch, sizeof(scratch))) die("RIVALS.PAK");
    if (!fzx_field_load(&course.track, &body)) {
        fprintf(stderr, "field status %u stations %u e2 %u\n", (unsigned)fzx_field_report[0], (unsigned)fzx_field_report[1],
                (unsigned)fzx_e2_report[0]);
        die("field load");
    }
    fzx_atk_vis_gate = (uint32_t)vis; fzx_atk_ai = (uint32_t)ai; fzx_atk_difficulty = (uint32_t)diff;
    fzx_atk_ko_hook = ko_hook;
    if (credit_test) return credit_tests(&body);
    FILE *tf = trace ? fopen(trace, "w") : NULL;
    enum { RACE_START_TICK = 460u };
    uint16_t prev = 0;
    uint32_t t = 0;
    sh2z_vd_contact_t ahead;
    int ahead_ok = 0;
    uint32_t deferred = 0, aheads = 0, clear0 = 0, clear1 = 0, viol0 = 0, viol1 = 0;
    int32_t retire_tick = -1;
    int pred0 = 0, pred1 = 0;
    for (; t < (uint32_t)ticks_max; ++t) {
        uint16_t button = t >= RACE_START_TICK ? 0x8000u : 0u;
        signed char sx = 0;
        if (body.intro == 0) {
            sx = driver_stick(&body);
            if (driver == 2) {
                uint16_t hb; int8_t hs;
                if (saturn) q_run();   /* the hunter reads the field (engine_drive.c) */
                (void)fzx_atk_hunter(&body, &hb, &hs);
                if (t >= RACE_START_TICK) button = hb;
                sx = hs;
            }
        }
        sh2z_vd_input_t in;
        in.held = fzx_phys_buttons_n64(button);
        in.pressed = fzx_phys_buttons_n64((uint16_t)((prev ^ button) & button));
        in.released = fzx_phys_buttons_n64((uint16_t)((prev ^ button) & prev));
        in.stick_x = sx; in.stick_y = 0;
        prev = button;
        sh2z_vd_contact_t contact;
        const uint64_t a0 = ns_now();
        if (saturn) {
            if (ahead_ok) { ahead_ok = 0; contact = ahead; fzx_atk_release_hooks(); }
            else if (q_n == 1u && !q_contacts && player_clear(&body, true)) {
                q_body[q_n] = body; q_contact[q_n] = 1u; ++q_n; ++q_contacts; ++deferred;
                memset(&contact, 0, sizeof(contact));
            } else { q_run(); fzx_atk_contact(&body, &contact); }
        } else {
            if (clear_check) { pred0 = player_clear(&body, false); clear0 += (uint32_t)pred0; }
            fzx_atk_contact(&body, &contact);
            if (clear_check) {
                if (pred0 && nonzero(&contact)) ++viol0;
                if (pred1 && nonzero(&contact)) ++viol1;
            }
        }
        const uint64_t a1 = ns_now();
#if FZX_PHYS_TUBES
        sh2z_vd_step2_contact(&body, &course.track2, &course.vehicle, &fzx_phys_rules, &in, &contact);
#else
        sh2z_vd_step_contact(&body, &course.track, &course.vehicle, &fzx_phys_rules, &in, &contact);
#endif
        if (retire_tick < 0 && (body.flags & SH2Z_VD_CRASHED)) retire_tick = (int32_t)t;
        const uint64_t a2 = ns_now();
        if (saturn) {
            if (q_n >= 8u) q_run();
            q_body[q_n] = body; q_contact[q_n] = 0u; ++q_n;
            if ((t + 1u) % (uint32_t)frame == 0u) {   /* the frame's end: the queue after the kick, then the next contact */
                q_run();
                if (t + 1u < (uint32_t)ticks_max && driver != 2) {   /* the hunter reads the attack state first */
                    fzx_atk_hold_hooks(true); fzx_atk_contact(&body, &ahead); fzx_atk_hold_hooks(false);
                    ahead_ok = 1; ++aheads;
                }
            }
        } else {
            if (clear_check) { pred1 = player_clear(&body, true); clear1 += (uint32_t)pred1; }
            fzx_field_tick(&body);
        }
        const uint64_t a3 = ns_now();
        t_contact += a1 - a0; t_step += a2 - a1; t_field += a3 - a2;
        if ((t + 1u) % 256u == 0u && (t + 1u) / 256u <= 64u) {
            if (saturn) q_run();   /* not needed for the body; keeps chk words in step with the canonical order */
            printf(gate ? "chk %u %016llx\n" : "body %u %016llx\n", t + 1u, (unsigned long long)fnv64_body(&body));
        }
        if (tf) fprintf(tf, "%u\t%d\t%d\t%u\t%u\t%u\n", t, body.energy, body.speed, (unsigned)body.flags, fzx_atk_report[3], fzx_atk_report[5]);
    }
    if (saturn) {
        q_run();
        fprintf(stderr, "saturn order: frame %d, deferred %u, ahead %u, deferred with a player output %u\n", frame, deferred, aheads, q_bad);
    }
    if (clear_check) printf("clear now %u violations %u; clear behind a field tick %u violations %u\n", clear0, viol0, clear1, viol1);
    if (tf) fclose(tf);
#ifdef ATK_PROF
    { extern uint64_t atk_prof[8]; fprintf(stderr, "contact parts ns/tick: begin %.0f sort %.0f candidates %.0f bodies %.0f resolve %.0f outputs %.0f\n", (double)atk_prof[0] / t, (double)atk_prof[1] / t, (double)atk_prof[2] / t, (double)atk_prof[3] / t, (double)atk_prof[4] / t, (double)atk_prof[5] / t); }
#endif
    {   /* wrecks: retired rivals, those at rest, and the fastest still moving (units a tick) */
        const fzx_atk_field_t *af = fzx_atk_field();
        unsigned wrecks = 0, rest = 0;
        int32_t creep = 0;
        for (uint32_t i = 1; i < af->count; ++i) {
            const sh2z_vehicle_t *v = &af->veh[i];
            if (!(v->flags & SH2Z_VEH_RETIRED) || (v->flags & SH2Z_VEH_FINISHED)) continue;
            ++wrecks;
            if (v->flags & SH2Z_VEH_STOPPED) ++rest;
            else if (v->t.speed > creep) creep = v->t.speed;
        }
        printf("wrecks %u at_rest %u fastest_moving %.4f\n", wrecks, rest, creep / 256.0);
    }
    printf("damage sources (rivals): contact %u / %.1f, attack %u / %.1f, wall %u / %.1f\n", fzx_atk_dmg_src[0][0],
           fzx_atk_dmg_src[0][1] / 256.0, fzx_atk_dmg_src[1][0], fzx_atk_dmg_src[1][1] / 256.0, fzx_atk_dmg_src[2][0],
           fzx_atk_dmg_src[2][1] / 256.0);
    fprintf(stderr, "HOST ns per tick: contact %.0f, player step %.0f, field tick (attacks move/after inside) %.0f\n", (double)t_contact / t, (double)t_step / t, (double)t_field / t);
    for (unsigned i = 0; i < FZX_ATK_CHECKPOINTS; ++i) if (fzx_atk_chk[i]) printf(gate ? "achk %u %08x\n" : "chk %u %08x\n", (i + 1u) * 256u, fzx_atk_chk[i]);
    if (gate) {
        printf("ticks %u final %016llx hash 0\n", t, (unsigned long long)fnv64_body(&body));
        if (body.flags & SH2Z_VD_FINISHED) printf("finish_ms %d retire_tick -\n", body.race_time);
        else if (retire_tick >= 0) printf("finish_ms - retire_tick %d\n", retire_tick);
        else printf("finish_ms - retire_tick -\n");
    }
    printf("ticks %u race_ms %d finished %u kos %u ko_hook %u spun %u retired_by_damage %u field_retired %u contacts %u cpu_side %u cpu_spin %u hits %u player_hits %u held_off_screen %u player_damage %u hash %08x\n",
           t, body.race_time, (unsigned)fzx_field_report[11], fzx_atk_report[3], ko_hook_calls, fzx_atk_report[4], fzx_atk_report[5],
           (unsigned)fzx_field_report[12], fzx_atk_report[0], fzx_atk_report[6], fzx_atk_report[7], fzx_atk_report[8], fzx_atk_report[12],
           fzx_atk_report[9], fzx_atk_report[10], fzx_atk_report[14]);
    if (report) {
        FILE *o = fopen(report, "w");
        if (!o) die("cannot write the report");
        fprintf(o, "{\"tier\": \"HOST\", \"driver\": %d, \"vis_gate\": %d, \"ai\": %d, \"difficulty\": %d, \"ticks\": %u, \"race_ms\": %d,\n",
                driver, vis, ai, diff, t, body.race_time);
        fprintf(o, " \"player_finished\": %d, \"player_crashed\": %d, \"rivals_finished\": %u, \"rivals_retired\": %u, \"kos\": %u, \"spun\": %u,\n",
                (body.flags & SH2Z_VD_FINISHED) ? 1 : 0, (body.flags & SH2Z_VD_CRASHED) ? 1 : 0, (unsigned)fzx_field_report[11],
                (unsigned)fzx_field_report[12], fzx_atk_report[3], fzx_atk_report[4]);
        fprintf(o, " \"contacts\": %u, \"cpu_side\": %u, \"cpu_spin\": %u, \"hits\": %u, \"player_hits\": %u, \"held_off_screen\": %u, \"player_damage\": %u, \"hash\": \"%08x\",\n",
                fzx_atk_report[0], fzx_atk_report[6], fzx_atk_report[7], fzx_atk_report[8], fzx_atk_report[12], fzx_atk_report[9],
                fzx_atk_report[10], fzx_atk_report[14]);
        fprintf(o, " \"kos_log\": [");
        const unsigned nk = fzx_atk_report[3] < FZX_ATK_KO_LOG ? fzx_atk_report[3] : FZX_ATK_KO_LOG;
        for (unsigned i = 0; i < nk; ++i) fprintf(o, "%s[%u, %u, %u]", i ? ", " : "", fzx_atk_kos[i].tick, fzx_atk_kos[i].victim, fzx_atk_kos[i].attacker);
        fprintf(o, "],\n \"retire\": [");
        for (unsigned i = 0; i < FZX_FIELD_CARS; ++i) fprintf(o, "%s[%d, %u]", i ? ", " : "", fzx_atk_retire_tick[i], fzx_atk_retire_place[i]);
        fprintf(o, "]}\n");
        fclose(o);
    }
    return 0;
}
