/* Engine E1 close-up 3D-N test: the rival field (see engine_field.h). */
#include <string.h>
#include "engine_field.h"
#include "sh2zam/line_field_experimental.h"
#include "sh2zam/unit30_experimental.h"

#if E1_FIELD < 1 || E1_FIELD > 29
#error E1_FIELD must be 1..29 when built
#endif

volatile uint32_t fzx_field_k = 1, fzx_field_d = 250, fzx_field_margin = 40;
volatile uint32_t fzx_field_report[16];

#define U(x) ((int32_t)((x) * 256))
enum { MAX_STATIONS = 2048, SPACING_Q12 = 64 << 12, HOLD_FRAMES = 8, NEAR_UNITS = 400 };
typedef struct { int32_t pos[3]; int16_t fwd[3], up[3]; } station_t; /* Q16 position, Q1.14 rows */

static sh2z_line_node_t nodes[MAX_STATIONS];
static station_t stations[MAX_STATIONS];
static sh2z_line_t line;
static const sh2z_vd_track_t *trk;
static sh2z_field_params_t params;
static sh2z_vehicle_t veh[FZX_FIELD_CARS];
static sh2z_field_brain_t brain[FZX_FIELD_CARS];
static uint8_t order[FZX_FIELD_CARS], slot_of[FZX_FIELD_CARS], rank_order[FZX_FIELD_CARS];
static sh2z_field_t field;
static int32_t left_sign = 1, hover = 0;
/* Selection state per car: 3D now, frames the other form has been wanted, 3D slot. */
static uint8_t is3d[FZX_FIELD_CARS], want_run[FZX_FIELD_CARS];

static int16_t q14(int32_t q30) { return (int16_t)((q30 + (1 << 15)) >> 16); }
static int32_t mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
static void cross30(int32_t o[3], const int32_t a[3], const int32_t b[3]) {
    o[0] = (int32_t)(((int64_t)a[1] * b[2] - (int64_t)a[2] * b[1]) >> 30);
    o[1] = (int32_t)(((int64_t)a[2] * b[0] - (int64_t)a[0] * b[2]) >> 30);
    o[2] = (int32_t)(((int64_t)a[0] * b[1] - (int64_t)a[1] * b[0]) >> 30);
}
/* Unit forward f and unit up u (Gram-Schmidt against f), Q30. */
static void frame30(int32_t f[3], int32_t u[3], const int32_t fwd[3], const int32_t up[3]) {
    int32_t t[3];
    if (!sh2z_vec3_normalize_u30(f, fwd)) { f[0] = 0; f[1] = 0; f[2] = 1 << 30; }
    const int32_t d = (int32_t)(((int64_t)up[0] * f[0] + (int64_t)up[1] * f[1] + (int64_t)up[2] * f[2]) >> 30);
    for (int c = 0; c < 3; ++c) t[c] = up[c] - mul30(d, f[c]);
    if (!sh2z_vec3_normalize_u30(u, t)) { u[0] = 0; u[1] = 1 << 30; u[2] = 0; }
}

