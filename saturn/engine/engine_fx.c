/* Engine race effects (see engine_fx.h). */
#include <string.h>
#include "engine_fx.h"
#include "engine_attacks.h"
#include "texture_cd.h"
#include "fx_identity.h"
#include "fx_tables.h"
#include "rivals_field.h"
#include "sh2zam/effect_pool_experimental.h"
#include "sh2zam/trig12_experimental.h"

volatile uint32_t fzx_fx_report[16];
volatile uint32_t fzx_fx_events[32]; /* tick << 8 | kind (1 retire, 2 destroyed, 3 spin-out start), last 32 */
static uint32_t n_events;

#define Q16(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))
#define CARS FZX_FIELD_CARS
enum { WHITE_TICKS = 5, NEAR_TAIL = 400, NEAR_BOOST = 900, FAR_SPAWN = 2500 };

/* The source's rings (effects.c): sizes, lifetimes and growth; debris and smoke rings are smaller
 * here (the source spawns 30 debris per rival retirement into 128). */
static sh2z_ep_particle_t it_spark[32], it_exp1[32], it_exp2[8], it_fly[32], it_smoke[16], it_debris[64];
static sh2z_ep_pool_t p_spark, p_exp1, p_exp2, p_fly, p_smoke, p_debris;

typedef struct {
    int32_t yaw;          /* spin-out turn, angle12 x 65536 */
    int32_t steer;        /* Q16 spin steer (decays 0.99 a tick) */
    int32_t atk_angle, atk_rate; /* spin attack (vehicle-dynamics' rule) */
    uint8_t mode;         /* last seen FZX_ATK_FX_* bits */
    uint8_t white;        /* bodyWhiteTimer */
    int8_t side;
    uint8_t posed;
} car_fx_t;
static car_fx_t car[CARS];
static uint32_t rng = 0x2f6b9e1du, fx_ticks;
static uintptr_t art;          /* VDP1 byte offset of FX.PAK */
static sh2z_ep_view_t view_last;
static bool view_ok, player_posed;
static uint16_t pmod_tex, pmod_tex_half, pmod_poly, pmod_poly_half, pmod_line;
static uint8_t player_body[3];
static int32_t player_energy, player_max;
static uint32_t player_flags_prev;
static void event(uint32_t kind) { fzx_fx_events[n_events++ & 31u] = fx_ticks << 8 | kind; }

/* engine_closeup.c (ENG_FX): the 3D player's body colour. */
void fzx_closeup_body_get(uint8_t rgb[3]);
void fzx_closeup_body_set(const uint8_t rgb[3]);

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
/* Uniform in [lo, hi) units, Q16 (the source's (Math_Rand & 0x1FFFF) / 131071 ramps). */
static int32_t urange(int32_t lo, int32_t hi) { return lo + (int32_t)(((uint64_t)(rnd() & 0xffffu) * (uint32_t)(hi - lo)) >> 16); }
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
    vdp1_cmdt_draw_mode_set(&c, (vdp1_cmdt_draw_mode_t){.end_code_disable = 1, .user_clipping_mode = 2,
                                                         .color_mode = VDP1_CMDT_CM_RGB_32768});
    pmod_poly = c.cmd_pmod;
    vdp1_cmdt_draw_mode_set(&c, (vdp1_cmdt_draw_mode_t){.end_code_disable = 1, .user_clipping_mode = 2,
                                                         .color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT});
    pmod_poly_half = c.cmd_pmod;
    vdp1_cmdt_draw_mode_set(&c, (vdp1_cmdt_draw_mode_t){.end_code_disable = 1, .user_clipping_mode = 2,
                                                         .color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_GOURAUD});
    pmod_line = c.cmd_pmod;
}

static void reset(void) {
    (void)sh2z_ep_init(&p_spark, it_spark, 32u, 16u, Q16(1.05), 65536);
    (void)sh2z_ep_init(&p_exp1, it_exp1, 32u, 16u, Q16(1.02), 65536);
    (void)sh2z_ep_init(&p_exp2, it_exp2, 8u, 32u, Q16(1.02), 65536);
    (void)sh2z_ep_init(&p_fly, it_fly, 32u, 64u, 65536, 65536);
    (void)sh2z_ep_init(&p_smoke, it_smoke, 16u, 32u, Q16(1.02), 65536);
    (void)sh2z_ep_init(&p_debris, it_debris, 64u, 64u, 65536, Q16(0.98));
    memset(car, 0, sizeof(car));
    view_ok = false; player_posed = false; fx_ticks = 0; player_flags_prev = 0;
}

