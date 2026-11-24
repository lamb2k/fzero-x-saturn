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

/* The effects' buffers live in low work RAM on the scenes image (ENG_LOW_COLD, engine_low.x: the high block is the
 * master's headroom): the CPU reads and writes them only, never a DMA, and only a few hundred bytes of each are
 * touched in a frame. The other images keep them in high work RAM. */
#if defined(ENG_LOW_COLD) && ENG_LOW_COLD
#define FX_LOW(n) __attribute__((section(".bss.lowcold.fx_" n)))
#else
#define FX_LOW(n)
#endif

#ifndef FZX_GUEST_MASK
#define FZX_GUEST_MASK 0ull
#endif

volatile uint32_t fzx_fx_report[16];
volatile uint32_t fzx_fx_demand[2];
volatile uint32_t fzx_fx_on_slave;
#if ENG_FX_PROF
uint16_t fzx_fx_prof[FZX_FX_PROF_ROWS][5] FX_LOW("prof");   /* the profile build keeps its rows in low work RAM (they do not fit the high block of the 24-course image) */
uint32_t fzx_fx_prof_row = FZX_FX_PROF_ROWS;
/* [who: 0 master, 1 slave][0..4 the job's phases (player boosters, sparks, rival sort, rival boosters, order and copy), 5 the job, 6..13 the counters, 14 jobs] */
volatile uint32_t fzx_fx_agg[2][16];
/* The stage in phases (master FRT units, summed over frames): [0] the view, [1] the player's colour, hover and sparks, [2] the cars, [3] the rival order and split, [4] frames. */
volatile uint32_t fzx_fx_sagg[8];
#define FXP(k, v) do { if (fzx_fx_prof_row < FZX_FX_PROF_ROWS) fzx_fx_prof[fzx_fx_prof_row][k] = (uint16_t)(v); } while (0)
#else
#define FXP(k, v) do { } while (0)
#endif

#define Q16(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))
#define CARS FZX_FIELD_CARS
enum { WHITE_TICKS = 5, NEAR_TAIL = 400, NEAR_BOOST = 900, FAR_SPAWN = 2500, SORT_BANDS = 192u /* 16-unit bands to 3,072 units */ };

/* The source's collision-spark ring (effects.c): 32 sparks of 16 ticks, drag 1.05. */
static sh2z_ep_particle_t it_spark[32] FX_LOW("spark");
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
typedef struct {
    uint16_t pmod, color, texture;
    bool valid;
} boost_material_t;
static boost_material_t head_mats[3], tail_mats[3];
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

/* Booster materials use only the two fixed 16x16 cells and three palettes.
 * Validate their VDP1 words once after loading the effect pack. */
static boost_material_t boost_material(uint32_t texture, uint32_t palette, uint16_t pmod) {
    const bool valid = !(texture & 7u) && !(palette & 31u) && texture <= 0x7ffffu && palette <= 0x7ffe0u;
    return (boost_material_t){.pmod = pmod, .color = (uint16_t)(palette >> 3),
                             .texture = (uint16_t)(texture >> 3), .valid = valid};
}

/* 7.5 sqrt(t / 100), Q16, for the boost timer t (racer.c's booster size). */
static int32_t sqrt_share[101] FX_LOW("sqrt");

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
    for (unsigned env = 0; env < 3u; ++env) {
        const uint32_t palette = (uint32_t)art + FZX_FX_CLUT_BOOST + env * 32u;
        head_mats[env] = boost_material((uint32_t)art + FZX_FX_BOOST_HEAD, palette, pmod_tex);
        tail_mats[env] = boost_material((uint32_t)art + FZX_FX_BOOST_TAIL, palette, pmod_tex_half);
    }
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
/* One emit job's own state: the frame's two jobs may run at once, one on each SH-2. A job writes its commands straight
 * into its slot of the job buffer (one 32-byte command a slot, in the order emitted) and keeps each one's depth; the
 * order (a sort of slot indexes) is made after. The budget keeps the nearest: past FZX_FX_JOB_SLOTS a new command
 * replaces the farthest kept one when it is nearer (fzx_fx_report[10] counts the commands left out). */
/* A job's output, read by the master (through the purge) once the job is done: first the sorted order (halfword
 * `ord`: slot of the i-th command far to near, `key`: its 16-unit depth band), then the commands by slot. */