static bool build_line(const sh2z_vd_track_t *track) {
    uint32_t count = 0;
    int32_t last = -1;
    for (uint32_t n = 0; n < track->count; ++n) {
        const sh2z_vd_node_t *a = &track->nodes[n], *b = &track->nodes[n + 1u < track->count ? n + 1u : 0u];
        const int32_t known[5] = { 0, a->mark_len[0], a->mark_len[1], a->mark_len[2], a->length };
        uint32_t m = (uint32_t)((a->length + SPACING_Q12 - 1) / SPACING_Q12);
        if (!m) m = 1;
        for (uint32_t k = 0; k < m; ++k) {
            const uint32_t tq16 = (uint32_t)(((uint64_t)k << 16) / m);
            const uint32_t piece = tq16 >> 14, frac = tq16 & 0x3fffu;
            const int32_t arc = known[piece] + (int32_t)(((int64_t)(known[piece + 1u] - known[piece]) * frac) >> 14);
            const int32_t q8 = (int32_t)(((uint32_t)(a->start + arc) + 8u) >> 4);
            if (last >= 0 && q8 - last < 2) continue;
            if (count == MAX_STATIONS) return false;
            int32_t pos[3], fwd[3], up[3], f[3], u[3];
            sh2z_vd_spline(track, n, (int32_t)(tq16 << 14), pos, fwd);
            for (int c = 0; c < 3; ++c) up[c] = a->up[c] + (int32_t)(((int64_t)(b->up[c] - a->up[c]) * tq16) >> 16);
            frame30(f, u, fwd, up);
            station_t *s = &stations[count];
            for (int c = 0; c < 3; ++c) { s->pos[c] = pos[c]; s->fwd[c] = q14(f[c]); s->up[c] = q14(u[c]); }
            sh2z_line_node_t *nd = &nodes[count];
            nd->start = q8; nd->lateral = 0; nd->speed_cap = 60u * 16u; nd->flags = 0;
            const int32_t hw = (a->half_left < a->half_right ? a->half_left : a->half_right) >> 16;
            nd->half_width = (uint16_t)(hw > 0 ? hw : 1);
            last = q8;
            ++count;
        }
    }
    const int32_t length = (int32_t)(((uint32_t)track->length + 8u) >> 4);
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t end = i + 1u < count ? nodes[i + 1u].start : length;
        const int32_t span = end - nodes[i].start;
        if (span < 2 || span > 65536) return false;
        nodes[i].inv_span = sh2z_line_node_inv_span(span);
    }
    line.nodes = nodes; line.count = count; line.length = length;
    fzx_field_report[1] = count; fzx_field_report[2] = (uint32_t)length;
    return count >= 2;
}

/* World pose of a line-space state: the station pose interpolated, the lateral (positive
 * right) along -left, the player's hover along up. */
static void line_pose(const sh2z_track_state_t *t, int32_t pos[3], int32_t f[3], int32_t u[3], int32_t l[3]) {
    const uint32_t n = t->node < line.count ? t->node : 0u, m = n + 1u < line.count ? n + 1u : 0u;
    const station_t *a = &stations[n], *b = &stations[m];
    const int32_t d = t->dist - nodes[n].start;
    int32_t fr = (int32_t)(((uint64_t)(uint32_t)(d < 0 ? 0 : d) * nodes[n].inv_span) >> 16);
    if (fr > 65535) fr = 65535;
    int32_t fw[3], up[3];
    for (int c = 0; c < 3; ++c) {
        int32_t bp = b->pos[c];
        pos[c] = a->pos[c] + (int32_t)(((int64_t)(bp - a->pos[c]) * fr) >> 16);
        fw[c] = ((int32_t)a->fwd[c] << 16) + (int32_t)(((int64_t)((int32_t)b->fwd[c] - a->fwd[c]) * fr));
        up[c] = ((int32_t)a->up[c] << 16) + (int32_t)(((int64_t)((int32_t)b->up[c] - a->up[c]) * fr));
    }
    frame30(f, u, fw, up);
    cross30(l, u, f);
    for (int c = 0; c < 3; ++c) {
        l[c] *= left_sign;
        /* lateral Q8 -> Q16 offset along -left; hover Q16 along up */
        pos[c] += -mul30(l[c], t->lateral << 8) + mul30(u[c], hover);
    }
}

static void player_into_field(const sh2z_vd_body_t *p) {
    sh2z_vd_line_view_t lv;
    sh2z_vd_line_view(p, trk, &lv);
    sh2z_vehicle_t *v = &veh[0];
    v->t.dist = lv.dist; v->t.lateral = lv.lateral; v->t.speed = lv.speed < 0 ? 0 : lv.speed;
    v->t.lat_vel = lv.lat_vel; v->t.lap = lv.lap;
    v->t.node = sh2z_line_locate(&line, v->t.dist, v->t.node);
    v->kick_lat = 0; v->kick_speed = 0; /* not applied: the player's race stays as E1's */
    v->energy = v->max_energy;
}

