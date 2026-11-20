/* HOST driver for the engine's attacks (lane claude/attacks): the same engine sources the Saturn image
 * runs for one race (saturn/physics_native.c, saturn/engine/engine_field.c, saturn/engine/engine_attacks.c,
 * SH2ZAM), stepped tick by tick as engine_drive.c's driver mode does. HOST only. MIT; see ../../LICENSE.
 *
 *   fzx-atk --phys PHYS.PAK --rivals RIVALS.PAK [--driver 1|2] [--ticks N] [--vis 0|1] [--ai 0|1]
 *           [--difficulty D] [--report OUT.json] [--trace OUT.tsv] [--mutate NAME]
 *
 * --driver 1 is the R01 closed-loop driver (the image's E1_DRIVER=1), 2 the scripted hunter.
 * Prints "chk N HASH" every 256 field ticks (fzx_atk_chk, the image keeps the same words) and
 * "body N HASH" every 256 steps (fzx_e1_checkpoints). --mutate plants one rule change for the
 * class controls (see attacks_host.h). The packs are ROM-derived: local only. */
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
static void ko_hook(uint32_t victim) { (void)victim; ++ko_hook_calls; }

int main(int argc, char **argv) {
    const char *phys = NULL, *report = NULL, *trace = NULL;
    int driver = 1, ticks_max = 9460, vis = 1, ai = 1, diff = 1;
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
    FILE *tf = trace ? fopen(trace, "w") : NULL;
    enum { RACE_START_TICK = 460u };
    uint16_t prev = 0;
    uint32_t t = 0;
    for (; t < (uint32_t)ticks_max; ++t) {
        uint16_t button = t >= RACE_START_TICK ? 0x8000u : 0u;
        signed char sx = 0;
        if (body.intro == 0) {
            sx = driver_stick(&body);
            if (driver == 2) {
                uint16_t hb; int8_t hs;
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
        fzx_atk_contact(&body, &contact);
        const uint64_t a1 = ns_now();
        sh2z_vd_step_contact(&body, &course.track, &course.vehicle, &fzx_phys_rules, &in, &contact);
        const uint64_t a2 = ns_now();
        fzx_field_tick(&body);
        const uint64_t a3 = ns_now();
        t_contact += a1 - a0; t_step += a2 - a1; t_field += a3 - a2;
        if ((t + 1u) % 256u == 0u && (t + 1u) / 256u <= 64u) printf("body %u %016llx\n", t + 1u, (unsigned long long)fnv64_body(&body));
        if (tf) fprintf(tf, "%u\t%d\t%d\t%u\t%u\t%u\n", t, body.energy, body.speed, (unsigned)body.flags, fzx_atk_report[3], fzx_atk_report[5]);
    }
    if (tf) fclose(tf);
#ifdef ATK_PROF
    { extern uint64_t atk_prof[8]; fprintf(stderr, "contact parts ns/tick: begin %.0f sort %.0f candidates %.0f bodies %.0f resolve %.0f outputs %.0f\n", (double)atk_prof[0] / t, (double)atk_prof[1] / t, (double)atk_prof[2] / t, (double)atk_prof[3] / t, (double)atk_prof[4] / t, (double)atk_prof[5] / t); }
#endif
    fprintf(stderr, "HOST ns per tick: contact %.0f, player step %.0f, field tick (attacks move/after inside) %.0f\n", (double)t_contact / t, (double)t_step / t, (double)t_field / t);
    for (unsigned i = 0; i < FZX_ATK_CHECKPOINTS; ++i) if (fzx_atk_chk[i]) printf("chk %u %08x\n", (i + 1u) * 256u, fzx_atk_chk[i]);
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
