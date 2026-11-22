/* Engine race effects without the attack code (see engine_fx.h). */
#include <string.h>
#include "engine_fx.h"
#include "engine_closeup.h"
#include "texture_cd.h"
#include "fx_identity.h"
#include "fx_tables.h"
#include "sh2zam/line_field_experimental.h"
#include "sh2zam/effect_pool_experimental.h"
#include "sh2zam/divu_experimental.h"

#ifndef FZX_GUEST_MASK
#define FZX_GUEST_MASK 0ull
#endif

volatile uint32_t fzx_fx_report[16];
volatile uint32_t fzx_fx_demand[2];
volatile uint32_t fzx_fx_on_slave;

#define Q16(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))
#define CARS FZX_FIELD_CARS
enum { WHITE_TICKS = 5, NEAR_TAIL = 400, NEAR_BOOST = 900, FAR_SPAWN = 2500, SORT_BANDS = 192u /* 16-unit bands to 3,072 units */ };

/* The source's collision-spark ring (effects.c): 32 sparks of 16 ticks, drag 1.05. */
static sh2z_ep_particle_t it_spark[32];
static sh2z_ep_pool_t p_spark;

typedef struct {
    uint8_t white;        /* bodyWhiteTimer */
    uint8_t flare;        /* the boosters' flare after an accelerator press (racer.c unk_214: 27, -1 a tick) */
} car_fx_t;
static car_fx_t car[CARS];
static uint32_t whites;   /* bit i: car i flashes white */
static uint32_t rng = 0x2f6b9e1du, fx_ticks;
static uintptr_t art;     /* VDP1 byte offset of FX.PAK */
static sh2z_ep_view_t view_last;
static bool view_ok;
static uint16_t pmod_tex, pmod_tex_half;
static uint8_t player_body[3];
static int32_t player_energy, player_max;

/* The player at the end of the last tick (what the emit and the body colour read). */
typedef struct {
    int32_t boost_timer, speed, hover, energy, max_energy;
    uint32_t flags;
    int16_t up[3];
} player_fx_t;
static player_fx_t pl;
static bool pl_ok;

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t m16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + 0x8000) >> 16); }
static int32_t q14(int32_t q30) { return (q30 + (1 << 15)) >> 16; }

static void pmods(void) {
    vdp1_cmdt_t c; memset(&c, 0, sizeof(c));
    vdp1_cmdt_draw_mode_set(&c, (vdp1_cmdt_draw_mode_t){.end_code_disable = 1, .user_clipping_mode = 2,
                                                         .color_mode = VDP1_CMDT_CM_CLUT_16});
    pmod_tex = c.cmd_pmod;
    vdp1_cmdt_draw_mode_set(&c, (vdp1_cmdt_draw_mode_t){.end_code_disable = 1, .user_clipping_mode = 2,
                                                         .color_mode = VDP1_CMDT_CM_CLUT_16, .cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT});
    pmod_tex_half = c.cmd_pmod;
}

/* 7.5 sqrt(t / 100), Q16, for the boost timer t (racer.c's booster size). */
static int32_t sqrt_share[101];

static void reset(void) {
    for (uint32_t t = 0, r = 0; t <= 100u; ++t) {
        while ((r + 1u) * (r + 1u) <= t * 100u) ++r;
        sqrt_share[t] = (int32_t)(Q16(7.5) / 100 * (int32_t)r);
    }
    (void)sh2z_ep_init(&p_spark, it_spark, 32u, 16u, Q16(1.05), 65536);
    memset(car, 0, sizeof(car));
    whites = 0; view_ok = false; fx_ticks = 0; pl_ok = false;
}

uint32_t fzx_fx_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes) {
    const uintptr_t tex = (uintptr_t)parts->texture_base;
    if (ceiling < FZX_FX_PACK_BYTES + 32u) { fzx_fx_report[0] = 3; return 0; }
    const uint32_t pack_at = (ceiling - FZX_FX_PACK_BYTES) & ~31u;
    if (((tex + pack_at) & 31u) != 0u) { fzx_fx_report[0] = 3; return 0; }
    if (!fzx_texture_cd_load("FX.PAK", FZX_FX_PACK_BYTES, FZX_FX_PACK_HASH, (void *)(tex + pack_at), scratch, scratch_bytes)) {
        fzx_fx_report[0] = 2; return 0;
    }
    art = (tex + pack_at) & 0x7ffffu;
    pmods();
    reset();
    fzx_closeup_body_get(player_body);
    fzx_fx_report[12] = (uint32_t)art;
    fzx_fx_report[0] = 1;
    return pack_at;
}