typedef struct {
    uint8_t ord[FZX_FX_JOB_SLOTS], key[FZX_FX_JOB_SLOTS];
    vdp1_cmdt_t cmd[FZX_FX_JOB_SLOTS];
} job_buf_t;
static job_buf_t job_buf[FZX_FX_JOBS] FX_LOW("jobs") __attribute__((aligned(32)));
typedef struct {
    job_buf_t *buf;
    int32_t depth[FZX_FX_JOB_SLOTS];
    uint8_t key[FZX_FX_JOB_SLOTS], tmp[FZX_FX_JOB_SLOTS];
    uint16_t spare[16];
    int32_t spare_depth;
    unsigned n_items, demand, dropped, boost_cmds;
#if ENG_FX_PROF
    uint32_t c[8];   /* [0] sockets visited, [1] heads refused by the projection, [2] heads under a pixel, [3] heads emitted, [4] tails projected out, [5] tails emitted, [6] cars, [7] cars with a tail */
#endif
} jobctx_t;
static jobctx_t jc[FZX_FX_JOBS] FX_LOW("jc");
static uint16_t *slot(jobctx_t *x, int32_t depth) {
    ++x->demand;
    x->spare_depth = depth;
    return x->n_items < FZX_FX_JOB_SLOTS ? (uint16_t *)(void *)&x->buf->cmd[x->n_items] : x->spare;
}
static void commit(jobctx_t *x, uint16_t *t) {
    if (t != x->spare) { x->depth[x->n_items++] = x->spare_depth; return; }
    ++x->dropped;
    unsigned far = 0;
    for (unsigned i = 1; i < FZX_FX_JOB_SLOTS; ++i) if (x->depth[i] > x->depth[far]) far = i;
    if (x->spare_depth < x->depth[far]) { memcpy(&x->buf->cmd[far], x->spare, 32u); x->depth[far] = x->spare_depth; }
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

/* Fixed 16x16 distorted-sprite command, with the same vertex order and bounds
 * as the generic effect-pool emitter. */
static inline void boost_cmd(uint16_t *cmd, const boost_material_t *mat) {
    cmd[0] = SH2Z_EP_DISTORTED; cmd[1] = 0;
    cmd[2] = mat->pmod; cmd[3] = mat->color;
    cmd[4] = mat->texture; cmd[5] = 0x0210;
    cmd[14] = 0; cmd[15] = 0;
}

static bool boost_head(uint16_t *cmd, const boost_material_t *mat, const pt1_t *at, int32_t radius) {
    const int32_t left = at->x - radius, right = at->x + radius;
    const int32_t top = at->y - radius, bottom = at->y + radius;
    if (!mat->valid || left < -1024 || right > 1023 || top < -1024 || bottom > 1023) return false;
    boost_cmd(cmd, mat);
    cmd[6] = (uint16_t)right; cmd[7] = (uint16_t)at->y;
    cmd[8] = (uint16_t)at->x; cmd[9] = (uint16_t)bottom;
    cmd[10] = (uint16_t)left; cmd[11] = (uint16_t)at->y;
    cmd[12] = (uint16_t)at->x; cmd[13] = (uint16_t)top;
    return true;
}

static bool boost_tail(uint16_t *cmd, const boost_material_t *mat, const pt1_t *at, const pt1_t *end, int32_t radius) {
    const int16_t left = (int16_t)(at->x - radius), right = (int16_t)(at->x + radius);
    /* Both projected points already lie inside the fixed 320x224 view plus its 96-pixel guard. */
    if (!mat->valid || left < -1024 || left > 1023 || right < -1024 || right > 1023) return false;
    boost_cmd(cmd, mat);
    cmd[6] = (uint16_t)right; cmd[7] = (uint16_t)at->y;
    cmd[8] = (uint16_t)end->x; cmd[9] = (uint16_t)end->y;
    cmd[10] = (uint16_t)left; cmd[11] = (uint16_t)at->y;
    cmd[12] = cmd[10]; cmd[13] = cmd[11];
    return true;
}

/* The racers' boosters (Racer_Draw's booster pass). */
#if ENG_FX_PROF
#define PC(i) (++x->c[i])
#else
#define PC(i) do { } while (0)
#endif
static void boosters(jobctx_t *x, const sh2z_ep_view_t *v, const boost_t *o, int32_t hover_frac, uint32_t tick) {
    const int16_t *b = o->row;
    int32_t fl = (int32_t)((3u - (tick & 3u)) * 2u);   /* (3 - (frame & 3)) x 2 x 0.2 flicker */
    if (fl < o->flare) fl = o->flare;
    const int32_t flicker = fl * Q16(0.2);
    const bool tail = o->depth < NEAR_TAIL * 16384 && o->speed > Q16(1.0);
    PC(6); if (tail) PC(7);
    for (int k = (int)b[0] - 1; k >= 0; --k) {
        PC(0);
        const int16_t *q = b + 1 + 4 * k;
        const int32_t s = q[3] * Q16(0.35) / 16;                       /* size x 0.35 */
        int32_t p[3];
        /* model (x left, y up, z forward) x 0.1, z moved back by 3.6 s (model units) */
        const int32_t mx = q[0] * Q16(0.1), my = q[1] * Q16(0.1), mz = q[2] * Q16(0.1) - m16(s, Q16(0.36));
        for (int a = 0; a < 3; ++a)
            p[a] = o->pos[a] + (int32_t)(((int64_t)mx * o->basis[2][a] + (int64_t)my * o->basis[1][a] + (int64_t)mz * o->basis[0][a]) >> 14);
        pt1_t pt;
        if (!proj1(v, p, &pt)) { PC(1); continue; }
        const int32_t size = m16(s + flicker + o->extra, hover_frac);
        const int32_t r = px1(&pt, size);
        if (r < 1) { PC(2); continue; }
        uint16_t *t = slot(x, pt.depth - 1);
        if (t && boost_head(t, &head_mats[o->env], &pt, r)) {
            commit(x, t); ++x->boost_cmds; PC(3);
        }
        if (!tail) continue;
        int32_t e[3]; pt1_t pe;
        along(e, p, -m16(o->speed, Q16(0.9)), o->basis[0]);
        if (!proj1(v, e, &pe)) { PC(4); continue; }
        t = slot(x, pt.depth);
        if (t && boost_tail(t, &tail_mats[o->env], &pt, &pe, r)) {
            commit(x, t); ++x->boost_cmds; PC(5);
        }
    }
}

/* The frame's inputs to the emit, staged by the master (fzx_fx_stage) and read by whichever SH-2
 * runs the emit (fzx_fx_job). The rivals are within the booster range and not wrecked. */
static sh2z_ep_view_t snap_v;
static boost_t snap_b[CARS] FX_LOW("snap_b");
static unsigned snap_n;
static int32_t snap_hover, snap_up[3];
/* The sparks: the master steps the ring during the frame's ticks, so the emit reads a copy. */
static sh2z_ep_particle_t snap_sp[32] FX_LOW("snap_sp");
static unsigned snap_sp_n;
static uint32_t snap_tick;
static bool snap_ok;
/* The rivals nearest first (indices into snap_b), and how many of them the first job takes: the staged frame
 * is emitted as two jobs (player, sparks and the nearest rivals; then the rest) so the two SH-2s can share it.
 * The jobs' items are in the original emit order (player, sparks, rivals nearest first), job 0 then job 1. */
static uint8_t snap_rank[CARS];
static unsigned snap_m, snap_end[FZX_FX_JOBS + 1u];   /* job j takes the rivals snap_end[j] .. snap_end[j + 1] (nearest first) */
/* Each job's results, written by its SH-2 and read by the master through the cache-through window. */
typedef struct { uint32_t n, demand, dropped, boost, live, ticks;
#if ENG_FX_PROF
    uint32_t ph[6], c[8];
#endif
} fx_out_t;
static fx_out_t job_out[FZX_FX_JOBS];

/* A racer's share of the emit (a head costs about 5, a tail 3): only the split point reads it. */
static unsigned snap_boost_weight(const boost_t *o) {
    const bool tail = o->depth < NEAR_TAIL * 16384 && o->speed > Q16(1.0);
    return (unsigned)o->row[0] * (tail ? 8u : 5u);
}

bool fzx_fx_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const fzx_field_car_t *cars, const int32_t *car_depth, unsigned n,
                  uint32_t player_machine) {
    snap_ok = false;
    if (fzx_fx_report[0] != 1 || !pl_ok) return false;
#if ENG_FX_PROF
    const uint16_t st0 = cpu_frt_count_get();
#endif
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
#if ENG_FX_PROF
    uint16_t sp0 = st0;
#define SPH(k) do { const uint16_t n_ = cpu_frt_count_get(); fzx_fx_sagg[k] += (uint16_t)(n_ - sp0); sp0 = n_; } while (0)
#else
#define SPH(k) do { } while (0)
#endif
    SPH(0);
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
    SPH(1);
    /* The boosters' inputs: the player (not crashed or spinning) and each rival within 900 units. */
    snap_n = 0;
    if (n > CARS) n = CARS;
    for (unsigned k = 0; k < n; ++k) {
        const fzx_field_car_t *c = &cars[k];
        const uint32_t id = c->id;
        if (id >= CARS) continue;
        boost_t *o = &snap_b[snap_n];
        /* Share the sprite cull depth when it was computed with the same rounded camera row.
         * The close-up cars and builds without sprite culling still need their own depth. */
        const int32_t depth = car_depth[k] == INT32_MIN ? sh2z_ep_depth(&v, c->pos) : car_depth[k];
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
        o->pos[0] = c->pos[0]; o->pos[1] = c->pos[1]; o->pos[2] = c->pos[2];
        memcpy(o->basis, c->basis, sizeof(o->basis));
        o->depth = depth; o->id = (uint8_t)id;
        ++snap_n;
    }
    snap_v = v;
    SPH(2);
    {   /* the rivals nearest first (sh2z_ep_sort puts the farthest first) and the split point */
        static int32_t cd[CARS]; static uint8_t ck[CARS], co[CARS];
        unsigned m = 0, wp = 0;
        for (unsigned k = 0; k < snap_n; ++k) {
            if (snap_b[k].id == 0u) { wp += snap_boost_weight(&snap_b[k]); continue; }
            cd[m] = snap_b[k].depth; ck[m] = (uint8_t)k; ++m;
        }
        sh2z_ep_sort(co, cd, m);
        unsigned total = wp + snap_sp_n * 5u;
        for (unsigned t = 0; t < m; ++t) { snap_rank[t] = ck[co[m - 1u - t]]; total += snap_boost_weight(&snap_b[snap_rank[t]]); }
        /* the jobs cut the cost (player and sparks to job 0, then the rivals nearest first) into equal parts */
        unsigned acc = wp + snap_sp_n * 5u, kk = 0;
        snap_end[0] = 0;
        for (unsigned j = 1; j < FZX_FX_JOBS; ++j) {
            while (kk < m && acc * FZX_FX_JOBS < total * j) acc += snap_boost_weight(&snap_b[snap_rank[kk++]]);
            snap_end[j] = kk;
        }
        snap_end[FZX_FX_JOBS] = m;
        snap_m = m;
    }
    SPH(3);
    snap_ok = true;
#if ENG_FX_PROF
    ++fzx_fx_sagg[4];
    FXP(0, cpu_frt_count_get() - st0);
    if (fzx_fx_prof_row < FZX_FX_PROF_ROWS) {
        fzx_fx_prof[fzx_fx_prof_row][1] = 0; fzx_fx_prof[fzx_fx_prof_row][2] = 0;
        fzx_fx_prof[fzx_fx_prof_row][3] = (uint16_t)(snap_n << 11);
    }
#endif
    return true;
}

