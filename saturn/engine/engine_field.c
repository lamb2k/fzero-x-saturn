/* Engine E1 close-up 3D-N test: the rival field (see engine_field.h). */
#include <string.h>
#include "engine_field.h"
#include "engine_atk_prof.h"
#include "sh2zam/line_field_experimental.h"
#include "sh2zam/unit30_experimental.h"

#if E1_FIELD < 1 || E1_FIELD > 29
#error E1_FIELD must be 1..29 when built
#endif
#ifndef ENG_ATTACKS
#define ENG_ATTACKS 0
#endif
#if ENG_ATTACKS
#include "engine_attacks.h"
#if !E2_PACK || !E2_FASTPOSE
#error ENG_ATTACKS needs E2_PACK=1 and E2_FASTPOSE=1
#endif
#endif
#ifndef E2_PACK
#define E2_PACK 0
#endif
#ifndef E2_FASTPOSE
#define E2_FASTPOSE 0
#endif
#ifndef E2_LOD
#define E2_LOD 0
#endif
#if E2_PACK
/* The E2 rival line and field (generated/rivals, local only): RIVALS.PAK's nodes replace the
 * built line's payload (CPU line laterals, speed caps, widths); the reference GP's config
 * replaces the synthetic grid. */
#include "rivals_course.h"
#include "rivals_field.h"
#include "course_select.h"
#include "texture_cd.h"
#if FZX_RIVALS_FIELD_COUNT < FZX_FIELD_CARS
#error the reference field has fewer vehicles than E1_FIELD + 1
#endif
#endif
#ifndef ENG_RIVAL_CLASS
#define ENG_RIVAL_CLASS 0
#endif
#if ENG_RIVAL_CLASS && !E2_PACK
#error ENG_RIVAL_CLASS needs E2_PACK=1
#endif
#if ENG_RIVAL_CLASS
/* line-field-5 (d): the field's rows for the class raced (C-097: cpu.c's target tables are course * 4 + gDifficulty).
 * One row set per class, from the reference GP at that class (tools/rivals_field_header.py --classes), grid column
 * included (Master's tier grid moves places). Scenes images race fzx_session.difficulty; other images
 * fzx_field_class (0 novice .. 3 master), read at fzx_field_load. */
#if defined(ENG_COURSE_SELECT) && ENG_COURSE_SELECT
#include "course_class.h"
#define FIELD_ROWS (fzx_course_field_class[fzx_cid_index][field_class_now])
#else
#include "rivals_field_class.h"
#if FZX_RIVALS_FIELD_CLASS_SEED != FZX_RIVALS_FIELD_SEED
#error the class rows come from another seed than rivals_field.h
#endif
#define FIELD_ROWS (fzx_rivals_field_class[field_class_now])
#endif
#if defined(ENG_SCENES) && ENG_SCENES
#include "engine_scenes.h"
extern fzx_session_t fzx_session;
#endif
volatile uint32_t fzx_field_class = 1u;
static uint32_t field_class_now = 1u;
static void field_class_pick(void) {
#if defined(ENG_SCENES) && ENG_SCENES
    field_class_now = fzx_session.difficulty & 3u;
#else
    field_class_now = fzx_field_class & 3u;
#endif
}
#else
#define FIELD_ROWS CSEL(field,fzx_rivals_field)
#endif
#ifndef ENG_RIVAL_SURFACE
#define ENG_RIVAL_SURFACE 0
#endif
#if E2_PACK && ENG_RIVAL_SURFACE
/* line-field-4 (COURSES.md section 10): the course's zones, its pipes and cylinders as tubes, and
 * the final dash, as the HOST bands were checked with (ref/rivals_gp_main.c). */
#include "rivals_zones.h"
#include "sh2zam/trig12_experimental.h"
#define RIVAL_TUBES 1
static sh2z_field_zone_t zones[FZX_RIVALS_ZONE_MAX];
static sh2z_field_surface_t surface;
#else
#define RIVAL_TUBES 0
#endif
#ifndef ENG_RIVAL_BEHAVE
#define ENG_RIVAL_BEHAVE 0
#endif
#if ENG_RIVAL_BEHAVE && !RIVAL_TUBES
#error ENG_RIVAL_BEHAVE needs E2_PACK=1 and ENG_RIVAL_SURFACE=1
#endif
#if ENG_RIVAL_BEHAVE
/* line-field-5: the behaviours' tuning, and the fall / retirement / KO-credit events for the attacks glue. */
static sh2z_field_behave_t behave;
#if defined(ENG_COURSE_SELECT) && ENG_COURSE_SELECT
#include "course_drops.h"
#define DROP_COUNT ((uint32_t)fzx_course_drop_count[fzx_cid_index])
#define DROP_ROWS (fzx_course_drops[fzx_cid_index])
#elif __has_include("rivals_drops.h")
#include "rivals_drops.h"
#define DROP_COUNT FZX_RIVALS_DROP_COUNT
#define DROP_ROWS fzx_rivals_drops
#else
#define DROP_COUNT 0u
static const uint16_t DROP_ROWS[1][2] = { { 0, 0 } };
#endif
static uint16_t prev_flags[FZX_FIELD_CARS];
static int16_t prev_credit[FZX_FIELD_CARS];
fzx_field_event_t fzx_field_events[FZX_FIELD_EVENTS];
volatile uint32_t fzx_field_event_count;
static void field_event(uint32_t agent, uint32_t kind, int32_t by, int32_t tick) {
    const uint32_t n = fzx_field_event_count;
    if (n < FZX_FIELD_EVENTS) {
        fzx_field_event_t *e = &fzx_field_events[n];
        e->agent = (uint8_t)agent; e->kind = (uint8_t)kind; e->by = (int16_t)by; e->tick = tick;
    }
    fzx_field_event_count = n + 1u; /* counts past the table too: a full table shows as count > FZX_FIELD_EVENTS */
}
#endif