uint32_t fzx_fx_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes) {
    const uintptr_t tex = (uintptr_t)parts->texture_base;
    if (ceiling < FZX_FX_PACK_BYTES + 32u) { fzx_fx_report[0] = 3; return 0; }
    const uint32_t floor = (ceiling - FZX_FX_PACK_BYTES) & ~31u;
    if (((tex + floor) & 31u) != 0u) { fzx_fx_report[0] = 3; return 0; }
    if (!fzx_texture_cd_load("FX.PAK", FZX_FX_PACK_BYTES, FZX_FX_PACK_HASH, (void *)(tex + floor), scratch, scratch_bytes)) {
        fzx_fx_report[0] = 2; return 0;
    }
    art = (tex + floor) & 0x7ffffu;
    pmods();
    reset();
    fzx_closeup_body_get(player_body);
    fzx_fx_report[12] = (uint32_t)art;
    fzx_fx_report[0] = 1;
    return floor;
}

/* ---- racers ------------------------------------------------------------------------------- */
static uint32_t family_of(uint32_t id) {
    if (id == 0u) return 0u;
    const int32_t f = fzx_rivals_field[id][5];
    return f >= 0 && f < 30 ? (uint32_t)f : 0u;
}

/* World position (Q16), rows (Q1.14: forward, up, left) and velocity (Q16 per tick). */
static void racer_state(uint32_t i, const sh2z_vd_body_t *pl, int32_t pos[3], int16_t rows[3][3], int32_t vel[3]) {
    if (i == 0u) {
        for (int c = 0; c < 3; ++c) {
            pos[c] = pl->pos[c]; vel[c] = pl->vel[c];
            for (int r = 0; r < 3; ++r) rows[r][c] = (int16_t)q14(pl->body[r][c]);
        }
        return;
    }
    const fzx_atk_field_t *f = fzx_atk_field();
    const sh2z_vehicle_t *v = &f->veh[i];
    f->pose(&v->t, pos, rows);
    for (int c = 0; c < 3; ++c)   /* forward x speed, right (= -left) x lateral speed; Q14 x Q8 -> Q16 */
        vel[c] = (int32_t)(((int64_t)rows[0][c] * v->t.speed - (int64_t)rows[2][c] * v->t.lat_vel) >> 6);
}

/* The source's machineLod != 0 (on the player's screen within 2,500 units), from the last frame's view. */
static bool visible(const int32_t pos[3]) {
    sh2z_ep_point_t pt;
    return view_ok && sh2z_ep_project(&view_last, pos, &pt) && pt.depth <= FAR_SPAWN * 16384;
}