/* Far to near by 16-unit depth bands, the order inside a band the emit order (a stable counting sort on the 8-bit
 * band, as two 4-bit passes). Writes the job buffer's sorted slot order and each one's band; returns the count. */
static unsigned order_job(jobctx_t *x) {
    const unsigned n = x->n_items;
    uint8_t *const key = x->key, *const tmp = x->tmp;
    uint8_t *const ord = x->buf->ord, *const bnd = x->buf->key;
    uint8_t cnt[16];
    for (unsigned i = 0; i < 16u; ++i) cnt[i] = 0;
    for (unsigned i = 0; i < n; ++i) {
        uint32_t b = (uint32_t)x->depth[i] >> 18;                  /* Q14 depth / 16 units */
        if (b >= SORT_BANDS) b = SORT_BANDS - 1u;
        const uint8_t k = (uint8_t)(SORT_BANDS - 1u - b);          /* far first */
        key[i] = k; ++cnt[k & 15u];
    }
    unsigned sum = 0;
    for (unsigned i = 0; i < 16u; ++i) { const unsigned c = cnt[i]; cnt[i] = (uint8_t)sum; sum += c; }
    for (unsigned i = 0; i < n; ++i) tmp[cnt[key[i] & 15u]++] = (uint8_t)i;
    for (unsigned i = 0; i < 16u; ++i) cnt[i] = 0;
    for (unsigned i = 0; i < n; ++i) ++cnt[key[i] >> 4];
    sum = 0;
    for (unsigned i = 0; i < 16u; ++i) { const unsigned c = cnt[i]; cnt[i] = (uint8_t)sum; sum += c; }
    for (unsigned i = 0; i < n; ++i) {
        const unsigned t = tmp[i];
        const unsigned at = cnt[key[t] >> 4]++;
        ord[at] = (uint8_t)t; bnd[at] = key[t];
    }
    return n;
}