volatile uint32_t fzx_field_k = 1, fzx_field_d = 250, fzx_field_margin = 40;
volatile uint32_t fzx_field_report[16];

#define U(x) ((int32_t)((x) * 256))
enum { MAX_STATIONS = 2048, SPACING_Q12 = 64 << 12, HOLD_FRAMES = 8, NEAR_UNITS = 400 };
#if E2_FASTPOSE
/* Q16 position, Q1.14 rows forward, up and left (left = sign * up x forward, set at load). */
typedef struct { int32_t pos[3]; int16_t fwd[3], up[3], left[3]; } station_t;
#else
typedef struct { int32_t pos[3]; int16_t fwd[3], up[3]; } station_t; /* Q16 position, Q1.14 rows */
#endif

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
/* Scaling of signed values by a power of two (C-127 finding 3). ISO C leaves the left shift of a negative value
 * undefined, and the lateral, the packed positions and the Q1.14 rows are negative on half the road. The shift is
 * done on the unsigned bits: the two's-complement scaling the SH-2 build has always computed, in the same
 * instruction. Where the scaled value can leave int32 the range is checked first: q8_q16 saturates, and the load
 * checks the pack's coordinates, its half widths and the spans between stations (stations_ok). */
static inline int32_t shl_s32(int32_t v, unsigned n) { return (int32_t)((uint32_t)v << n); }
static inline int32_t q8_q16(int32_t q8) { return q8 > 0x7fffff ? 0x7fffff00 : q8 < -0x800000 ? INT32_MIN : shl_s32(q8, 8); }
static inline int32_t wq16(uint16_t whole) { return shl_s32((int32_t)whole, 16); } /* whole units -> Q16 (< 0x8000) */
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

#if RIVAL_TUBES
/* A tube node (the pack's shape code: pipe 0x80, cylinder 0xC0): the lateral is an arc round a circle of
 * radius half_width about the line, which is the tube's axis. Returns 1 pipe, 2 cylinder, 0 road, and the
 * angle's sine and cosine (Q30). Lateral positive right turns toward -left. */
static int tube_at(uint32_t n, int32_t lateral, int32_t *s, int32_t *c) {
    const uint16_t shape = (uint16_t)(nodes[n].flags & 0x01c0u);
    if ((shape != 0x0080u && shape != 0x00c0u) || !nodes[n].half_width) return 0;
    const int32_t a12 = (int32_t)(((int64_t)lateral * 10430) / ((int64_t)nodes[n].half_width << 12)); /* 4096 / 2 pi */
    sh2z_sincos12_u30((uint32_t)a12, s, c);
    return shape == 0x0080u ? 1 : 2;
}
#endif

/* The pose arithmetic's range (C-127 finding 3): adjacent stations (the last with station 0) differ by less than 2^31 in
 * every Q16 coordinate, so the interpolation's subtraction cannot overflow, and no node is wider than 0x7fff units, so
 * its Q16 width and the tube radius fit int32. The built line is far inside both; the pack's is checked here too. */
static bool stations_ok(void) {
    for (uint32_t i = 0; i < line.count; ++i) {
        const station_t *a = &stations[i], *b = &stations[i + 1u < line.count ? i + 1u : 0u];
        if (nodes[i].half_width > 0x7fffu) return false;
        for (int c = 0; c < 3; ++c) {
            const int64_t d = (int64_t)b->pos[c] - a->pos[c];
            if (d > INT32_MAX || d < INT32_MIN) return false;
        }
    }
    return true;
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
        fw[c] = shl_s32(a->fwd[c], 16) + (int32_t)(((int64_t)((int32_t)b->fwd[c] - a->fwd[c]) * fr));
        up[c] = shl_s32(a->up[c], 16) + (int32_t)(((int64_t)((int32_t)b->up[c] - a->up[c]) * fr));
    }
    frame30(f, u, fw, up);
    cross30(l, u, f);
    for (int c = 0; c < 3; ++c) l[c] *= left_sign;
#if RIVAL_TUBES
    int32_t sn, cs;
    const int tube = tube_at(n, t->lateral, &sn, &cs);
    if (tube) {
        /* Round the tube: a cylinder's car sits outside, up away from the axis; a pipe's inside, up
         * toward it (at lateral 0 on top and at the bottom). The frame turns about forward. */
        const int32_t r = wq16(nodes[n].half_width);
        for (int c = 0; c < 3; ++c) {
            const int32_t uc = u[c], lc = l[c];
            if (tube == 2) { u[c] = mul30(uc, cs) - mul30(lc, sn); l[c] = mul30(lc, cs) + mul30(uc, sn); }
            else { u[c] = mul30(uc, cs) + mul30(lc, sn); l[c] = mul30(lc, cs) - mul30(uc, sn); }
            pos[c] += tube == 2 ? mul30(u[c], r + hover) : -mul30(u[c], r - hover);
        }
        return;
    }
