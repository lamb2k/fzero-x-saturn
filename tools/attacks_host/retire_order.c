/* Retire-order case (attacks lane, claude/attacks; the cup lane's request): two close retirements whose
 * order the classification gets wrong when it takes each retired racer's place at the trigger instead of
 * the place it held when it retired. HOST only. MIT; see ../../LICENSE.
 *
 * The engine's own field and attacks run (as tools/attacks_host/attacks_main.c, R01 driver). At tick T
 * the case takes the first pair of rivals adjacent in race order, under 30 units apart, with the one
 * behind faster, and empties both (fzx_atk_test_damage): both spin out on the same tick, retire on the
 * same tick 150 later (Racer_RetireRacer), and then coast, the faster one past the other. At the
 * player's finish the classification (the cup lane's engine_cup.c, fzx_classify) runs twice: with the
 * retire tick and place the attacks keep (fzx_atk_retire_tick / fzx_atk_retire_place), and with the
 * place at the trigger (the approximation). The case passes when the first order is the original's
 * (raceDistancePosition = frame + (30 - place at retirement) / 10, lower is worse) and the second differs.
 *
 *   fzx-retire-order --phys PHYS.PAK --rivals RIVALS.PAK [--at T]
 * Exit 0 when the case holds; prints both orders of the pair. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_native.h"
#include "engine_field.h"
#include "engine_attacks.h"
#include "engine_cup.h"
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
    if (n != bytes) return false;
    uint8_t *p = dst;
    for (uint32_t i = 0; i < FZX_RIVALS_NODE_COUNT; ++i, p += 16) { swap32(p); swap16(p + 4); swap16(p + 6); swap16(p + 8); swap16(p + 10); swap32(p + 12); }
    for (uint32_t i = 0; i < FZX_RIVALS_NODE_COUNT; ++i, p += 32) { swap32(p); swap32(p + 4); swap32(p + 8); for (unsigned k = 0; k < 10u; ++k) swap16(p + 12 + 2 * k); }
    return true;
}
static void die(const char *m) { fprintf(stderr, "fzx-retire-order: %s\n", m); exit(2); }
static int32_t clock_ms(int32_t ticks) { return ticks * 50 / 3; }
/* The cup lane's feed (claude/e3-cup engine_field.c fzx_field_classify_in) with the retire tick and place the
 * attacks keep: the change this lane hands over. */
static unsigned classify_in(fzx_cl_in_t *out, int32_t player_ms, fzx_cl_key_t *key) {
    const fzx_atk_field_t *fa = fzx_atk_field();
    const sh2z_field_t *field = fa->field;
    const int32_t now = (int32_t)(field->tick - field->start_tick);
    for (unsigned i = 0; i < fa->count; ++i) {
        const sh2z_vehicle_t *v = &fa->veh[i];
        const sh2z_field_brain_t *b = &fa->brain[i];
        fzx_cl_in_t *c = &out[i];
        memset(c, 0, sizeof(*c));
        c->entrant = (uint8_t)i; c->position = (uint8_t)(v->rank + 1u);
        const int laps = field->params->laps;
        c->laps_done = (uint8_t)(b->laps_seen > 0 ? (b->laps_seen < laps ? b->laps_seen : laps) : 0);
        for (unsigned k = 0; k < 3u; ++k) {
            const int32_t end = b->lap_tick[k], from = k ? b->lap_tick[k - 1u] : 0;
            c->lap_ms[k] = (end >= 0 && from >= 0) ? clock_ms(end - from) : 0;
        }
        if (i == 0u) { c->state = FZX_CLIN_FINISHED; c->race_ms = player_ms; }
        else if (v->flags & SH2Z_VEH_FINISHED) { c->state = FZX_CLIN_FINISHED; c->race_ms = clock_ms(b->finish_tick); }
        else if (v->flags & SH2Z_VEH_RETIRED) {
            c->state = FZX_CLIN_RETIRED;
            c->retire_frame = fzx_atk_retire_tick[i] >= 0 ? fzx_atk_retire_tick[i] : b->retire_tick;
            c->retire_position = fzx_atk_retire_place[i] ? fzx_atk_retire_place[i] : c->position;
        } else {
            c->state = FZX_CLIN_RUNNING; c->race_ms = clock_ms(now);
            c->race_distance = (float)((int64_t)v->t.lap * fa->line->length + v->t.dist);
        }
    }
    key->frame = now; key->course_length = (float)fa->line->length; key->laps = (uint8_t)field->params->laps;
    return fa->count;
}
static double q30d(int32_t v) { return v / 1073741824.0; }
static signed char driver_stick(const sh2z_vd_body_t *b) {
    const float lateral = (float)(sh2z_vd_body_lateral(b) / 65536.0);
    const double h = q30d(b->body[0][0]) * q30d(b->road[2][0]) + q30d(b->body[0][1]) * q30d(b->road[2][1]) +
                     q30d(b->body[0][2]) * q30d(b->road[2][2]);
    float steer = 0.004f * lateral + 2.5f * (float)h;
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;
    return (signed char)(steer >= 0.0f ? (int)(steer * 63.0f + 0.5f) : -(int)(-steer * 63.0f + 0.5f));
}