/* One job of the staged frame: job 0 the player's boosters, the sparks and the nearest rivals, job 1 the rest of the
 * rivals. Either SH-2 (dual-claim-1 in engine_main.c); writes only its own context and results. */
void fzx_fx_job(unsigned job) {
    fx_out_t *const jo = &job_out[job];
    if (!snap_ok) { jo->n = 0; jo->demand = jo->dropped = jo->boost = jo->live = jo->ticks = 0; return; }
    jobctx_t *const x = &jc[job];
    x->buf = &job_buf[job];
    const uint16_t t0 = cpu_frt_count_get();
    const sh2z_ep_view_t v = snap_v;
    x->n_items = 0; x->demand = 0; x->dropped = 0; x->boost_cmds = 0;
#if ENG_FX_PROF
    uint16_t pt = t0;
    uint32_t phs[6] = {0, 0, 0, 0, 0, 0};
#define PHASE(k) do { const uint16_t n_ = cpu_frt_count_get(); phs[k] = (uint16_t)(n_ - pt); pt = n_; } while (0)
    for (unsigned i = 0; i < 8u; ++i) x->c[i] = 0;
#else
#define PHASE(k) do { } while (0)
#endif
    /* By priority, while the budget lasts: the player's boosters, the sparks, then the rivals'
     * boosters nearest first (the budget keeps the nearest). */
    if (job == 0u) {
        for (unsigned k = 0; k < snap_n; ++k) if (snap_b[k].id == 0u) boosters(x, &v, &snap_b[k], snap_hover, snap_tick);
        PHASE(0);
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
            uint16_t *t = slot(x, pt.depth);
            if (t && sh2z_ep_emit_diamond(t, pt.x, pt.y, r, (uint32_t)art + FZX_FX_SPARK + (uint32_t)(e->timer >> 1) * 512u, 32u, 32u,
                                          (uint32_t)art + FZX_FX_CLUT_SPARK + (uint32_t)e->timer * 32u, pmod_tex)) commit(x, t);
        }
        PHASE(1);
    }
    {
        const unsigned lo = snap_end[job], hi = snap_end[job + 1u];
        for (unsigned t = lo; t < hi; ++t) boosters(x, &v, &snap_b[snap_rank[t]], snap_hover, snap_tick);
        PHASE(3);
    }
    const unsigned out = order_job(x);
    PHASE(4);