bool fzx_field_load(const sh2z_vd_track_t *track, const sh2z_vd_body_t *player) {
    memset((void *)fzx_field_report, 0, sizeof(fzx_field_report));
    trk = track;
    if (!build_line(track)) { fzx_field_report[0] = 2; return false; }
    /* Left axis sign and hover from the player's own frame at the start. */
    {
        int32_t c[3];
        cross30(c, player->body[1], player->body[0]);
        const int64_t s = (int64_t)c[0] * player->body[2][0] + (int64_t)c[1] * player->body[2][1] + (int64_t)c[2] * player->body[2][2];
        left_sign = s >= 0 ? 1 : -1;
    }
    memset(veh, 0, sizeof(veh)); memset(brain, 0, sizeof(brain));
    veh[0].flags = SH2Z_VEH_EXTERNAL; veh[0].max_energy = veh[0].energy = U(178);
    veh[0].top_speed = U(60); veh[0].grip = U(6); veh[0].accel = 40; veh[0].brake = 96; veh[0].armor = 256;
    player_into_field(player);
    {
        /* Hover: the player's height above the line point under it. */
        sh2z_track_state_t t = veh[0].t; t.lateral = 0;
        int32_t pos[3], f[3], u[3], l[3];
        hover = 0;
        line_pose(&t, pos, f, u, l);
        int64_t h = 0;
        for (int c = 0; c < 3; ++c) h += (int64_t)(player->pos[c] - pos[c]) * u[c];
        hover = (int32_t)(h >> 30);
        fzx_field_report[13] = (uint32_t)left_sign; fzx_field_report[14] = (uint32_t)hover;
    }
    memset(&params, 0, sizeof(params));
    params.pace_gain = 3277; params.pace_max = 81920; params.chase_gain = 655; params.steer_gain = 9830;
    params.margin = U(6); params.avoid_long = U(92); params.avoid_lat = U(69); params.avoid_offset = U(92);
    params.avoid_ticks = 40; params.pass_hold = 60; params.contact_long = U(46); params.contact_lat = U(40);
    params.bump_keep = 16384; params.hit_energy = 32768; params.side_energy = U(2); params.attack_energy = U(10);
    params.wall_energy = U(3); params.wall_keep = 62259; params.attack_long = U(184); params.attack_lat = U(92);
    params.boost_speed = U(6); params.boost_energy = U(10); params.boost_min_share = 19661; params.boost_min_cap = 0;
    params.boost_behind = U(150); params.cruise = U(40); params.rank_lead = U(8);
    params.lod_near[0] = U(3000); params.lod_near[1] = U(8000); params.lod_near[2] = U(16000);
    params.boost_ticks = 90; params.laps = 3; params.boost_from_lap = 1;
    /* Synthetic grid: the 29 rivals in rows of four ahead of the player (as the source's
     * Grand Prix, where the player starts last), target laps spread around the player's. */
    uint32_t rs = 0x9e3779b9u;
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
        sh2z_vehicle_t *v = &veh[i]; sh2z_field_brain_t *b = &brain[i];
        const uint32_t row = (i - 1u) / 4u, col = (i - 1u) % 4u;
        int32_t d = veh[0].t.dist + U(110) + (int32_t)row * U(100);
        int32_t lap = veh[0].t.lap;
        while (d >= line.length) { d -= line.length; ++lap; }
        v->t.dist = d; v->t.lap = lap; v->t.lateral = (int32_t)(3 - 2 * (int32_t)col) * U(25);
        v->t.node = sh2z_line_locate(&line, d, 0);
        v->max_energy = v->energy = U(178); v->accel = 40; v->brake = 96; v->top_speed = U(60); v->grip = U(6); v->armor = 256;
        const int32_t lap_ms = 33400 + (int32_t)((i * 97u) % 29u) * 60; /* 33.4 .. 35.1 s */
        const int32_t lap_ticks = lap_ms * 60 / 1000;
        b->pace = line.length / lap_ticks;
        rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
        b->lane = (int32_t)((int32_t)(rs % 61u) - 30) * 256;
        b->chase = -1; b->chase_lead = 0; b->aggression = 0;
    }
    sh2z_field_init(&field, &line, &params, veh, brain, order, slot_of, rank_order, FZX_FIELD_CARS, 1u);
    field.focus = 0;
    memset(is3d, 0, sizeof(is3d)); memset(want_run, 0, sizeof(want_run));
    fzx_field_report[0] = 1;
    return true;
}