#endif
    for (int c = 0; c < 3; ++c) {
        /* lateral Q8 -> Q16 offset along -left; hover Q16 along up */
        pos[c] += -mul30(l[c], q8_q16(t->lateral)) + mul30(u[c], hover);
    }
}

#if E2_FASTPOSE
/* line_pose without renormalising: the station rows interpolated in Q1.14 (stations are
 * 64 units apart, so the rows stay unit length to well under 0.1%). Render only. */
static void line_pose_fast(const sh2z_track_state_t *t, int32_t pos[3], int16_t rows[3][3]) {
    const uint32_t n = t->node < line.count ? t->node : 0u, m = n + 1u < line.count ? n + 1u : 0u;
    const station_t *a = &stations[n], *b = &stations[m];
    const int32_t d = t->dist - nodes[n].start;
    int32_t fr = (int32_t)(((uint64_t)(uint32_t)(d < 0 ? 0 : d) * nodes[n].inv_span) >> 16);
    if (fr > 65535) fr = 65535;
    const int64_t lat = (int64_t)t->lateral * 256; /* Q16 */
#if RIVAL_TUBES
    int32_t sn, cs;
    const int tube = tube_at(n, t->lateral, &sn, &cs);
    if (tube) {   /* as line_pose, in Q1.14 */
        const int32_t r = wq16(nodes[n].half_width), s14 = sn >> 16, c14 = cs >> 16;
        for (int c = 0; c < 3; ++c) {
            const int32_t f = a->fwd[c] + (int32_t)((((int32_t)b->fwd[c] - a->fwd[c]) * fr) >> 16);
            const int32_t u = a->up[c] + (int32_t)((((int32_t)b->up[c] - a->up[c]) * fr) >> 16);
            const int32_t l = a->left[c] + (int32_t)((((int32_t)b->left[c] - a->left[c]) * fr) >> 16);
            const int32_t nu = tube == 2 ? (u * c14 - l * s14) >> 14 : (u * c14 + l * s14) >> 14;
            const int32_t nl = tube == 2 ? (l * c14 + u * s14) >> 14 : (l * c14 - u * s14) >> 14;
            rows[0][c] = (int16_t)f; rows[1][c] = (int16_t)nu; rows[2][c] = (int16_t)nl;
            pos[c] = a->pos[c] + (int32_t)(((int64_t)(b->pos[c] - a->pos[c]) * fr) >> 16)
                   + (tube == 2 ? (int32_t)(((int64_t)nu * (r + hover)) >> 14) : -(int32_t)(((int64_t)nu * (r - hover)) >> 14));
        }
        return;
    }
#endif
    for (int c = 0; c < 3; ++c) {
        const int32_t f = a->fwd[c] + (int32_t)((((int32_t)b->fwd[c] - a->fwd[c]) * fr) >> 16);
        const int32_t u = a->up[c] + (int32_t)((((int32_t)b->up[c] - a->up[c]) * fr) >> 16);
        const int32_t l = a->left[c] + (int32_t)((((int32_t)b->left[c] - a->left[c]) * fr) >> 16);
        rows[0][c] = (int16_t)f; rows[1][c] = (int16_t)u; rows[2][c] = (int16_t)l;
        pos[c] = a->pos[c] + (int32_t)(((int64_t)(b->pos[c] - a->pos[c]) * fr) >> 16)
               - (int32_t)((l * lat) >> 14) + (int32_t)(((int64_t)u * hover) >> 14);
    }
}
#endif

#if E2_PACK
/* The field counts laps as the source's race distance does: behind the start line on the
 * grid is lap -1. E1's line view reports lap 0 there (laps_done - 1, clamped), so until the
 * player first crosses the line from the back half of the lap, its lap is -1. */
static uint8_t player_crossed;
static int32_t player_last_dist = -1;
#endif
#if ENG_ATTACKS
/* The player's distance along the line and lateral as player_into_field sets them (contact deferral). */
void fzx_field_player_line(const sh2z_vd_body_t *p, int32_t *dist, int32_t *lateral) {
    sh2z_vd_line_view_t lv;
    sh2z_vd_line_view(p, trk, &lv);
    *dist = lv.dist; *lateral = lv.lateral;
}
#endif
static void player_into_field(const sh2z_vd_body_t *p) {
    sh2z_vd_line_view_t lv;
    sh2z_vd_line_view(p, trk, &lv);
    sh2z_vehicle_t *v = &veh[0];
    v->t.dist = lv.dist; v->t.lateral = lv.lateral; v->t.speed = lv.speed < 0 ? 0 : lv.speed;
    v->t.lat_vel = lv.lat_vel; v->t.lap = lv.lap;
#if E2_PACK
    if (!player_crossed) {
        if (player_last_dist > line.length / 2 && lv.dist < line.length / 2) player_crossed = 1;
        else if (lv.dist > line.length / 2 && lv.lap == 0) v->t.lap = -1;
        player_last_dist = lv.dist;
    }
#endif
    v->t.node = sh2z_line_locate(&line, v->t.dist, v->t.node);
    v->kick_lat = 0; v->kick_speed = 0; /* not applied: the player's race stays as E1's */
#if ENG_ATTACKS
    v->energy = p->energy >> 8;         /* contact reaches the player through its own step (engine_attacks.c) */
#else
    v->energy = v->max_energy;
#endif
}