/* p + a x row (Q16 + Q16 x Q1.14). */
static void along(int32_t out[3], const int32_t p[3], int32_t a, const int16_t row[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[c] + (int32_t)(((int64_t)a * row[c]) >> 14);
}

/* v + a side + b up + c forward (the source's trueBasis z, y, x ramps). */
static void kick(int32_t out[3], const int32_t v[3], const int16_t rows[3][3], int32_t side, int32_t up, int32_t fwd) {
    for (int c = 0; c < 3; ++c)
        out[c] = v[c] + (int32_t)(((int64_t)side * rows[2][c] + (int64_t)up * rows[1][c] + (int64_t)fwd * rows[0][c]) >> 14);
}

static void jitter16(int32_t out[3], const int32_t p[3]) {   /* (Math_Rand % 32) - 16 on each axis */
    for (int c = 0; c < 3; ++c) out[c] = p[c] + (((int32_t)(rnd() & 31u) - 16) << 16);
}

static void spawn(sh2z_ep_pool_t *p, const int32_t pos[3], const int32_t vel[3], int32_t scale, uint32_t owner) {
    if (sh2z_ep_spawn(p, pos, vel, scale, (uint8_t)owner, (uint8_t)rnd()) < 0) ++fzx_fx_report[9];
}

/* Effects_SpawnCollisionSparks: refused while the owner flashes white; sets its flash. */
static bool spark(const int32_t pos[3], const int32_t vel[3], int32_t scale, uint32_t owner) {
    if (car[owner].white) return false;
    car[owner].white = WHITE_TICKS;
    spawn(&p_spark, pos, vel, scale, owner);
    ++fzx_fx_report[4];
    return true;
}

/* A spin-out tick (racer.c 5700): every fourth tick, on screen, a puff and two flying sparks. */
static void spin_puffs(uint32_t id, int32_t spin_timer, const sh2z_vd_body_t *pl) {
    if (((fx_ticks + id) & 3u) != 0u) return;
    int32_t pos[3], vel[3], at[3], v2[3]; int16_t rows[3][3];
    racer_state(id, pl, pos, rows, vel);
    if (!visible(pos)) return;
    jitter16(at, pos);
    const int32_t scale = (int32_t)((rnd() & 15u) + (uint32_t)(spin_timer * 5 / 150) + 5u) << 16;
    spawn(&p_exp1, at, vel, scale, id);
    ++fzx_fx_report[5];
    for (int k = 0; k < 2; ++k) {
        kick(v2, vel, rows, urange(Q16(-4.5), Q16(4.5)), urange(Q16(2.0), Q16(7.0)), urange(Q16(-4.5), Q16(4.5)));
        spawn(&p_fly, at, v2, 0, id);
    }
}

/* Retirement (racer.c 5740) or a wreck destroyed (2812): the big explosion and machine debris. */
static void blow_up(uint32_t id, int32_t scale, uint32_t debris, const sh2z_vd_body_t *pl) {
    int32_t pos[3], vel[3], at[3], v2[3]; int16_t rows[3][3];
    racer_state(id, pl, pos, rows, vel);
    if (!visible(pos)) return;
    spawn(&p_exp2, pos, vel, scale, id);
    ++fzx_fx_report[6];
    event(scale > Q16(60.0) ? 1u : 2u);
    for (uint32_t k = 0; k < debris; ++k) {
        jitter16(at, pos);
        kick(v2, vel, rows, urange(Q16(-7.5), Q16(7.5)), urange(Q16(3.5), Q16(10.5)), urange(Q16(-7.5), Q16(7.5)));
        spawn(&p_debris, at, v2, Q16(7.5), id);
        ++fzx_fx_report[7];
    }
}

/* A crashed racer (racer.c 5850): now and then a spark and smoke. */
static void wreck_smoke(uint32_t id, const sh2z_vd_body_t *pl) {
    if ((rnd() & 0x1ffffu) >= 0x444u) return;
    int32_t pos[3], vel[3], at[3], v2[3]; int16_t rows[3][3];
    racer_state(id, pl, pos, rows, vel);
    if (!visible(pos)) return;
    jitter16(at, pos);
    kick(v2, vel, rows, urange(Q16(-1.5), Q16(1.5)), urange(Q16(0.5), Q16(2.0)), urange(Q16(-1.5), Q16(1.5)));
    (void)spark(at, v2, Q16(10.0), id);
    along(v2, vel, 65536, rows[1]);
    spawn(&p_smoke, at, v2, Q16(20.0), id);
    ++fzx_fx_report[8];
}

/* A rival's wall hit (engine_attacks.c move_external; Racer_HitWall). */
static const sh2z_vd_body_t *tick_player;
static void rival_wall(uint32_t i, int32_t side, int32_t into) {
    if (fzx_fx_report[0] != 1 || !tick_player || i >= CARS) return;
    int32_t pos[3], vel[3], at[3]; int16_t rows[3][3];
    racer_state(i, tick_player, pos, rows, vel);
    if (!visible(pos)) return;
    along(at, pos, side > 0 ? Q16(-15.0) : Q16(15.0), rows[2]);   /* right = -left */
    (void)spark(at, vel, (into * 3) / 2 * 256 + Q16(18.0), i);    /* into (Q8) x 1.5 + 18 */
}

/* vehicle-dynamics' attack_motion for a spin attack. */
static void attack_motion(car_fx_t *c) {
    if (c->atk_angle == 0) return;
    c->atk_angle += c->atk_rate;
    if (c->atk_rate < 0) {
        c->atk_rate += 2;
        if (c->atk_angle <= 0) { c->atk_angle += 0x1000; if (c->atk_rate > -50) c->atk_angle = 0; }
    } else {
        c->atk_rate -= 2;
        if (c->atk_angle >= 0x1000) { c->atk_angle -= 0x1000; if (c->atk_rate < 50) c->atk_angle = 0; }
    }
}

/* The spin-out turn: atan(0.2 steer) a tick (x - x^3/3; x <= 0.2), steer x 0.99. */
static void spin_turn(car_fx_t *c) {
    if (!c->steer) return;
    const int32_t x = m16(c->steer, Q16(0.2));
    const int32_t a = x - (int32_t)(((int64_t)x * x / 65536 * x / 65536) / 3);
    c->yaw += (int32_t)(((int64_t)a * 651898 / 1000) * c->side);   /* rad Q16 -> angle12 Q16 */
    c->steer = m16(c->steer, Q16(0.99));
}

void fzx_fx_tick(const sh2z_vd_body_t *player) {
    if (fzx_fx_report[0] != 1) return;
    tick_player = player;
    fzx_atk_fx_wall = rival_wall;
    ++fx_ticks;
    for (uint32_t i = 0; i < CARS; ++i) if (car[i].white) --car[i].white;
    /* Contacts that started this tick (body-contact-1: first contacts and the pair history). */
    const sh2z_bc_field_t *bc = fzx_atk_fx_contacts();
    for (uint32_t a = 0; a < bc->count; ++a) {
        if (!bc->body[a].first) continue;
        for (uint32_t b = a + 1u; b < bc->count; ++b) {
            if (!bc->body[b].first || bc->last[b * (b - 1u) / 2u + a] != bc->tick) continue;
            const sh2z_bc_body_t *A = &bc->body[a], *B = &bc->body[b];
            int32_t mid[3], vel[3], n[3]; int64_t d2 = 0, nd = 0;
            for (int c = 0; c < 3; ++c) {
                mid[c] = (int32_t)(((int64_t)A->pos[c] + B->pos[c]) >> 1);
                vel[c] = (int32_t)(((int64_t)A->vel[c] + B->vel[c]) >> 1);
                n[c] = B->pos[c] - A->pos[c];
                d2 += ((int64_t)n[c] >> 4) * (n[c] >> 4);
                nd += ((int64_t)n[c] >> 4) * ((B->vel[c] - A->vel[c]) >> 4);
            }
            if (!visible(B->pos)) continue;
            /* |n . dv| (Q16) with n the unit separation; the impact before the 1.1 gain. */
            int32_t impact = 0;
            if (d2 > 0) {
                uint64_t r = 0, v = (uint64_t)d2, bit = 1ull << 62;   /* isqrt(d2), Q12 */
                while (bit > v) bit >>= 2;
                while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
                if (r) impact = (int32_t)((nd < 0 ? -nd : nd) / (int64_t)r) << 4;
            }
            int16_t rows[3][3]; int32_t p0[3], v0[3];
            racer_state(a, player, p0, rows, v0);
            along(mid, mid, Q16(6.0), rows[1]);
            if (spark(mid, vel, impact + Q16(12.0), b)) car[a].white = car[b].white;
        }
    }
    /* Rivals: spin-out, spin attacks, retirement, wrecks. */
    for (uint32_t i = 1; i < CARS; ++i) {
        int32_t spin_t, dir;
        const uint32_t mode = fzx_atk_fx_mode(i, &spin_t, &dir);
        car_fx_t *c = &car[i];
        if ((mode & FZX_ATK_FX_SPUN) && !(c->mode & FZX_ATK_FX_SPUN)) {
            c->steer = Q16(1.0); c->side = (rnd() & 1u) ? 1 : -1;     /* min(1, 0.2 x damage), random side */
            event(3u);
            c->atk_angle = 0;
        }
        if ((mode & FZX_ATK_FX_SPIN) && !(c->mode & FZX_ATK_FX_SPIN) && c->atk_angle == 0) {
            c->atk_angle = dir > 0 ? 0xF80 : 0x80; c->atk_rate = dir > 0 ? -0x100 : 0x100;
        }
        if (!(mode & FZX_ATK_FX_SPIN)) c->atk_angle = 0;
        if ((mode & FZX_ATK_FX_CRASHED) && !(c->mode & FZX_ATK_FX_CRASHED)) blow_up(i, Q16(80.0), 24u, player);
        if ((mode & FZX_ATK_FX_GONE) && !(c->mode & FZX_ATK_FX_GONE)) blow_up(i, Q16(40.0), 16u, player);
        if (mode & FZX_ATK_FX_SPUN) spin_puffs(i, spin_t, player);
        if ((mode & FZX_ATK_FX_CRASHED) && !(mode & FZX_ATK_FX_GONE)) wreck_smoke(i, player);
        spin_turn(c);
        attack_motion(c);
        c->mode = (uint8_t)mode;
    }
    /* One step of every ring; gravity along the player's up (the source: each racer's own). */
    for (int c = 0; c < 3; ++c) {
        const int32_t up = q14(player->body[1][c]);
        p_fly.accel[c] = -(int32_t)(((int64_t)Q16(0.4) * up) >> 14);
        p_debris.accel[c] = -(int32_t)(((int64_t)Q16(0.2) * up) >> 14);
    }
    sh2z_ep_step(&p_spark); sh2z_ep_step(&p_exp1); sh2z_ep_step(&p_exp2);
    sh2z_ep_step(&p_fly); sh2z_ep_step(&p_smoke); sh2z_ep_step(&p_debris);
}

void fzx_fx_player(const sh2z_vd_body_t *pl, int32_t max_energy) {
    if (fzx_fx_report[0] != 1) return;
    car_fx_t *c = &car[0];
    /* Wall hits (Racer_HitWall): the spark at the wall side; its size from the damage 0.7 x into. */
    if (pl->flags & SH2Z_VD_RECOIL) {
        int32_t pos[3], vel[3], at[3]; int16_t rows[3][3];
        racer_state(0, pl, pos, rows, vel);
        int64_t lat = 0;
        for (int k = 0; k < 3; ++k) lat += (int64_t)pl->disp[k] * pl->road[2][k];
        const int32_t lost = player_energy - pl->energy;
        const int32_t into = (pl->flags & SH2Z_VD_DAMAGED) && lost > 0 ? (int32_t)(((int64_t)lost * 10) / 7) : 0;
        if (visible(pos)) {
            along(at, pos, lat >= 0 ? Q16(15.0) : Q16(-15.0), rows[2]);
            (void)spark(at, vel, m16(into, Q16(1.5)) + Q16(18.0), 0u);
        }
    }
    if (pl->spin_timer) spin_puffs(0u, pl->spin_timer, pl);
    if ((pl->flags & SH2Z_VD_CRASHED) && !(player_flags_prev & SH2Z_VD_CRASHED)) blow_up(0u, Q16(80.0), 32u, pl);
    if ((pl->flags & SH2Z_VD_CRASHED) && !(pl->flags & SH2Z_VD_FALLING)) wreck_smoke(0u, pl);
    c->atk_angle = pl->attack == SH2Z_VD_ATTACK_SPIN ? pl->attack_angle : 0;
    player_energy = pl->energy; player_max = max_energy; player_flags_prev = pl->flags;
}

/* ---- the frame ---------------------------------------------------------------------------- */
void fzx_fx_pose(fzx_field_car_t *cars, unsigned n) {
    if (fzx_fx_report[0] != 1) return;
    uint32_t turned = 0;
    player_posed = false;
    for (unsigned k = 0; k < n; ++k) {
        fzx_field_car_t *o = &cars[k];
        const uint32_t id = o->id;
        if (id >= CARS) continue;
        const car_fx_t *c = &car[id];
        const uint32_t angle = (uint32_t)(c->yaw >> 16) + (uint32_t)c->atk_angle;
        if (angle & 4095u) {
            sh2z_ep_spin_rows(o->basis[0], o->basis[2], angle & 4095u);
            if (id) ++turned; else player_posed = true;
        }
        const bool spinning = id ? (c->mode & FZX_ATK_FX_SPUN) != 0 : false;
        if (spinning)   /* modelPos +-0.9 while spinning out */
            for (int a = 0; a < 3; ++a) o->pos[a] += urange(Q16(-0.9), Q16(0.9));
    }
    fzx_fx_report[15] = turned;
}

bool fzx_fx_player_posed(void) { return player_posed; }

typedef struct { int32_t depth; uint16_t w[16]; } item_t;
static item_t items[FZX_FX_SLOTS];
static unsigned n_items;

static item_t *slot(int32_t depth) {
    if (n_items >= FZX_FX_SLOTS) { ++fzx_fx_report[10]; return 0; }
    item_t *t = &items[n_items]; t->depth = depth;
    return t;
}

static void diamond(const sh2z_ep_view_t *v, const sh2z_ep_particle_t *e, uint32_t pixels, uint32_t w, uint32_t clut,
                    uint16_t pmod, int32_t max_depth) {
    sh2z_ep_point_t pt;
    if (!sh2z_ep_project(v, e->pos, &pt) || pt.depth > max_depth) return;
    const int32_t r = sh2z_ep_pixels(v, e->scale, pt.depth) >> 16;
    item_t *t = slot(pt.depth);
    if (!t || r < 1) return;
    if (sh2z_ep_emit_diamond(t->w, pt.x >> 16, pt.y >> 16, r, (uint32_t)art + pixels, w, w, (uint32_t)art + clut, pmod)) ++n_items;
}

static uint16_t rgb(const uint8_t c[3]) {
    return (uint16_t)(0x8000u | ((uint32_t)(c[2] >> 3) << 10) | ((uint32_t)(c[1] >> 3) << 5) | (uint32_t)(c[0] >> 3));
}

/* The racers' boosters (Racer_Draw's booster pass). */
static void boosters(const sh2z_ep_view_t *v, const fzx_field_car_t *o, int32_t hover_frac, bool player_only) {
    const uint32_t id = o->id;
    const fzx_atk_field_t *f = fzx_atk_field();
    if (id == 0u) {
        const sh2z_vd_body_t *b = tick_player;
        if (!b || (b->flags & (SH2Z_VD_CRASHED | SH2Z_VD_SPINNING))) return;
    } else {
        int32_t st, dir;
        if (player_only || (fzx_atk_fx_mode(id, &st, &dir) & (FZX_ATK_FX_SPUN | FZX_ATK_FX_CRASHED))) return;
    }
    sh2z_ep_point_t c0;
    if (!sh2z_ep_project(v, o->pos, &c0) || c0.depth > NEAR_BOOST * 16384) return;
    const int16_t *b = fzx_fx_boosters[family_of(id)];
    uint32_t env = 0; int32_t extra = 0, speed = 0;
    if (id == 0u) {
        const sh2z_vd_body_t *pl = tick_player;
        if (pl->boost_timer > 0) {
            env = (pl->flags & SH2Z_VD_PLATE_BOOST) ? 2u : 1u;
            /* 7.5 sqrt(boostTimer / 100), boostTimer counting down from 100 */
            uint32_t t = (uint32_t)pl->boost_timer > 100u ? 100u : (uint32_t)pl->boost_timer, r = 0;
            while ((r + 1u) * (r + 1u) <= t * 100u) ++r;   /* sqrt(t / 100) x 100 */
            extra = (int32_t)(Q16(7.5) / 100 * (int32_t)r);
        }
        speed = pl->speed;
    } else {
        const sh2z_vehicle_t *rv = &f->veh[id];
        if (rv->flags & SH2Z_VEH_BOOST) { env = 1u; extra = Q16(5.3); }
        speed = rv->t.speed << 8;
    }
    /* (3 - (frame & 3)) x 2 x 0.2 flicker */
    const int32_t flicker = (int32_t)((3u - (fx_ticks & 3u)) * 2u) * Q16(0.2);
    const bool tail = c0.depth < NEAR_TAIL * 16384 && speed > Q16(1.0);
    for (int k = (int)b[0] - 1; k >= 0; --k) {
        const int16_t *q = b + 1 + 4 * k;
        const int32_t s = q[3] * Q16(0.35) / 16;                       /* size x 0.35 */
        int32_t p[3];
        /* model (x left, y up, z forward) x 0.1, z moved back by 3.6 s (model units) */
        const int32_t mx = q[0] * Q16(0.1), my = q[1] * Q16(0.1), mz = q[2] * Q16(0.1) - m16(s, Q16(0.36));
        for (int a = 0; a < 3; ++a)
            p[a] = o->pos[a] + (int32_t)(((int64_t)mx * o->basis[2][a] + (int64_t)my * o->basis[1][a] + (int64_t)mz * o->basis[0][a]) >> 14);
        sh2z_ep_point_t pt;
        if (!sh2z_ep_project(v, p, &pt)) continue;
        const int32_t size = m16(s + flicker + extra, hover_frac);
        const int32_t r = sh2z_ep_pixels(v, size, pt.depth) >> 16;
        if (r < 1) continue;
        const uint32_t colr = (uint32_t)art + FZX_FX_CLUT_BOOST + env * 32u;
        item_t *t = slot(pt.depth - 1);
        if (!t) return;
        if (sh2z_ep_emit_diamond(t->w, pt.x >> 16, pt.y >> 16, r, (uint32_t)art + FZX_FX_BOOST_HEAD, 16u, 16u, colr, pmod_tex)) {
            ++n_items; ++fzx_fx_report[11];
        }
        if (!tail) continue;
        int32_t e[3]; sh2z_ep_point_t pe;
        along(e, p, -m16(speed, Q16(0.9)), o->basis[0]);
        if (!sh2z_ep_project(v, e, &pe)) continue;
        const int16_t xy[4][2] = { { (int16_t)((pt.x >> 16) + r), (int16_t)(pt.y >> 16) },
                                   { (int16_t)(pe.x >> 16), (int16_t)(pe.y >> 16) },
                                   { (int16_t)((pt.x >> 16) - r), (int16_t)(pt.y >> 16) },
                                   { (int16_t)((pt.x >> 16) - r), (int16_t)(pt.y >> 16) } };
        t = slot(pt.depth);
        if (!t) return;
        if (sh2z_ep_emit_quad(t->w, xy, (uint32_t)art + FZX_FX_BOOST_TAIL, 16u, 16u, colr, pmod_tex_half)) {
            ++n_items; ++fzx_fx_report[11];
        }
    }
}

unsigned fzx_fx_emit(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view, const fzx_engine_cam_t *cam,
                     const fzx_field_car_t *cars, unsigned n) {
    if (fzx_fx_report[0] != 1) return 0;
    const uint16_t t0 = cpu_frt_count_get();
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
    n_items = 0; fzx_fx_report[11] = 0;
    /* The player's flash and low-energy flicker on the 3D body (racer.c 5795). */
    if (tick_player && player_max > 0) {
        uint8_t col[3] = { player_body[0], player_body[1], player_body[2] };
        const int32_t e = (int32_t)(((int64_t)player_energy << 16) / player_max);   /* Q16 share */
        if (!(tick_player->flags & SH2Z_VD_CRASHED)) {
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
    int32_t hover_frac = 65536;
    if (tick_player) {
        const int32_t h = tick_player->hover;
        hover_frac = h <= Q16(7.0) ? 0 : h >= Q16(13.0) ? 65536 : (int32_t)(((int64_t)(h - Q16(7.0)) << 16) / Q16(6.0));
    }
    /* By priority, while the budget lasts: the player's boosters, explosions, sparks, flying
     * sparks, debris, smoke, then the rivals' boosters. */
    for (unsigned k = 0; k < n; ++k) if (cars[k].id == 0u) boosters(&v, &cars[k], hover_frac, true);
    for (uint32_t k = 0; k < p_exp2.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_exp2, k);
        diamond(&v, e, FZX_FX_EXPLOSION + (uint32_t)(e->timer >> 2) * 512u, 32u, FZX_FX_CLUT_EXPLOSION + (uint32_t)(e->timer >> 1) * 32u,
                pmod_tex, INT32_MAX);
    }
    for (uint32_t k = 0; k < p_exp1.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_exp1, k);
        diamond(&v, e, FZX_FX_EXPLOSION + (uint32_t)(e->timer >> 1) * 512u, 32u, FZX_FX_CLUT_EXPLOSION + (uint32_t)e->timer * 32u,
                pmod_tex, INT32_MAX);
    }
    for (uint32_t k = 0; k < p_spark.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_spark, k);
        /* drawn falling: pos - 0.15 (timer - 1)^2 along the owner's up (here the player's) */
        sh2z_ep_particle_t d = *e;
        if (tick_player) {
            const int32_t drop = (int32_t)(e->timer - 1) * (int32_t)(e->timer - 1) * Q16(0.15);
            for (int a = 0; a < 3; ++a) d.pos[a] -= (int32_t)(((int64_t)drop * q14(tick_player->body[1][a])) >> 14);
        }
        diamond(&v, &d, FZX_FX_SPARK + (uint32_t)(e->timer >> 1) * 512u, 32u, FZX_FX_CLUT_SPARK + (uint32_t)e->timer * 32u,
                pmod_tex, NEAR_BOOST * 16384);
    }
    for (uint32_t k = 0; k < p_fly.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_fly, k);
        int32_t tail[3]; sh2z_ep_point_t a, b;
        for (int c = 0; c < 3; ++c) tail[c] = e->pos[c] - 3 * e->vel[c];
        if (!sh2z_ep_project(&v, e->pos, &a) || !sh2z_ep_project(&v, tail, &b)) continue;
        item_t *t = slot(a.depth);
        if (!t) break;
        const int16_t xy[4][2] = { { (int16_t)(a.x >> 16), (int16_t)(a.y >> 16) }, { (int16_t)(b.x >> 16), (int16_t)(b.y >> 16) },
                                   { 0, 0 }, { 0, 0 } };
        if (sh2z_ep_emit_shape(t->w, SH2Z_EP_LINE, xy, 0x821Fu, pmod_line, (uint32_t)art + FZX_FX_GOURAUD_SPARK)) ++n_items;
    }
    for (uint32_t k = 0; k < p_debris.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_debris, k);
        sh2z_ep_point_t pt;
        if (!sh2z_ep_project(&v, e->pos, &pt)) continue;
        const int32_t r = sh2z_ep_pixels(&v, e->scale, pt.depth) >> 16;
        if (r < 1) continue;
        item_t *t = slot(pt.depth);
        if (!t) break;
        /* A shard turning on itself: three corners at phases from its seed, 0xCC a tick (racer.c debris). */
        int16_t xy[4][2];
        const uint32_t ph = (uint32_t)e->seed * 16u + (uint32_t)e->timer * 0xCCu;
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = (ph + (uint32_t)c * 1365u + (uint32_t)(e->seed & 7u) * 97u * (uint32_t)c) & 4095u;
            const int32_t sx = sh2z_sin12_u30(a) >> 16, cx = sh2z_cos12_u30(a) >> 16;    /* Q14 */
            xy[c][0] = (int16_t)((pt.x >> 16) + ((r * cx) >> 14));
            xy[c][1] = (int16_t)((pt.y >> 16) + ((r * sx) >> 14) / (c == 1 ? 2 : 1));
        }
        xy[3][0] = xy[2][0]; xy[3][1] = xy[2][1];
        const uint32_t id = e->user < CARS ? e->user : 0u;
        const uint16_t colour = id ? rgb(fzx_fx_body[family_of(id)]) : rgb(player_body);
        /* alpha 255 - sin(vibration & 0x7FF) x 255: opaque or half-transparent */
        const bool half = (sh2z_sin12_u30(ph & 0x7ffu) >> 22) >= 128;
        if (sh2z_ep_emit_shape(t->w, SH2Z_EP_POLYGON, (const int16_t (*)[2])xy, colour, half ? pmod_poly_half : pmod_poly, 0u)) ++n_items;
    }
    for (uint32_t k = 0; k < p_smoke.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_smoke, k);
        diamond(&v, e, FZX_FX_SMOKE + (uint32_t)(e->timer >> 2) * 512u, 32u, FZX_FX_CLUT_SMOKE, pmod_tex_half, NEAR_BOOST * 16384);
    }
    {
        /* The rivals' boosters nearest first, so the budget drops the farthest. */
        static int32_t cd[CARS]; static uint8_t ck[CARS], co[CARS];
        unsigned m = 0;
        for (unsigned k = 0; k < n && m < CARS; ++k) {
            sh2z_ep_point_t pt;
            if (cars[k].id == 0u || !sh2z_ep_project(&v, cars[k].pos, &pt) || pt.depth > NEAR_BOOST * 16384) continue;
            cd[m] = pt.depth; ck[m] = (uint8_t)k; ++m;
        }
        sh2z_ep_sort(co, cd, m);
        for (unsigned j = m; j-- > 0;) boosters(&v, &cars[ck[co[j]]], hover_frac, false);
    }
    /* Far to near, after the cars. */
    static int32_t depth[FZX_FX_SLOTS];
    static uint8_t order[FZX_FX_SLOTS];
    for (unsigned i = 0; i < n_items; ++i) depth[i] = items[i].depth;
    sh2z_ep_sort(order, depth, n_items);
    unsigned out = 0;
    for (unsigned i = 0; i < n_items && out < capacity; ++i) memcpy(&commands[out++], items[order[i]].w, sizeof(vdp1_cmdt_t));
    fzx_fx_report[1] = out;
    if (out > fzx_fx_report[2]) fzx_fx_report[2] = out;
    fzx_fx_report[3] = p_spark.count + p_exp1.count + p_exp2.count + p_fly.count + p_smoke.count + p_debris.count;
    const uint32_t dt = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_fx_report[13] = dt;
    if (dt > fzx_fx_report[14]) fzx_fx_report[14] = dt;
    return out;
}