void fzx_fx_reset(void) {
    if (fzx_fx_report[0] != 1) return;
    reset();
    fzx_closeup_body_set(player_body);
}

/* ---- booster rows (GUEST-BOOSTERS draft 1) --------------------------------------------------------- */
/* The row of a rendered machine id: stock 0..29 their own, 33 and 34 the guests' when enabled, any
 * other id (and a disabled guest) an empty layout. Never indexes the stock table with a guest id. */
static const int16_t empty_row[1 + 4 * 4];
static const int16_t *booster_row(uint32_t machine) {
    if (machine < 30u) return fzx_fx_boosters[machine];
    if ((machine == 33u || machine == 34u) && ((FZX_GUEST_MASK >> machine) & 1ull)) return fzx_fx_guest_boosters[machine - 33u];
    return empty_row;
}

/* ---- the tick ---------------------------------------------------------------------------------------- */
/* The effects' billboards: effect-pool-1's one-divide spot (within a pixel of sh2z_ep_project). */
typedef sh2z_ep_spot_t pt1_t;
static int32_t fy_ratio;   /* sh2z_ep_ratio of the frame's view */
#define view_depth(v, p) sh2z_ep_depth((v), (p))
#define proj1(v, p, o) sh2z_ep_spot((v), fy_ratio, (p), (o))
#define px1(o, l) sh2z_ep_spot_pixels((o), (l))

/* The source's machineLod != 0 (on the player's screen within 2,500 units), from the last frame's view. */
static bool visible(const int32_t pos[3]) {
    if (!view_ok) return false;
    const int32_t d = view_depth(&view_last, pos);
    pt1_t pt;
    return d > 0 && d <= FAR_SPAWN * 16384 && sh2z_ep_spot(&view_last, sh2z_ep_ratio(&view_last), pos, &pt);
}