volatile uint32_t fzx_e2_report[16];
#if E2_PACK
static uint32_t pack_buf[(CMAX(FZX_COURSE_MAX_RIVALS_BYTES,FZX_RIVALS_PACK_BYTES) + 7u) / 4u] __attribute__((aligned(8)));
static bool pack_ok;
bool fzx_field_pack_load(void *scratch, uint32_t scratch_bytes) {
    pack_ok = fzx_texture_cd_load(CNAME(FZX_CF_RIVALS,"RIVALS.PAK"), CSEL(rivals_bytes,FZX_RIVALS_PACK_BYTES), CSEL(rivals_hash,FZX_RIVALS_PACK_HASH), pack_buf, scratch, scratch_bytes);
    fzx_e2_report[0] = pack_ok ? 1u : 2u;
    return pack_ok;
}
#endif

#if E2_PACK && defined(ENG_SCENES) && ENG_SCENES
volatile uint8_t fzx_field_grid[30];
volatile uint32_t fzx_field_grid_src;
/* Grid places (cpu.c, the race set-up): round one of a cup, and every other mode, uses the tier grid
 * with the player last (the course header, from the reference's own set-up). From round two,
 * position = N - D_80115D50[i] + 1, where D_80115D50 holds the last finish (menus.c, NEXT):
 * the previous finish reversed, the player included. The cup's grid[] is that finish (entrant
 * by place, set at NEXT), so a retry of a round starts from the same grid. */
static void grid_places(void) {
    for (uint32_t i = 0; i < FZX_FIELD_CARS; ++i) fzx_field_grid[i] = (uint8_t)FIELD_ROWS[i][0];
    fzx_field_grid_src = 1;
    const fzx_cup_t *c = fzx_cup();
    if ((c->state != FZX_CUP_READY && c->state != FZX_CUP_RACING) || c->round == 0u || c->entrants.count != FZX_FIELD_CARS) return;
    uint8_t place[FZX_FIELD_CARS];
    uint32_t seen = 0;
    for (uint32_t p = 0; p < FZX_FIELD_CARS; ++p) {
        const uint8_t e = c->grid[p];
        if (e >= FZX_FIELD_CARS || (seen & (1u << e))) return; /* not a whole finish: the tier grid, as cpu.c */
        seen |= 1u << e;
        place[e] = (uint8_t)(FZX_FIELD_CARS - p);
    }
    for (uint32_t i = 0; i < FZX_FIELD_CARS; ++i) fzx_field_grid[i] = place[i];
    fzx_field_grid_src = 2;
}
#endif