#if ENG_FX_PROF
    for (unsigned i = 0; i < 6u; ++i) jo->ph[i] = phs[i];
    for (unsigned i = 0; i < 8u; ++i) jo->c[i] = x->c[i];
#endif
    jo->n = out; jo->demand = x->demand; jo->dropped = x->dropped; jo->boost = x->boost_cmds;
    jo->live = job == 0u ? snap_sp_n : 0u;
    jo->ticks = (uint16_t)(cpu_frt_count_get() - t0);
}

/* On the master after both jobs are done (slave_mask bit j: job j ran on the slave): the two sorted lists merged
 * far to near (job 0 first among equal bands, the original emit order) into dst, at most capacity (the farthest
 * go first when over). Returns the count. */
unsigned fzx_fx_collect(vdp1_cmdt_t *dst, unsigned capacity, unsigned slave_mask) {
    const uint32_t *base[FZX_FX_JOBS];
    const uint8_t *ord[FZX_FX_JOBS], *bnd[FZX_FX_JOBS];
    unsigned n[FZX_FX_JOBS], idx[FZX_FX_JOBS], total = 0;
    uint32_t key[FZX_FX_JOBS];
    for (unsigned j = 0; j < FZX_FX_JOBS; ++j) {
        const volatile fx_out_t *o = (const volatile fx_out_t *)((uintptr_t)&job_out[j] | CPU_CACHE_THROUGH);
        n[j] = o->n; idx[j] = 0; total += n[j];
        /* the slave's lists are in memory (write-through): purge this CPU's copies of the order and of the commands
         * used (the buffer's first 2 x 64 B, then n commands) and read them cached */
        if (((slave_mask >> j) & 1u) && n[j]) cpu_cache_area_purge(&job_buf[j], sizeof(job_buf[j].ord) + sizeof(job_buf[j].key) + n[j] * sizeof(vdp1_cmdt_t));
        base[j] = (const uint32_t *)(const void *)job_buf[j].cmd;
        ord[j] = job_buf[j].ord; bnd[j] = job_buf[j].key;
        key[j] = n[j] ? bnd[j][0] : 0x100u;
    }
    unsigned skip = total > capacity ? total - capacity : 0u, out = 0;
    for (unsigned c = 0; c < total; ++c) {
        unsigned j = 0;
        for (unsigned q = 1; q < FZX_FX_JOBS; ++q) if (key[q] < key[j]) j = q;   /* the first job among equal bands */
        const uint32_t *w = base[j] + 8u * ord[j][idx[j]];
        key[j] = ++idx[j] < n[j] ? bnd[j][idx[j]] : 0x100u;
        if (skip) { --skip; continue; }
        uint32_t *d = (uint32_t *)(void *)&dst[out++];
        d[0] = w[0]; d[1] = w[1]; d[2] = w[2]; d[3] = w[3]; d[4] = w[4]; d[5] = w[5]; d[6] = w[6];
        d[7] = w[7] & 0xffff0000u;
    }
    return out;
}