/* p + a x row (Q16 + Q16 x Q1.14). */
static void along(int32_t out[3], const int32_t p[3], int32_t a, const int16_t row[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[c] + (int32_t)(((int64_t)a * row[c]) >> 14);
}

/* Effects_SpawnCollisionSparks: refused while the owner flashes white; sets its flash. */
static void spark(const int32_t pos[3], const int32_t vel[3], int32_t scale, uint32_t owner) {
    if (car[owner].white) return;
    car[owner].white = WHITE_TICKS; whites |= 1u << owner;
    if (sh2z_ep_spawn(&p_spark, pos, vel, scale, (uint8_t)owner, (uint8_t)rnd()) < 0) ++fzx_fx_report[9];
    ++fzx_fx_report[4];
}

void fzx_fx_tick(const sh2z_vd_body_t *b, int32_t max_energy, bool accel_pressed) {
    if (fzx_fx_report[0] != 1) return;
    ++fx_ticks;
    if (whites) {
        for (uint32_t i = 0; i < CARS; ++i) if (car[i].white && !--car[i].white) whites &= ~(1u << i);
    }
    /* The player's wall hit (Racer_HitWall): the spark at the wall side; its size from the damage 0.7 x into. */
    if ((b->flags & SH2Z_VD_RECOIL) && !car[0].white) {
        int32_t pos[3], vel[3], at[3];
        for (int c = 0; c < 3; ++c) { pos[c] = b->pos[c]; vel[c] = b->vel[c]; }
        if (visible(pos)) {
            int16_t left[3];
            for (int c = 0; c < 3; ++c) left[c] = (int16_t)q14(b->body[2][c]);
            int64_t lat = 0;
            for (int k = 0; k < 3; ++k) lat += (int64_t)b->disp[k] * b->road[2][k];
            const int32_t lost = player_energy - b->energy;
            const int32_t into = (b->flags & SH2Z_VD_DAMAGED) && lost > 0 ? (int32_t)(((int64_t)lost * 10) / 7) : 0;
            along(at, pos, lat >= 0 ? Q16(15.0) : Q16(-15.0), left);
            spark(at, vel, m16(into, Q16(1.5)) + Q16(18.0), 0u);
        }
    }
    /* A rival's wall touch (the line's edge this step), at its wall side. */
    for (uint32_t i = 1; i < CARS; ++i) {
        const sh2z_vehicle_t *v = fzx_field_vehicle(i);
        if (!(v->flags & SH2Z_VEH_WALL) || car[i].white) continue;
        int32_t pos[3], vel[3], at[3]; int16_t rows[3][3];
        fzx_field_pose_of(i, pos, rows);
        if (!visible(pos)) continue;
        for (int c = 0; c < 3; ++c)   /* forward x speed, right (= -left) x lateral speed; Q14 x Q8 -> Q16 */
            vel[c] = (int32_t)(((int64_t)rows[0][c] * v->t.speed - (int64_t)rows[2][c] * v->t.lat_vel) >> 6);
        along(at, pos, v->t.lateral > 0 ? Q16(-15.0) : Q16(15.0), rows[2]);   /* right = -left */
        const int32_t into = v->t.lat_vel < 0 ? -v->t.lat_vel : v->t.lat_vel;   /* Q8 */
        spark(at, vel, (into * 3) / 2 * 256 + Q16(18.0), i);
    }
    /* One step of the ring; gravity is applied at the draw (the falling spark). */
    sh2z_ep_step(&p_spark);
    /* The accelerator press (racer.c 4791..4811). */
    if (!(b->flags & (SH2Z_VD_CRASHED | SH2Z_VD_SPINNING))) {
        if (accel_pressed) car[0].flare = 27u; else if (car[0].flare) --car[0].flare;
    }
    player_energy = b->energy; player_max = max_energy;
    pl.boost_timer = b->boost_timer; pl.speed = b->speed; pl.hover = b->hover; pl.energy = b->energy;
    pl.max_energy = max_energy; pl.flags = b->flags;
    for (int c = 0; c < 3; ++c) pl.up[c] = (int16_t)q14(b->body[1][c]);
    pl_ok = true;
}

/* ---- the frame --------------------------------------------------------------------------------------- */
typedef struct { int32_t depth; uint16_t w[16]; } item_t;
static item_t items[FZX_FX_SLOTS];
static unsigned n_items;

/* The budget keeps the nearest: past FZX_FX_SLOTS a new command replaces the farthest kept one
 * when it is nearer (fzx_fx_report[10] counts the commands left out). */
static item_t spare;
static unsigned demand, dropped, boost_cmds;
static item_t *slot(int32_t depth) {
    ++demand;
    item_t *t = n_items < FZX_FX_SLOTS ? &items[n_items] : &spare;
    t->depth = depth;
    return t;
}
static void commit(item_t *t) {
    if (t != &spare) { ++n_items; return; }
    ++dropped;
    unsigned far = 0;
    for (unsigned i = 1; i < FZX_FX_SLOTS; ++i) if (items[i].depth > items[far].depth) far = i;
    if (spare.depth < items[far].depth) items[far] = spare;
}

/* A racer's boosters, staged: where it is, how it burns. */
typedef struct {
    int32_t pos[3];
    int16_t basis[3][3];       /* rows: forward, up, left */
    int32_t depth;
    int32_t extra;             /* 7.5 sqrt(boost share), Q16 (0 idle) */
    int32_t speed;             /* Q16 per tick */
    int32_t flare;             /* flicker floor from the accelerator press */
    const int16_t *row;
    uint8_t env;               /* 0 idle, 1 boost, 2 dash plate */
    uint8_t id;
} boost_t;

/* The racers' boosters (Racer_Draw's booster pass). */
static void boosters(const sh2z_ep_view_t *v, const boost_t *o, int32_t hover_frac, uint32_t tick) {
    const int16_t *b = o->row;
    int32_t fl = (int32_t)((3u - (tick & 3u)) * 2u);   /* (3 - (frame & 3)) x 2 x 0.2 flicker */
    if (fl < o->flare) fl = o->flare;
    const int32_t flicker = fl * Q16(0.2);
    const bool tail = o->depth < NEAR_TAIL * 16384 && o->speed > Q16(1.0);
    for (int k = (int)b[0] - 1; k >= 0; --k) {
        const int16_t *q = b + 1 + 4 * k;
        const int32_t s = q[3] * Q16(0.35) / 16;                       /* size x 0.35 */
        int32_t p[3];
        /* model (x left, y up, z forward) x 0.1, z moved back by 3.6 s (model units) */
        const int32_t mx = q[0] * Q16(0.1), my = q[1] * Q16(0.1), mz = q[2] * Q16(0.1) - m16(s, Q16(0.36));
        for (int a = 0; a < 3; ++a)
            p[a] = o->pos[a] + (int32_t)(((int64_t)mx * o->basis[2][a] + (int64_t)my * o->basis[1][a] + (int64_t)mz * o->basis[0][a]) >> 14);
        pt1_t pt;
        if (!proj1(v, p, &pt)) continue;
        const int32_t size = m16(s + flicker + o->extra, hover_frac);
        const int32_t r = px1(&pt, size);
        if (r < 1) continue;
        const uint32_t colr = (uint32_t)art + FZX_FX_CLUT_BOOST + (uint32_t)o->env * 32u;
        item_t *t = slot(pt.depth - 1);
        if (t && sh2z_ep_emit_diamond(t->w, pt.x, pt.y, r, (uint32_t)art + FZX_FX_BOOST_HEAD, 16u, 16u, colr, pmod_tex)) {
            commit(t); ++boost_cmds;
        }
        if (!tail) continue;
        int32_t e[3]; pt1_t pe;
        along(e, p, -m16(o->speed, Q16(0.9)), o->basis[0]);
        if (!proj1(v, e, &pe)) continue;
        const int16_t xy[4][2] = { { (int16_t)(pt.x + r), (int16_t)pt.y }, { (int16_t)pe.x, (int16_t)pe.y },
                                   { (int16_t)(pt.x - r), (int16_t)pt.y }, { (int16_t)(pt.x - r), (int16_t)pt.y } };
        t = slot(pt.depth);
        if (t && sh2z_ep_emit_quad(t->w, xy, (uint32_t)art + FZX_FX_BOOST_TAIL, 16u, 16u, colr, pmod_tex_half)) {
            commit(t); ++boost_cmds;
        }
    }
}

/* The frame's inputs to the emit, staged by the master (fzx_fx_stage) and read by whichever SH-2
 * runs the emit (fzx_fx_job). The rivals are within the booster range and not wrecked. */
static sh2z_ep_view_t snap_v;
static boost_t snap_b[CARS];
static unsigned snap_n;
static int32_t snap_hover, snap_up[3];
/* The sparks: the master steps the ring during the frame's ticks, so the emit reads a copy. */
static sh2z_ep_particle_t snap_sp[32];
static unsigned snap_sp_n;
static uint32_t snap_tick;
static bool snap_ok;
/* The emit's results, written by its SH-2 and read by the master through the cache-through window. */
typedef struct { uint32_t n, demand, dropped, boost, live, ticks; } fx_out_t;
static fx_out_t job_out;

bool fzx_fx_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const fzx_field_car_t *cars, unsigned n,
                  uint32_t player_machine) {
    snap_ok = false;
    if (fzx_fx_report[0] != 1 || !pl_ok) return false;
    sh2z_ep_view_t v;
    for (unsigned i = 0; i < 3u; ++i) for (unsigned j = 0; j < 3u; ++j) {
        const int32_t q = view->rotation[i][j];
        v.r[i][j] = (int16_t)((q >= 0 ? q + 32768 : -(-q + 32768)) / 65536);
    }
    for (unsigned i = 0; i < 3u; ++i) v.eye[i] = cam->eye[i];
    v.focal_x = view->focal[0]; v.focal_y = view->focal[1];
    v.center_x = (int16_t)((view->center[0] + 32768) >> 16); v.center_y = (int16_t)(((view->center[1] + 32768) >> 16) - 1);
    v.near_z = view->near_plane * 4;
    v.width = 320; v.height = 224; v.guard = 96;
    view_last = v; view_ok = true;
    fy_ratio = sh2z_ep_ratio(&v);
    /* The player's flash and low-energy flicker on the 3D body (racer.c 5795). */
    if (player_max > 0) {
        uint8_t col[3] = { player_body[0], player_body[1], player_body[2] };
        const int32_t e = sh2z_divu_div64((int64_t)player_energy << 16, player_max, NULL);   /* Q16 share */
        if (!(pl.flags & SH2Z_VD_CRASHED)) {
            if (!(fx_ticks & 8u) && (e < Q16(0.2) || (e < Q16(0.5) && !(fx_ticks & 16u)))) {
                const int32_t f2 = 2 * e, mix = (int32_t)(fx_ticks & 7u) * 65536 / 7;
                static const uint8_t low[3] = { 255, 0, 0 };
                for (int a = 0; a < 3; ++a) {
                    const int32_t lo = low[a] + m16(player_body[a] - low[a], f2);
                    col[a] = (uint8_t)(lo + m16(player_body[a] - lo, mix));
                }
            }
            for (int a = 0; a < 3; ++a) col[a] = (uint8_t)(col[a] + (255 - col[a]) * car[0].white / WHITE_TICKS);
        }
        fzx_closeup_body_set(col);
    }
    /* Hover share (unk_17C - 7) / 6: the boosters grow as the machines lift at the start. */
    {
        const int32_t h = pl.hover;
        snap_hover = h <= Q16(7.0) ? 0 : h >= Q16(13.0) ? 65536 : m16(h - Q16(7.0), Q16(1.0 / 6.0));
    }
    for (int c = 0; c < 3; ++c) snap_up[c] = pl.up[c];
    snap_tick = fx_ticks;
    if (pl.boost_timer > 0) { ++fzx_fx_report[5]; if (pl.flags & SH2Z_VD_PLATE_BOOST) ++fzx_fx_report[6]; }
    if (p_spark.count) ++fzx_fx_report[8];
    snap_sp_n = p_spark.count > 32u ? 32u : p_spark.count;
    for (unsigned k = 0; k < snap_sp_n; ++k) snap_sp[k] = *sh2z_ep_at(&p_spark, k);
    /* The boosters' inputs: the player (not crashed or spinning) and each rival within 900 units. */
    snap_n = 0;
    if (n > CARS) n = CARS;
    for (unsigned k = 0; k < n; ++k) {
        const fzx_field_car_t *c = &cars[k];
        const uint32_t id = c->id;
        if (id >= CARS) continue;
        boost_t *o = &snap_b[snap_n];
        const int32_t depth = view_depth(&v, c->pos);
        if (depth <= 0 || depth > NEAR_BOOST * 16384) continue;
        if (id == 0u) {
            if (pl.flags & (SH2Z_VD_CRASHED | SH2Z_VD_SPINNING)) continue;
            o->env = 0; o->extra = 0;
            if (pl.boost_timer > 0) {
                o->env = (pl.flags & SH2Z_VD_PLATE_BOOST) ? 2u : 1u;
                const uint32_t t = (uint32_t)pl.boost_timer > 100u ? 100u : (uint32_t)pl.boost_timer;   /* counts down from 100 */
                o->extra = sqrt_share[t];
            }
            o->speed = pl.speed;
            o->row = booster_row(player_machine);
            o->flare = car[0].flare;
        } else {
            const sh2z_vehicle_t *rv = fzx_field_vehicle(id);
            uint16_t bad = SH2Z_VEH_RETIRED | SH2Z_VEH_STOPPED;
#ifdef SH2Z_VEH_FALLING
            bad |= SH2Z_VEH_FALLING;
#endif
            if (rv->flags & bad) continue;
            o->env = 0; o->extra = 0;
            if ((rv->flags & SH2Z_VEH_BOOST) && rv->boost_ticks > 0) {
                const int32_t len = fzx_field_boost_ticks();
                int32_t t = len > 0 ? (rv->boost_ticks * 100) / len : 100;
                if (t > 100) t = 100;
                o->env = 1u; o->extra = sqrt_share[t];
            }
            o->speed = rv->t.speed << 8;
            o->row = booster_row(fzx_field_machine(id));
            o->flare = 0;
        }
        if (!o->row[0]) { ++fzx_fx_report[15]; continue; }
        for (int a = 0; a < 3; ++a) { o->pos[a] = c->pos[a]; for (int r = 0; r < 3; ++r) o->basis[r][a] = c->basis[r][a]; }
        o->depth = depth; o->id = (uint8_t)id;
        ++snap_n;
    }
    snap_v = v;
    snap_ok = true;
    return true;
}