void fzx_field_tick(const sh2z_vd_body_t *player) {
    if (fzx_field_report[0] != 1) return;
    player_into_field(player);
    if (!field.started && player->race_time > 0) { sh2z_field_start(&field); fzx_field_report[4] = field.tick; }
    sh2z_field_step(&field);
    fzx_field_report[3] = field.tick; fzx_field_report[5] = field.contacts;
    fzx_field_report[11] = field.finished; fzx_field_report[12] = field.retired;
}

static uint32_t dist_units(const int32_t a[3], const int32_t b[3]) {
    int64_t s = 0;
    for (int c = 0; c < 3; ++c) { const int64_t d = (int64_t)(a[c] - b[c]) >> 8; s += d * d; } /* Q8 */
    uint64_t v = (uint64_t)s, r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (uint32_t)(r >> 8); /* whole units */
}

unsigned fzx_field_cars(fzx_field_car_t *out, const sh2z_vd_body_t *player) {
    if (fzx_field_report[0] != 1) return 0;
    const uint32_t k = fzx_field_k > FZX_FIELD_K_MAX ? FZX_FIELD_K_MAX : fzx_field_k;
    const uint32_t dmax = fzx_field_d, margin = fzx_field_margin;
    uint32_t dist[FZX_FIELD_CARS], near = 0;
    /* The player. */
    for (int c = 0; c < 3; ++c) {
        out[0].pos[c] = player->pos[c];
        for (int r = 0; r < 3; ++r) out[0].basis[r][c] = q14(player->body[r][c]);
    }
    out[0].id = 0; dist[0] = 0;
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
        int32_t pos[3], f[3], u[3], l[3];
        line_pose(&veh[i].t, pos, f, u, l);
        fzx_field_car_t *o = &out[i];
        for (int c = 0; c < 3; ++c) { o->pos[c] = pos[c]; o->basis[0][c] = q14(f[c]); o->basis[1][c] = q14(u[c]); o->basis[2][c] = q14(l[c]); }
        o->id = (uint16_t)i;
        dist[i] = dist_units(pos, player->pos);
        if (dist[i] < NEAR_UNITS) ++near;
    }
    /* Selection with hysteresis: rivals that want 3D are within D - margin (entering) or
     * D + margin (staying); among them the nearest K - 1 (the player holds the first
     * slot when K >= 1). A change of form needs HOLD_FRAMES frames in a row. */
    uint32_t in3d = 0;
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) in3d += is3d[i];
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
        const uint32_t limit = is3d[i] ? dmax + margin : (dmax > margin ? dmax - margin : 0u);
        bool want = k >= 2 && dist[i] < limit;
        if (want) {
            /* Only the nearest K - 1 rivals may be 3D. */
            uint32_t closer = 0;
            for (uint32_t j = 1; j < FZX_FIELD_CARS; ++j) if (j != i && (dist[j] < dist[i] || (dist[j] == dist[i] && j < i))) ++closer;
            want = closer < k - 1u;
        }
        if (want != (bool)is3d[i]) {
            if (++want_run[i] >= HOLD_FRAMES) {
                if (want && in3d >= k - 1u) continue; /* full: wait for a slot */
                is3d[i] = want; want_run[i] = 0;
                if (want) { ++in3d; ++fzx_field_report[8]; } else { --in3d; ++fzx_field_report[9]; }
            }
        } else want_run[i] = 0;
    }
    if (k < 2) for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) is3d[i] = 0;
    /* 3D slots: the player 0, rivals 1.. in index order. */
    uint32_t next = 1, n3d = 0;
    out[0].is3d = k >= 1; out[0].slot = 0; n3d += out[0].is3d;
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
        out[i].is3d = is3d[i] && next < FZX_FIELD_K_MAX;
        out[i].slot = out[i].is3d ? (uint8_t)next++ : 0u;
        n3d += out[i].is3d;
    }
    fzx_field_report[6] = near; fzx_field_report[7] = n3d;
    if (near > fzx_field_report[10]) fzx_field_report[10] = near;
    fzx_field_report[15] = sh2z_field_hash(&field);
    return FZX_FIELD_CARS;
}