#if ENG_FX_PROF
#define smask_prof(m) ((m) & 15u)
#endif
void fzx_fx_job_done(unsigned slave_mask, unsigned out) {
    unsigned demand = 0, boost = 0;
    uint32_t live = 0, dropped = 0, mticks = 0;
    for (unsigned j = 0; j < FZX_FX_JOBS; ++j) {
        const volatile fx_out_t *o = (const volatile fx_out_t *)((uintptr_t)&job_out[j] | CPU_CACHE_THROUGH);
        const bool slave = (slave_mask >> j) & 1u;
        demand += o->demand; boost += o->boost; live += o->live; dropped += o->dropped;
#if ENG_FX_PROF
        {   /* sums in master FRT units (the slave's are /128, the master's /32) */
            const uint32_t sc = slave ? 4u : 1u, w = slave ? 1u : 0u;
            for (unsigned i = 0; i < 5u; ++i) fzx_fx_agg[w][i] += o->ph[i] * sc;
            fzx_fx_agg[w][5] += o->ticks * sc;
            for (unsigned i = 0; i < 8u; ++i) fzx_fx_agg[w][6 + i] += o->c[i];
            ++fzx_fx_agg[w][14];
            if (fzx_fx_prof_row < FZX_FX_PROF_ROWS) {
                uint16_t *r = fzx_fx_prof[fzx_fx_prof_row];
                r[slave ? 2 : 1] = (uint16_t)(r[slave ? 2 : 1] + o->ticks * sc);   /* [1] the master's jobs, [2] the slave's */
                r[3] = (uint16_t)((r[3] & 0xf800u) | out | ((smask_prof(slave_mask)) << 7));
            }
        }
#endif
        if (slave) ++fzx_fx_on_slave;
        else { mticks += o->ticks; }
    }
    fzx_fx_report[1] = out;
    if (out > fzx_fx_report[2]) fzx_fx_report[2] = out;
    fzx_fx_report[3] = live;
    fzx_fx_report[10] += dropped;
    fzx_fx_report[11] = boost;
    if (boost > fzx_fx_report[7]) fzx_fx_report[7] = boost;
    fzx_fx_demand[0] = demand; if (demand > fzx_fx_demand[1]) fzx_fx_demand[1] = demand;
    fzx_fx_report[13] = mticks; if (mticks > fzx_fx_report[14]) fzx_fx_report[14] = mticks;
}