/* The effects' commands, far to near, from the staged frame. Either SH-2 (dual-claim-1 in
 * engine_main.c); writes only the emit's own state and job_out. */
unsigned fzx_fx_job(vdp1_cmdt_t *commands, unsigned capacity) {
    if (!snap_ok) { job_out.n = 0; return 0; }
    const uint16_t t0 = cpu_frt_count_get();
    const sh2z_ep_view_t v = snap_v;
    n_items = 0; demand = 0; dropped = 0; boost_cmds = 0;
    /* By priority, while the budget lasts: the player's boosters, the sparks, then the rivals'
     * boosters nearest first (the budget keeps the nearest). */
    for (unsigned k = 0; k < snap_n; ++k) if (snap_b[k].id == 0u) boosters(&v, &snap_b[k], snap_hover, snap_tick);
    for (uint32_t k = 0; k < snap_sp_n; ++k) {
        const sh2z_ep_particle_t *e = &snap_sp[k];
        /* drawn falling: pos - 0.15 (timer - 1)^2 along the owner's up (here the player's) */
        sh2z_ep_particle_t d = *e;
        const int32_t drop = (int32_t)(e->timer - 1) * (int32_t)(e->timer - 1) * Q16(0.15);
        for (int a = 0; a < 3; ++a) d.pos[a] -= (int32_t)(((int64_t)drop * snap_up[a]) >> 14);
        pt1_t pt;
        if (!proj1(&v, d.pos, &pt) || pt.depth > NEAR_BOOST * 16384) continue;
        const int32_t r = px1(&pt, d.scale);
        if (r < 1) continue;
        item_t *t = slot(pt.depth);
        if (t && sh2z_ep_emit_diamond(t->w, pt.x, pt.y, r, (uint32_t)art + FZX_FX_SPARK + (uint32_t)(e->timer >> 1) * 512u, 32u, 32u,
                                      (uint32_t)art + FZX_FX_CLUT_SPARK + (uint32_t)e->timer * 32u, pmod_tex)) commit(t);
    }
    {
        static int32_t cd[CARS]; static uint8_t ck[CARS], co[CARS];
        unsigned m = 0;
        for (unsigned k = 0; k < snap_n; ++k) { if (snap_b[k].id == 0u) continue; cd[m] = snap_b[k].depth; ck[m] = (uint8_t)k; ++m; }
        sh2z_ep_sort(co, cd, m);
        for (unsigned j = m; j-- > 0;) boosters(&v, &snap_b[ck[co[j]]], snap_hover, snap_tick);
    }
    /* Far to near, after the cars: a counting sort by 16-unit depth bands (the order inside a band is the emit order). */
    static uint16_t band_n[SORT_BANDS + 1];
    static uint8_t band_of[FZX_FX_SLOTS], order[FZX_FX_SLOTS];
    memset(band_n, 0, sizeof(band_n));
    for (unsigned i = 0; i < n_items; ++i) {
        uint32_t b = (uint32_t)items[i].depth >> 18;            /* Q14 depth / 16 units */
        if (b >= SORT_BANDS) b = SORT_BANDS - 1u;
        b = SORT_BANDS - 1u - b;                                /* far first */
        band_of[i] = (uint8_t)b; ++band_n[b + 1u];
    }
    for (unsigned b = 0; b < SORT_BANDS; ++b) band_n[b + 1u] = (uint16_t)(band_n[b + 1u] + band_n[b]);
    for (unsigned i = 0; i < n_items; ++i) order[band_n[band_of[i]]++] = (uint8_t)i;
    unsigned out = 0;
    for (unsigned i = 0; i < n_items && out < capacity; ++i) memcpy(&commands[out++], items[order[i]].w, sizeof(vdp1_cmdt_t));
    job_out.n = out; job_out.demand = demand; job_out.dropped = dropped; job_out.boost = boost_cmds;
    job_out.live = snap_sp_n;
    job_out.ticks = (uint16_t)(cpu_frt_count_get() - t0);
    return out;
}

void fzx_fx_job_done(bool slave) {
    const volatile fx_out_t *o = (const volatile fx_out_t *)((uintptr_t)&job_out | CPU_CACHE_THROUGH);
    const uint32_t out = o->n;
    fzx_fx_report[1] = out;
    if (out > fzx_fx_report[2]) fzx_fx_report[2] = out;
    fzx_fx_report[3] = o->live;
    fzx_fx_report[10] += o->dropped;
    fzx_fx_report[11] = o->boost;
    if (o->boost > fzx_fx_report[7]) fzx_fx_report[7] = o->boost;
    fzx_fx_demand[0] = o->demand; if (o->demand > fzx_fx_demand[1]) fzx_fx_demand[1] = o->demand;
    if (slave) ++fzx_fx_on_slave;
    else { fzx_fx_report[13] = o->ticks; if (o->ticks > fzx_fx_report[14]) fzx_fx_report[14] = o->ticks; }
}