bool fzx_field_load(const sh2z_vd_track_t *track, const sh2z_vd_body_t *player) {
    memset((void *)fzx_field_report, 0, sizeof(fzx_field_report));
    trk = track;
    if (!build_line(track)) { fzx_field_report[0] = 2; return false; }
#if ENG_RIVAL_CLASS
    {
        /* [12] the class raced, [13] a sum over its rows' pace columns (target lap ms, finish ms, chase cap) */
        field_class_pick();
        uint32_t sum = 0;
        for (uint32_t i = 0; i < FZX_FIELD_CARS; ++i) sum = sum * 31u + (uint32_t)FIELD_ROWS[i][1] + (uint32_t)FIELD_ROWS[i][6] * 7u + (uint32_t)FIELD_ROWS[i][7];
        fzx_e2_report[12] = field_class_now; fzx_e2_report[13] = sum;
    }
#endif
#if E2_PACK
    /* RIVALS.PAK is the HOST bake on course-line-1: the same station count and line length as
     * the line built from the physics course (whose spans' arc lengths are approximate, so the
     * built starts drift by up to ~15 units). The pack's nodes (CPU line payload) and its station
     * poses (source units Q4 -> the physics' Q16: << 12; rows as baked) replace the built ones, so
     * the field runs on exactly the line the HOST bands were checked on. Station 0 must agree with
     * the built one (same point, distance 0) to within one unit. */
    if (!pack_ok || CSEL(rivals_nodes,FZX_RIVALS_NODE_COUNT) != line.count || (int32_t)CSEL(rivals_length,FZX_RIVALS_LINE_LENGTH) != line.length) {
        fzx_field_report[0] = 4; fzx_e2_report[0] = 3; return false;
    }
    {
        typedef struct { int32_t pos[3]; int16_t fwd[3], up[3], left[3]; uint16_t pad; } pack_pose_t;
        const sh2z_line_node_t *pn = (const sh2z_line_node_t *)(const void *)pack_buf;
        const pack_pose_t *pp = (const pack_pose_t *)(const void *)((const uint8_t *)(const void *)pack_buf + 16u * line.count);
        uint32_t worst = 0, off0 = 0;
        /* The pack's positions are source Q4 in int32 and scale to Q16 (x 4096): refuse one the scaling would wrap
         * (|Q4| >= 2^19, 32768 units), as the load refuses a start that disagrees (C-127 finding 3). */
        for (uint32_t i = 0; i < line.count; ++i)
            for (int c = 0; c < 3; ++c)
                if ((uint32_t)pp[i].pos[c] + 0x80000u >= 0x100000u) { fzx_field_report[0] = 5; fzx_e2_report[0] = 4; return false; }
        for (int c = 0; c < 3; ++c) {
            const int64_t dd = (int64_t)stations[0].pos[c] - shl_s32(pp[0].pos[c], 12);
            const uint32_t ad = (uint32_t)(dd < 0 ? -dd : dd);
            if (ad > off0) off0 = ad;
        }
        for (uint32_t i = 0; i < line.count; ++i) {
            const int32_t dd = pn[i].start - nodes[i].start;
            const uint32_t ad = dd < 0 ? (uint32_t)-dd : (uint32_t)dd;
            if (ad > worst) worst = ad;
            nodes[i] = pn[i];
            station_t *st = &stations[i];
            for (int c = 0; c < 3; ++c) { st->pos[c] = shl_s32(pp[i].pos[c], 12); st->fwd[c] = pp[i].fwd[c]; st->up[c] = pp[i].up[c]; }
        }
        fzx_e2_report[1] = worst; fzx_e2_report[9] = off0;
#if ENG_RIVAL_BEHAVE
        /* line-field-5 drops: node flag 0x0200 on the course's drop ranges (tools/rivals_drops.py). */
        for (uint32_t r = 0; r < DROP_COUNT; ++r)
            for (uint32_t k = DROP_ROWS[r][0]; k <= DROP_ROWS[r][1] && k < line.count; ++k) nodes[k].flags |= 0x0200u;
#endif
        if (off0 > 65536u || nodes[0].start != 0) { fzx_field_report[0] = 5; fzx_e2_report[0] = 4; return false; }
    }
#endif
    if (!stations_ok()) { fzx_field_report[0] = 6; return false; } /* 6: a station span or width out of range */
    /* Left axis sign and hover from the player's own frame at the start. */
    {
        int32_t c[3];
        cross30(c, player->body[1], player->body[0]);
        const int64_t s = (int64_t)c[0] * player->body[2][0] + (int64_t)c[1] * player->body[2][1] + (int64_t)c[2] * player->body[2][2];
        left_sign = s >= 0 ? 1 : -1;
    }
#if E2_FASTPOSE
    for (uint32_t i = 0; i < line.count; ++i) {
        station_t *st = &stations[i];
        for (int c = 0; c < 3; ++c) {
            const int a = (c + 1) % 3, b = (c + 2) % 3;
            const int32_t x = ((int32_t)st->up[a] * st->fwd[b] - (int32_t)st->up[b] * st->fwd[a]) >> 14;
            st->left[c] = (int16_t)(left_sign * x);
        }
    }
#endif
    memset(veh, 0, sizeof(veh)); memset(brain, 0, sizeof(brain));
#if E2_PACK
    player_crossed = 0; player_last_dist = -1;
#endif
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
    /* avoid_ticks 0: the source avoids only while the vehicle ahead is within 92 (cpu.c, distanceToRacerAhead < 92),
     * with no look-ahead for closing speed (field contact frequency, C-108). */
    params.avoid_ticks = 0; params.pass_hold = 60; params.contact_long = U(46); params.contact_lat = U(40);
    params.bump_keep = 16384; params.hit_energy = 32768; params.side_energy = U(2); params.attack_energy = U(10);
    /* wall_keep 0.998 a tick on the edge: the source wall routine (racer.c ~2880-2936) scales velocity by 0.998 a tick
     * of contact and charges only the impact, not 5% a tick. */
    params.wall_energy = U(3); params.wall_keep = 65405; params.attack_long = U(184); params.attack_lat = U(92);
    params.boost_speed = U(6); params.boost_energy = U(10); params.boost_min_share = 19661; params.boost_min_cap = 0;
    params.boost_behind = U(150); params.cruise = U(40); params.rank_lead = U(8);
    params.lod_near[0] = U(3000); params.lod_near[1] = U(8000); params.lod_near[2] = U(16000);
    params.boost_ticks = 90; params.laps = 3; params.boost_from_lap = 1;
#if E2_PACK
    /* The reference GP's field (as ref/rivals_gp_main.c sets it up, same generator order): the
     * source grid (146 back, 100 per place, 4 columns of 100), shifted so the player's own
     * place is where E1's physics puts the player. */
    {
        uint32_t rs = CSEL(field_seed,FZX_RIVALS_FIELD_SEED) * 2654435761u + 12345u;
#define RND() (rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5, rs)
#if defined(ENG_SCENES) && ENG_SCENES
        /* The grid is anchored on the player, as for round one: the pack's start body stays where it
         * is and the rivals take their places round the player's place in this race. */
        grid_places();
#define GRID_PLACE(i, cf) ((uint32_t)fzx_field_grid[i])
#else
#define GRID_PLACE(i, cf) ((uint32_t)(cf)[0])
#endif
        sh2z_track_state_t ps;
        sh2z_field_grid_slot(&line, GRID_PLACE(0, FIELD_ROWS[0]) - 1u, U(146), U(100), U(50), 4, &ps);
        const int64_t shift = ((int64_t)veh[0].t.lap * line.length + veh[0].t.dist) - ((int64_t)ps.lap * line.length + ps.dist);
        for (uint32_t i = 0; i < FZX_FIELD_CARS; ++i) {
            const int32_t *cf = FIELD_ROWS[i];
            const int32_t ms = cf[1], chase = cf[2], maxe = cf[3], aggr = cf[4];
            sh2z_track_state_t slot;
            sh2z_field_grid_slot(&line, GRID_PLACE(i, cf) - 1u, U(146), U(100), U(50), 4, &slot);
            const int32_t lap_ticks = (ms > 0 ? ms : 36000) * 60 / 1000;
            int32_t pace = line.length / lap_ticks;
            pace += (int32_t)((int64_t)pace * ((int32_t)(RND() % 81u) - 40) / 10000);
            const int32_t lane = ((int32_t)(RND() % 61u) - 30) * 256;
            const int32_t chase_lead = chase == 0 ? (int32_t)(RND() % 400u) - 10 : 30;
            if (i == 0) continue; /* the player: external, set above (its draws keep the order) */
            sh2z_vehicle_t *v = &veh[i]; sh2z_field_brain_t *b = &brain[i];
            int64_t tot = (int64_t)slot.lap * line.length + slot.dist + shift;
            int32_t lap = 0;
            while (tot < 0) { tot += line.length; --lap; }
            while (tot >= line.length) { tot -= line.length; ++lap; }
            v->t.dist = (int32_t)tot; v->t.lap = lap; v->t.lateral = slot.lateral;
            v->t.node = sh2z_line_locate(&line, v->t.dist, 0);
            v->max_energy = v->energy = maxe;
            v->accel = 40; v->brake = 96; v->top_speed = U(60); v->grip = U(6);
            v->armor = (uint16_t)((int64_t)256 * 178 * 256 / (maxe > 256 ? maxe : U(178)));
            v->flags = 0;
            b->pace = pace; b->lane = lane;
            b->chase = (int16_t)(chase == 255 ? -1 : chase);
            b->chase_lead = (int16_t)chase_lead;
            b->aggression = (uint8_t)aggr;
            b->finish_ticks = cf[6] > 0 ? cf[6] * 60 / 1000 : 0; /* line-field-2: finish target from lap 2 (0 none) */
            b->first_lap_cap = cf[7] > 0 ? cf[7] : 0;            /* line-field-3: a chaser's first-lap cap (0 none) */
        }
#undef RND
#undef GRID_PLACE
    }
#if E2_LOD
    {
        static const int32_t lod[3] = { E2_LOD_NEAR };
        params.lod_near[0] = U(lod[0]); params.lod_near[1] = U(lod[1]); params.lod_near[2] = U(lod[2]);
    }
#endif
#if ENG_RIVAL_SURFACE
    {
        /* line-field-4: the values ref/rivals_gp_main.c checks the bands with. */
        const uint32_t nz = (uint32_t)CSEL(zone_count,FZX_RIVALS_ZONE_COUNT);
        memset(&surface, 0, sizeof(surface));
        for (uint32_t k = 0; k < nz && k < FZX_RIVALS_ZONE_MAX; ++k) {
            const int32_t *z = CSEL(zones,fzx_rivals_zones)[k];
            zones[k].start = z[0]; zones[k].end = z[1]; zones[k].lat_lo = z[2]; zones[k].lat_hi = z[3]; zones[k].kind = (uint32_t)z[4];
        }
        surface.zones = zones; surface.zone_count = nz < FZX_RIVALS_ZONE_MAX ? nz : FZX_RIVALS_ZONE_MAX;
        surface.plate_ticks = 60; surface.slip_steer = 16384; surface.rough_keep = 58982; surface.recharge = 512;
        surface.wrap_mask = 0x0180u; surface.wrap_value = 0x0080u; /* pipe 0x80 and cylinder 0xC0 */
        params.surface = &surface;
        params.dash_from = (int32_t)CSEL(dash_from,FZX_RIVALS_DASH_FROM); params.dash_pace = 72090; /* 1.10 */
        fzx_e2_report[10] = surface.zone_count; fzx_e2_report[11] = (uint32_t)params.dash_from;
    }
#endif
#if ENG_RIVAL_BEHAVE
    {
        /* line-field-5: the values HOST checked the bands and the C-095 tube judge with (ref/rivals_gp_main.c
         * --behave 1 --tune boost_energy=0): the source's tube bias pick (1/64 a decision; half hold to an exit,
         * the rest 20..35 decisions), cylinders hold their angle, pipes settle near their line; air nodes (shape
         * 0x180), open-edge road (0x000), fall to retirement 95 ticks, KO window 120; drops; free CPU boost. */
        memset(&behave, 0, sizeof(behave));
        behave.roll_chance = 1024; behave.roll_latch = 32768; behave.roll_min = 20; behave.roll_span = 16;
        behave.roll_lead = 26214; behave.hold_lead = 7864; behave.roll_well = 52429;
        behave.hold_mask = 0x01c0u; behave.hold_value = 0x00c0u; behave.exit_mask = 0x01c0u; behave.exit_value = 0x0080u;
        behave.roll_exit_ahead = U(3000);
        behave.lat_accel = 192;
        behave.air_mask = 0x01c0u; behave.air_value = 0x0180u; behave.open_mask = 0x01c0u; behave.open_value = 0x0000u;
        behave.air_spread = 2600; behave.fall_margin = U(30); behave.fall_ticks = 95;
        behave.ko_window = 120; behave.ko_near = U(1500); behave.ko_floor = 26;
        /* Drops (HOST tuning seeds 1-6 and 101-106, judged on 201-206): launches out of a pipe on a drop node 1 in 16;
         * beyond 1.8 of the radius round a drop cylinder, 16 / 65536 a step. */
        behave.drop_mask = 0x0200u; behave.drop_value = 0x0200u; behave.drop_lat = 117965; behave.drop_chance = 16; behave.launch_chance = 4096;
        behave.contact_norm = 36045; /* racer.c's impact: 1.1 x the closing speed along the line between the two, shared */
        params.behave = &behave;
        params.boost_energy = 0;
        fzx_field_event_count = 0;
    }
#endif
    sh2z_field_init(&field, &line, &params, veh, brain, order, slot_of, rank_order, FZX_FIELD_CARS, CSEL(field_seed,FZX_RIVALS_FIELD_SEED));
#if ENG_RIVAL_BEHAVE
    for (uint32_t i = 0; i < FZX_FIELD_CARS; ++i) { prev_flags[i] = veh[i].flags; prev_credit[i] = brain[i].ko_credit; }
#endif
    field.focus = 0;
    memset(is3d, 0, sizeof(is3d)); memset(want_run, 0, sizeof(want_run));
#if ENG_ATTACKS
    {
        /* One contact rule for everyone (engine_attacks.c, SH2ZAM body-contact-1): the field's own box
         * contact and its flat wall charge are off; walls hurt through the attacks' own rule. */
        params.contact_lat = 0;
        params.wall_energy = 0;
        fzx_atk_field_t a;
        a.field = &field; a.veh = veh; a.brain = brain; a.line = &line; a.pose = line_pose_fast;
        a.config = &fzx_rivals_field[0][0]; a.config_stride = (uint32_t)(sizeof(fzx_rivals_field[0]) / sizeof(fzx_rivals_field[0][0]));
        a.count = FZX_FIELD_CARS;
        fzx_atk_init(&a, FZX_RIVALS_FIELD_SEED);
    }
#endif
    fzx_field_report[0] = 1;
    return true;
#else
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
#if E2_LOD
    {
        static const int32_t lod[3] = { E2_LOD_NEAR };
        params.lod_near[0] = U(lod[0]); params.lod_near[1] = U(lod[1]); params.lod_near[2] = U(lod[2]);
    }
#endif
    sh2z_field_init(&field, &line, &params, veh, brain, order, slot_of, rank_order, FZX_FIELD_CARS, 1u);
    field.focus = 0;
    memset(is3d, 0, sizeof(is3d)); memset(want_run, 0, sizeof(want_run));
    fzx_field_report[0] = 1;
    return true;
#endif
}