int main(int argc, char **argv) {
    const char *phys = NULL;
    uint32_t at = 1000;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--phys")) phys = argv[i + 1];
        else if (!strcmp(argv[i], "--rivals")) rivals_path = argv[i + 1];
        else if (!strcmp(argv[i], "--at")) at = (uint32_t)atoi(argv[i + 1]);
        else die("unknown argument");
    }
    if (!phys || !rivals_path) die("usage: fzx-retire-order --phys PHYS.PAK --rivals RIVALS.PAK [--at T]");
    FILE *f = fopen(phys, "rb");
    if (!f) die("cannot open the physics pack");
    static uint8_t image[1 << 20];
    const size_t n = fread(image, 1, sizeof(image), f);
    fclose(f);
    static FzxPhysCourse course;
    if (fzx_phys_load(&course, image, n, 1) != 0) die("bad physics pack");
    sh2z_vd_body_t body = course.start;
    static uint32_t scratch[64];
    if (!fzx_field_pack_load(scratch, sizeof(scratch)) || !fzx_field_load(&course.track, &body)) die("field load");
    fzx_atk_vis_gate = 0; fzx_atk_ai = 0;   /* the case alone: no CPU attacks, everyone KO-able */
    int a = -1, b = -1;
    uint16_t prev = 0;
    uint32_t t = 0, finish_tick = 0;
    for (; t < 12000u; ++t) {
        const uint16_t button = t >= 460u ? 0x8000u : 0u;
        sh2z_vd_input_t in;
        in.held = fzx_phys_buttons_n64(button);
        in.pressed = fzx_phys_buttons_n64((uint16_t)((prev ^ button) & button));
        in.released = fzx_phys_buttons_n64((uint16_t)((prev ^ button) & prev));
        in.stick_x = body.intro == 0 ? driver_stick(&body) : 0; in.stick_y = 0;
        prev = button;
        if (t == at) {
            /* The pair: adjacent in race order, under 30 apart, the one behind faster. */
            const fzx_atk_field_t *fa = fzx_atk_field();
            const sh2z_vehicle_t *v = fa->veh;
            const uint8_t *ro = fa->field->rank_order;
            for (unsigned p = 1; p + 1 < FZX_FIELD_CARS && a < 0; ++p) {
                const int i = ro[p], j = ro[p + 1];
                if (i == 0 || j == 0) continue;
                const int32_t gap = (v[i].t.lap * fa->line->length + v[i].t.dist) - (v[j].t.lap * fa->line->length + v[j].t.dist);
                if (gap > 0 && gap < 30 * 256 && v[j].t.speed > v[i].t.speed + 64) { a = i; b = j; }
            }
            if (a < 0) die("no pair at this tick; try another --at");
            fzx_atk_test_damage(&body, (uint32_t)a, 1000 << 16);
            fzx_atk_test_damage(&body, (uint32_t)b, 1000 << 16);
            printf("pair at tick %u: %d (place %u) ahead of %d (place %u)\n", t, a, v[a].rank + 1u, b, v[b].rank + 1u);
        }
        sh2z_vd_contact_t contact;
        fzx_atk_contact(&body, &contact);
        sh2z_vd_step_contact(&body, &course.track, &course.vehicle, &fzx_phys_rules, &in, &contact);
        fzx_field_tick(&body);
        if ((body.flags & SH2Z_VD_FINISHED) && !finish_tick) { finish_tick = t; break; }
    }
    if (a < 0 || !finish_tick) die("the case did not run to the finish");
    if (fzx_atk_retire_tick[a] < 0 || fzx_atk_retire_tick[b] < 0) die("the pair did not retire");
    /* Classification inputs at the trigger: the field's rows, retired ones two ways. */
    fzx_cl_in_t in_exact[FZX_ENTRANTS_MAX], in_approx[FZX_ENTRANTS_MAX];
    fzx_cl_key_t key;
    const unsigned rows = classify_in(in_exact, body.race_time, &key);
    memcpy(in_approx, in_exact, sizeof(in_exact));
    for (unsigned i = 0; i < rows; ++i) {
        if (in_exact[i].state != FZX_CLIN_RETIRED) continue;
        in_approx[i].retire_position = in_approx[i].position;           /* the place at the trigger */
    }
    key.run_id = 1; key.outcome = FZX_OUT_FINISH;
    fzx_classification_t ce, ca;
    if (!fzx_classify(&ce, &key, in_exact, rows) || !fzx_classify(&ca, &key, in_approx, rows)) die("classify");
    int pe_a = -1, pe_b = -1, pa_a = -1, pa_b = -1;
    for (unsigned p = 0; p < rows; ++p) {
        if (ce.row[p].entrant == a) pe_a = (int)p + 1;
        if (ce.row[p].entrant == b) pe_b = (int)p + 1;
        if (ca.row[p].entrant == a) pa_a = (int)p + 1;
        if (ca.row[p].entrant == b) pa_b = (int)p + 1;
    }
    /* The original's order: raceDistancePosition at retirement, lower is worse. */
    const double rdp_a = fzx_atk_retire_tick[a] + (30 - fzx_atk_retire_place[a]) / 10.0;
    const double rdp_b = fzx_atk_retire_tick[b] + (30 - fzx_atk_retire_place[b]) / 10.0;
    const int source_a_above = rdp_a > rdp_b;
    printf("retired: %d tick %d place %u (place at the trigger %u); %d tick %d place %u (at the trigger %u)\n",
           a, fzx_atk_retire_tick[a], fzx_atk_retire_place[a], in_exact[a].position, b, fzx_atk_retire_tick[b],
           fzx_atk_retire_place[b], in_exact[b].position);
    printf("classification with the retire place: %d at %d, %d at %d; with the trigger place: %d at %d, %d at %d; source: %d above\n",
           a, pe_a, b, pe_b, a, pa_a, b, pa_b, source_a_above ? a : b);
    const int exact_ok = (pe_a < pe_b) == source_a_above;
    const int flips = (pa_a < pa_b) != (pe_a < pe_b);
    printf("exact order %s; approximation %s\n", exact_ok ? "matches the source" : "DIFFERS from the source",
           flips ? "flips the pair (the case holds)" : "keeps the pair (no case here)");
    return exact_ok && flips ? 0 : 1;
}