void fzx_field_tick(const sh2z_vd_body_t *player) {
    if (fzx_field_report[0] != 1) return;
    player_into_field(player);
    if (!field.started && player->race_time > 0) { sh2z_field_start(&field); fzx_field_report[4] = field.tick; }
    APROF_T(fp);
#if ENG_ATTACKS
    fzx_atk_move(player);
#endif
    APROF(6, fp);
    /* line-field-7: the step for this image's behaviour block (the other is not linked). */
#if ENG_RIVAL_BEHAVE && E2_PACK
    sh2z_field_step_behave(&field);
#else
    sh2z_field_step_plain(&field);
#endif
    APROF(7, fp);
#if ENG_ATTACKS
    fzx_atk_after(player);
#endif
    fzx_field_report[3] = field.tick; fzx_field_report[5] = field.contacts;
    fzx_field_report[11] = field.finished; fzx_field_report[12] = field.retired;
#if ENG_RIVAL_BEHAVE
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
        const uint16_t f = veh[i].flags, was = prev_flags[i];
        const sh2z_field_brain_t *b = &brain[i];
        if ((f & SH2Z_VEH_FALLING) && !(was & SH2Z_VEH_FALLING)) field_event(i, FZX_FIELD_EV_FALL, b->ko_by, b->fall_tick);
        if (b->ko_credit != prev_credit[i] && b->ko_credit >= 0) field_event(i, FZX_FIELD_EV_KO, b->ko_credit, (int32_t)field.tick);
        if ((f & SH2Z_VEH_RETIRED) && !(was & SH2Z_VEH_RETIRED)) field_event(i, FZX_FIELD_EV_RETIRE, b->ko_credit, b->retire_tick);
        prev_flags[i] = f; prev_credit[i] = b->ko_credit;
    }
#endif
}

#if E2_FASTPOSE
/* Squared distance, Q16 units^2 (no square root): compared against squared limits. */
typedef uint64_t fdist_t;
#define DLIM(u) (((uint64_t)(u) * (uint64_t)(u)) << 16)
static fdist_t dist_sq(const int32_t a[3], const int32_t b[3]) {
    uint64_t s = 0;
    for (int c = 0; c < 3; ++c) { const int64_t d = (int64_t)(a[c] - b[c]) >> 8; s += (uint64_t)(d * d); } /* Q8 */
    return s;
}
#else
typedef uint32_t fdist_t;
#define DLIM(u) (u)
#endif
#if !E2_FASTPOSE
static uint32_t dist_units(const int32_t a[3], const int32_t b[3]) {
    int64_t s = 0;
    for (int c = 0; c < 3; ++c) { const int64_t d = (int64_t)(a[c] - b[c]) >> 8; s += d * d; } /* Q8 */
    uint64_t v = (uint64_t)s, r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (uint32_t)(r >> 8); /* whole units */
}
#endif

unsigned fzx_field_cars(fzx_field_car_t *out, const sh2z_vd_body_t *player) {
    if (fzx_field_report[0] != 1) return 0;
    /* fzx_field_k: K in bits 0..7, the rivals' 3D LOD (source numbering, 0 = 1) in bits 8..15. */
    const uint32_t k = (fzx_field_k & 0xffu) > FZX_FIELD_K_MAX ? FZX_FIELD_K_MAX : (fzx_field_k & 0xffu);
    const uint32_t dmax = fzx_field_d, margin = fzx_field_margin;
    fdist_t dist[FZX_FIELD_CARS];
    uint32_t near = 0;
    /* The player. */
    for (int c = 0; c < 3; ++c) {
        out[0].pos[c] = player->pos[c];
        for (int r = 0; r < 3; ++r) out[0].basis[r][c] = q14(player->body[r][c]);
    }
    out[0].id = 0; dist[0] = 0;
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
#if E2_FASTPOSE
        fzx_field_car_t *o = &out[i];
        line_pose_fast(&veh[i].t, o->pos, o->basis);
        o->id = (uint16_t)i;
        dist[i] = dist_sq(o->pos, player->pos);
        if (dist[i] < DLIM(NEAR_UNITS)) ++near;
#else
        int32_t pos[3], f[3], u[3], l[3];
        line_pose(&veh[i].t, pos, f, u, l);
        fzx_field_car_t *o = &out[i];
        for (int c = 0; c < 3; ++c) { o->pos[c] = pos[c]; o->basis[0][c] = q14(f[c]); o->basis[1][c] = q14(u[c]); o->basis[2][c] = q14(l[c]); }
        o->id = (uint16_t)i;
        dist[i] = dist_units(pos, player->pos);
        if (dist[i] < NEAR_UNITS) ++near;
#endif
    }
    /* Selection with hysteresis: rivals that want 3D are within D - margin (entering) or
     * D + margin (staying); among them the nearest K - 1 (the player holds the first
     * slot when K >= 1). A change of form needs HOLD_FRAMES frames in a row. */
    uint32_t in3d = 0;
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) in3d += is3d[i];
    for (uint32_t i = 1; i < FZX_FIELD_CARS; ++i) {
        const uint32_t limit = is3d[i] ? dmax + margin : (dmax > margin ? dmax - margin : 0u);
        bool want = k >= 2 && dist[i] < DLIM(limit);
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

#if defined(ENG_SCENES) && ENG_SCENES
/* SCENES.md 11.3: each car's state at the trigger, in the original's terms. Times use the race
 * clock (ticks x 50 / 3 ms, physics_native.c) for everyone; distances are Q8 line units as
 * floats (the projection's units cancel). The field keeps no rank at retirement: a retired
 * car's rank at the trigger stands in for it. */
#include "engine_cup.h"
bool fzx_field_entrants(fzx_entrants_t *out, uint8_t player_machine, uint8_t player_skin) {
    uint8_t machine[FZX_ENTRANTS_MAX] = { 0 };
#if E2_PACK
    for (unsigned i = 1; i < FZX_FIELD_CARS; ++i) machine[i] = (uint8_t)fzx_rivals_field[i][5];
#else
    for (unsigned i = 1; i < FZX_FIELD_CARS; ++i) machine[i] = (uint8_t)(i % 30u);
#endif
    return fzx_entrants_build(out, FZX_FIELD_CARS, player_machine, player_skin, machine);
}
static int32_t clock_ms(int32_t t) { return t > 0 ? (int32_t)(((int64_t)t * 50) / 3) : 0; }
unsigned fzx_field_classify_in(fzx_cl_in_t *out, int32_t player_ms, fzx_cl_key_t *key) {
    if (fzx_field_report[0] != 1 || !field.started) return 0u;
    const int32_t now = (int32_t)(field.tick - field.start_tick);
    for (unsigned i = 0; i < FZX_FIELD_CARS; ++i) {
        const sh2z_vehicle_t *v = &veh[i];
        const sh2z_field_brain_t *b = &brain[i];
        fzx_cl_in_t *c = &out[i];
        memset(c, 0, sizeof(*c));
        c->entrant = (uint8_t)i; c->position = (uint8_t)(v->rank + 1u);
        c->laps_done = (uint8_t)(b->laps_seen > 0 ? (b->laps_seen < (int16_t)params.laps ? b->laps_seen : params.laps) : 0);
        for (unsigned k = 0; k < 3u; ++k) {
            const int32_t end = b->lap_tick[k], from = k ? b->lap_tick[k - 1u] : 0;
            c->lap_ms[k] = (end >= 0 && from >= 0) ? clock_ms(end - from) : 0;
        }
        if (i == 0u) { c->state = FZX_CLIN_FINISHED; c->race_ms = player_ms; }
        else if (v->flags & SH2Z_VEH_FINISHED) { c->state = FZX_CLIN_FINISHED; c->race_ms = clock_ms(b->finish_tick); }
        else if (v->flags & SH2Z_VEH_RETIRED) {
            c->state = FZX_CLIN_RETIRED; c->retire_frame = b->retire_tick; c->retire_position = c->position;
#if ENG_ATTACKS
            /* Racer_RetireRacer's own tick and place (engine_attacks.c), not the place at the trigger. */
            if (fzx_atk_retire_tick[i] >= 0) { c->retire_frame = fzx_atk_retire_tick[i]; c->retire_position = fzx_atk_retire_place[i]; }
#endif
        } else {
            c->state = FZX_CLIN_RUNNING; c->race_ms = clock_ms(now);
            c->race_distance = (float)((int64_t)v->t.lap * line.length + v->t.dist);
        }
    }
    key->frame = now; key->course_length = (float)line.length; key->laps = params.laps;
    return FZX_FIELD_CARS;
}
#endif
