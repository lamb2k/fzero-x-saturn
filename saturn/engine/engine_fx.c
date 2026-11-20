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
#include "sh2zam/divu_experimental.h"

volatile uint32_t fzx_fx_report[16];
volatile uint32_t fzx_fx_events[32];
volatile uint32_t fzx_fx_demand[2];
volatile uint32_t fzx_fx_shells;
volatile uint32_t fzx_fx_on_slave;    /* frames whose effects the slave emitted */      /* highlight shells drawn (cars x frames) */   /* commands the effects asked for: last frame, most in a frame */ /* tick << 8 | kind (1 retire, 2 destroyed, 3 spin-out start), last 32 */
static uint32_t n_events;
/* Diagnostic: master FRT totals and maxima per phase over the run: [0] set-up and body colour,
 * [1] the player's boosters, [2] particles, [3] the rivals' order, [4] the rivals' boosters,
 * [5] sort and copy, [6] fzx_fx_tick, [7] fzx_fx_player; maxima at [8 + k]. */
volatile uint32_t fzx_fx_prof[16];
/* Diagnostic: fzx_fx_tick's parts, FRT totals: [0] flashes and contacts, [1] the rivals, [2] the rings. */
volatile uint32_t fzx_fx_prof2[4];
static uint16_t prof2_t;
static void prof2(unsigned k) { const uint16_t now = cpu_frt_count_get(); fzx_fx_prof2[k] += (uint16_t)(now - prof2_t); prof2_t = now; }
static uint16_t prof_t;
static void prof(unsigned k) {
    const uint16_t now = cpu_frt_count_get(), d = (uint16_t)(now - prof_t);
    fzx_fx_prof[k] += d; if (d > fzx_fx_prof[8 + k]) fzx_fx_prof[8 + k] = d;
    prof_t = now;
}

#define Q16(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))
#define CARS FZX_FIELD_CARS
enum { WHITE_TICKS = 5, NEAR_TAIL = 400, NEAR_BOOST = 900, FAR_SPAWN = 2500 };

/* The source's rings (effects.c): sizes, lifetimes and growth; the smoke ring is 16 (the source's
 * 32; smoke comes one puff at a time). Debris: 30 a rival, 60 the player, into 128, as the source. */
static sh2z_ep_particle_t it_spark[32], it_exp1[32], it_exp2[8], it_fly[32], it_smoke[16], it_debris[128];
static sh2z_ep_pool_t p_spark, p_exp1, p_exp2, p_fly, p_smoke, p_debris;

typedef struct {
    int32_t yaw;          /* spin-out turn, angle12 x 65536 */
    int32_t steer;        /* Q16 spin steer (decays 0.99 a tick) */
    int32_t atk_angle, atk_rate; /* spin attack (vehicle-dynamics' rule) */
    uint8_t mode;         /* last seen FZX_ATK_FX_* bits */
    uint8_t white;        /* bodyWhiteTimer */
    int8_t side;
    uint8_t posed;
    uint8_t smoke_flash;  /* a wreck's smoke puff this frame: the yellow highlight (racer.c 5875) */
    uint8_t flare;        /* the boosters' flare after an accelerator press (racer.c unk_214: 27, -1 a tick) */
} car_fx_t;
static car_fx_t car[CARS];
static uint32_t whites, linger, seen_contacts;   /* bit i: car i flashes white / has an effect state running */
static uint32_t rng = 0x2f6b9e1du, fx_ticks;
static uintptr_t art;          /* VDP1 byte offset of FX.PAK */
static uintptr_t shell_clut;   /* VDP1 byte offset of the eight highlight colour tables (yellow .. red) */
enum { SHELL_BYTES = 8u * 32u };
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

/* 7.5 sqrt(t / 100), Q16, for the boost timer t (racer.c's booster size). */
static int32_t sqrt_share[101];

static void reset(void) {
    for (uint32_t t = 0, r = 0; t <= 100u; ++t) {
        while ((r + 1u) * (r + 1u) <= t * 100u) ++r;
        sqrt_share[t] = (int32_t)(Q16(7.5) / 100 * (int32_t)r);
    }
    (void)sh2z_ep_init(&p_spark, it_spark, 32u, 16u, Q16(1.05), 65536);
    (void)sh2z_ep_init(&p_exp1, it_exp1, 32u, 16u, Q16(1.02), 65536);
    (void)sh2z_ep_init(&p_exp2, it_exp2, 8u, 32u, Q16(1.02), 65536);
    (void)sh2z_ep_init(&p_fly, it_fly, 32u, 64u, 65536, 65536);
    (void)sh2z_ep_init(&p_smoke, it_smoke, 16u, 32u, Q16(1.02), 65536);
    (void)sh2z_ep_init(&p_debris, it_debris, 128u, 64u, 65536, Q16(0.98));
    memset(car, 0, sizeof(car));
    whites = 0; linger = 0; seen_contacts = fzx_atk_fx_contacts()->contacts;
    view_ok = false; player_posed = false; fx_ticks = 0; player_flags_prev = 0;
}

uint32_t fzx_fx_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes) {
    const uintptr_t tex = (uintptr_t)parts->texture_base;
    if (ceiling < FZX_FX_PACK_BYTES + SHELL_BYTES + 32u) { fzx_fx_report[0] = 3; return 0; }
    const uint32_t pack_at = (ceiling - FZX_FX_PACK_BYTES) & ~31u, floor = pack_at - SHELL_BYTES;
    if (((tex + floor) & 31u) != 0u) { fzx_fx_report[0] = 3; return 0; }
    if (!fzx_texture_cd_load("FX.PAK", FZX_FX_PACK_BYTES, FZX_FX_PACK_HASH, (void *)(tex + pack_at), scratch, scratch_bytes)) {
        fzx_fx_report[0] = 2; return 0;
    }
    art = (tex + pack_at) & 0x7ffffu;
    /* The highlight's colour tables: entry 0 stays transparent; 1..15 the colour (255, G, 0) with G
     * from 255 (yellow, table 0) to 0 (red, table 7): racer.c's attackHighlight colours. */
    {
        volatile uint16_t *t = (volatile uint16_t *)(tex + floor);
        for (uint32_t k = 0; k < 8u; ++k) {
            const uint32_t g = 255u - (255u * k + 3u) / 7u;
            for (uint32_t e = 0; e < 16u; ++e) t[k * 16u + e] = (uint16_t)(0x8000u | ((g >> 3) << 5) | 31u);
        }
        shell_clut = (tex + floor) & 0x7ffffu;
    }
    pmods();
    reset();
    fzx_closeup_body_get(player_body);
    fzx_fx_report[12] = (uint32_t)art;
    fzx_fx_report[0] = 1;
    return floor;
}

void fzx_fx_reset(void) {
    if (fzx_fx_report[0] != 1) return;
    reset();
    fzx_closeup_body_set(player_body);
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
    return d > 0 && d <= FAR_SPAWN * 16384 && proj1(&view_last, pos, &pt);
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
    car[owner].white = WHITE_TICKS; whites |= 1u << owner;
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
    car[id].smoke_flash = 1u;
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

/* The spin-out turn: atan(0.2 steer) a tick (x - x^3/3; x <= 0.2), steer x 0.99, while the
 * steer is above 0.1 (racer.c 4338: the basis turns only for |steer| > 0.1). No divides. */
static void spin_turn(car_fx_t *c) {
    if (!c->steer) return;
    if (c->steer <= Q16(0.1)) { c->steer = 0; return; }
    const int32_t x = m16(c->steer, Q16(0.2));
    const int32_t a = x - m16(m16(m16(x, x), x), Q16(1.0 / 3.0));
    c->yaw += ((a * 41722) >> 6) * c->side;   /* rad Q16 x 4096 / 2 pi (x 64, a <= 13,108) -> angle12 Q16 */
    c->steer = m16(c->steer, Q16(0.99));
}

void fzx_fx_tick(const sh2z_vd_body_t *player) {
    if (fzx_fx_report[0] != 1) return;
    prof_t = cpu_frt_count_get(); prof2_t = prof_t;
    tick_player = player;
    fzx_atk_fx_wall = rival_wall;
    ++fx_ticks;
    if (whites) {
        for (uint32_t i = 0; i < CARS; ++i) if (car[i].white && !--car[i].white) whites &= ~(1u << i);
    }
    /* Contacts that started this tick (body-contact-1: first contacts and the pair history); the
     * scan only on a tick whose contact count moved. */
    const sh2z_bc_field_t *bc = fzx_atk_fx_contacts();
    const bool fresh = bc->contacts != seen_contacts;
    seen_contacts = bc->contacts;
    for (uint32_t a = 0; fresh && a < bc->count; ++a) {
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
            if (spark(mid, vel, impact + Q16(12.0), b)) { car[a].white = car[b].white; whites |= 1u << a; }
        }
    }
    prof2(0);
    /* Rivals: spin-out, spin attacks, retirement, wrecks: those with an attack mode now or an
     * effect state still running (linger). */
    uint32_t todo = fzx_atk_fx_busy() | linger;
    linger = 0;
    while (todo) {
        const uint32_t i = (uint32_t)__builtin_ctz(todo);
        todo &= todo - 1u;
        if (i >= CARS) break;
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
        if ((mode & FZX_ATK_FX_CRASHED) && !(c->mode & FZX_ATK_FX_CRASHED)) blow_up(i, Q16(80.0), 30u, player);
        if ((mode & FZX_ATK_FX_GONE) && !(c->mode & FZX_ATK_FX_GONE)) blow_up(i, Q16(40.0), 30u, player);
        if (mode & FZX_ATK_FX_SPUN) spin_puffs(i, spin_t, player);
        if ((mode & FZX_ATK_FX_CRASHED) && !(mode & FZX_ATK_FX_GONE)) wreck_smoke(i, player);
        spin_turn(c);
        attack_motion(c);
        c->mode = (uint8_t)mode;
        if (c->mode || c->steer || c->atk_angle) linger |= 1u << i;
    }
    prof2(1);
    /* One step of every ring; gravity along the player's up (the source: each racer's own). */
    for (int c = 0; c < 3; ++c) {
        const int32_t up = q14(player->body[1][c]);
        p_fly.accel[c] = -(int32_t)(((int64_t)Q16(0.4) * up) >> 14);
        p_debris.accel[c] = -(int32_t)(((int64_t)Q16(0.2) * up) >> 14);
    }
    sh2z_ep_step(&p_spark); sh2z_ep_step(&p_exp1); sh2z_ep_step(&p_exp2);
    sh2z_ep_step(&p_fly); sh2z_ep_step(&p_smoke); sh2z_ep_step(&p_debris);
    prof2(2);
    prof(6);
}

void fzx_fx_player(const sh2z_vd_body_t *pl, int32_t max_energy, bool accel_pressed) {
    if (fzx_fx_report[0] != 1) return;
    prof_t = cpu_frt_count_get();
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
    if ((pl->flags & SH2Z_VD_CRASHED) && !(player_flags_prev & SH2Z_VD_CRASHED)) blow_up(0u, Q16(80.0), 60u, pl);
    if ((pl->flags & SH2Z_VD_CRASHED) && !(pl->flags & SH2Z_VD_FALLING)) wreck_smoke(0u, pl);
    c->atk_angle = pl->attack == SH2Z_VD_ATTACK_SPIN ? pl->attack_angle : 0;
    if (!(pl->flags & (SH2Z_VD_CRASHED | SH2Z_VD_SPINNING))) {   /* racer.c 4791..4811 */
        if (accel_pressed) c->flare = 27u; else if (c->flare) --c->flare;
    }
    player_energy = pl->energy; player_max = max_energy; player_flags_prev = pl->flags;
    prof(7);
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

/* The attack highlight (racer.c 5826..5880): spin attack (255, 255 - 255 s, 0) at 1.05 + 0.1 s,
 * s = sin(angle & 0x7FF); side attack red at 1.075; a wreck's smoke puff yellow at 1.03. */
bool fzx_fx_shell(uint32_t id, uint16_t *colr, uint16_t *rgb, int32_t *scale) {
    if (fzx_fx_report[0] != 1 || id >= CARS) return false;
    const car_fx_t *c = &car[id];
    uint32_t attack = 0; int32_t angle = 0; bool crashed;
    if (id == 0u) {
        const sh2z_vd_body_t *pl = tick_player;
        if (!pl) return false;
        crashed = (pl->flags & SH2Z_VD_CRASHED) != 0u;
        attack = pl->attack == SH2Z_VD_ATTACK_SPIN ? 2u : pl->attack == SH2Z_VD_ATTACK_SIDE ? 1u : 0u;
        angle = pl->attack_angle;
    } else {
        crashed = (c->mode & FZX_ATK_FX_CRASHED) != 0u;
        attack = (c->mode & FZX_ATK_FX_SPIN) ? 2u : (c->mode & FZX_ATK_FX_SIDE) ? 1u : 0u;
        angle = c->atk_angle;
    }
    uint32_t k; int32_t sc;
    if (crashed) {
        if (!c->smoke_flash) return false;
        k = 0u; sc = Q16(1.03);
    } else if (attack == 2u) {
        const int32_t s = sh2z_sin12_u30((uint32_t)angle & 0x7ffu) >> 14;   /* Q16, 0..1 */
        k = (uint32_t)((s * 7 + 32768) >> 16); sc = Q16(1.05) + m16(s, Q16(0.1));
    } else if (attack == 1u) {
        k = 7u; sc = Q16(1.075);
    } else return false;
    const uint32_t g = 255u - (255u * k + 3u) / 7u;
    *colr = (uint16_t)((shell_clut + k * 32u) >> 3);
    *rgb = (uint16_t)(0x8000u | ((g >> 3) << 5) | 31u);
    *scale = sc;
    ++fzx_fx_shells;
    return true;
}

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

static void diamond(const sh2z_ep_view_t *v, const sh2z_ep_particle_t *e, uint32_t pixels, uint32_t w, uint32_t clut,
                    uint16_t pmod, int32_t max_depth) {
    pt1_t pt;
    if (!proj1(v, e->pos, &pt) || pt.depth > max_depth) return;
    const int32_t r = px1(&pt, e->scale);
    if (r < 1) return;
    item_t *t = slot(pt.depth);
    if (t && sh2z_ep_emit_diamond(t->w, pt.x, pt.y, r, (uint32_t)art + pixels, w, w, (uint32_t)art + clut, pmod)) commit(t);
}

static uint16_t rgb(const uint8_t c[3]) {
    return (uint16_t)(0x8000u | ((uint32_t)(c[2] >> 3) << 10) | ((uint32_t)(c[1] >> 3) << 5) | (uint32_t)(c[0] >> 3));
}

/* The racers' boosters (Racer_Draw's booster pass). */
static void boosters(const sh2z_ep_view_t *v, const fzx_field_car_t *o, int32_t depth0, int32_t hover_frac, bool player_only) {
    const uint32_t id = o->id;
    const fzx_atk_field_t *f = fzx_atk_field();
    if (id == 0u) {
        const sh2z_vd_body_t *b = tick_player;
        if (!b || (b->flags & (SH2Z_VD_CRASHED | SH2Z_VD_SPINNING))) return;
    } else {
        int32_t st, dir;
        if (player_only || (fzx_atk_fx_mode(id, &st, &dir) & (FZX_ATK_FX_SPUN | FZX_ATK_FX_CRASHED))) return;
    }
    if (depth0 <= 0 || depth0 > NEAR_BOOST * 16384) return;
    const int16_t *b = fzx_fx_boosters[family_of(id)];
    uint32_t env = 0; int32_t extra = 0, speed = 0;
    if (id == 0u) {
        const sh2z_vd_body_t *pl = tick_player;
        if (pl->boost_timer > 0) {
            env = (pl->flags & SH2Z_VD_PLATE_BOOST) ? 2u : 1u;
            /* 7.5 sqrt(boostTimer / 100), boostTimer counting down from 100 */
            const uint32_t t = (uint32_t)pl->boost_timer > 100u ? 100u : (uint32_t)pl->boost_timer;
            extra = sqrt_share[t];
        }
        speed = pl->speed;
    } else {
        const sh2z_vehicle_t *rv = &f->veh[id];
        if (rv->flags & SH2Z_VEH_BOOST) { env = 1u; extra = Q16(5.3); }
        speed = rv->t.speed << 8;
    }
    /* (3 - (frame & 3)) x 2 x 0.2 flicker */
    int32_t fl = (int32_t)((3u - (fx_ticks & 3u)) * 2u);
    if (fl < car[id].flare) fl = car[id].flare;   /* the accelerator press (racer.c unk_214) */
    const int32_t flicker = fl * Q16(0.2);
    const bool tail = depth0 < NEAR_TAIL * 16384 && speed > Q16(1.0);
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
        const int32_t size = m16(s + flicker + extra, hover_frac);
        const int32_t r = px1(&pt, size);
        if (r < 1) continue;
        const uint32_t colr = (uint32_t)art + FZX_FX_CLUT_BOOST + env * 32u;
        item_t *t = slot(pt.depth - 1);
        if (t && sh2z_ep_emit_diamond(t->w, pt.x, pt.y, r, (uint32_t)art + FZX_FX_BOOST_HEAD, 16u, 16u, colr, pmod_tex)) {
            commit(t); ++boost_cmds;
        }
        if (!tail) continue;
        int32_t e[3]; pt1_t pe;
        along(e, p, -m16(speed, Q16(0.9)), o->basis[0]);
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
 * runs the emit (fzx_fx_job): the view, the cars and the boosters' hover share. */
static sh2z_ep_view_t snap_v;
static fzx_field_car_t snap_cars[CARS];
static unsigned snap_n;
static int32_t snap_hover;
static bool snap_ok;
/* The emit's results, written by its SH-2 and read by the master through the cache-through window. */
typedef struct { uint32_t n, demand, dropped, boost, live, ticks; } fx_out_t;
static fx_out_t job_out;

bool fzx_fx_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const fzx_field_car_t *cars, unsigned n) {
    snap_ok = false;
    if (fzx_fx_report[0] != 1) return false;
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
    if (tick_player && player_max > 0) {
        uint8_t col[3] = { player_body[0], player_body[1], player_body[2] };
        const int32_t e = sh2z_divu_div64((int64_t)player_energy << 16, player_max, NULL);   /* Q16 share */
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
        hover_frac = h <= Q16(7.0) ? 0 : h >= Q16(13.0) ? 65536 : m16(h - Q16(7.0), Q16(1.0 / 6.0));
    }
    snap_v = v; snap_hover = hover_frac;
    if (n > CARS) n = CARS;
    memcpy(snap_cars, cars, n * sizeof(cars[0])); snap_n = n;
    for (uint32_t i = 0; i < CARS; ++i) car[i].smoke_flash = 0u;   /* the shells have read it (fzx_drive_cars) */
    snap_ok = true;
    return true;
}
/* The effects' commands, far to near, from the staged frame. Either SH-2 (dual-claim-1 in
 * engine_main.c); writes only the emit's own state and job_out. */
unsigned fzx_fx_job(vdp1_cmdt_t *commands, unsigned capacity) {
    if (!snap_ok) { job_out.n = 0; return 0; }
    const uint16_t t0 = cpu_frt_count_get();
    const sh2z_ep_view_t v = snap_v;
    const fzx_field_car_t *const cars = snap_cars;
    const unsigned n = snap_n;
    const int32_t hover_frac = snap_hover;
    n_items = 0; demand = 0; dropped = 0; boost_cmds = 0;
    /* By priority, while the budget lasts: the player's boosters, explosions, sparks, flying
     * sparks, debris, smoke, then the rivals' boosters (the budget keeps the nearest). */
    for (unsigned k = 0; k < n; ++k) if (cars[k].id == 0u) boosters(&v, &cars[k], view_depth(&v, cars[k].pos), hover_frac, true);
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
        int32_t tail[3]; pt1_t a, b;
        for (int c = 0; c < 3; ++c) tail[c] = e->pos[c] - 3 * e->vel[c];
        if (!proj1(&v, e->pos, &a) || !proj1(&v, tail, &b)) continue;
        item_t *t = slot(a.depth);
        const int16_t xy[4][2] = { { (int16_t)a.x, (int16_t)a.y }, { (int16_t)b.x, (int16_t)b.y }, { 0, 0 }, { 0, 0 } };
        if (sh2z_ep_emit_shape(t->w, SH2Z_EP_LINE, xy, 0x821Fu, pmod_line, (uint32_t)art + FZX_FX_GOURAUD_SPARK)) commit(t);
    }
    for (uint32_t k = 0; k < p_debris.count; ++k) {
        const sh2z_ep_particle_t *e = sh2z_ep_at(&p_debris, k);
        pt1_t pt;
        if (!proj1(&v, e->pos, &pt)) continue;
        const int32_t r = px1(&pt, e->scale);
        if (r < 1) continue;
        item_t *t = slot(pt.depth);
        /* A shard turning on itself: three corners at phases from its seed, 0xCC a tick (racer.c debris). */
        int16_t xy[4][2];
        const uint32_t ph = (uint32_t)e->seed * 16u + (uint32_t)e->timer * 0xCCu;
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = (ph + (uint32_t)c * 1365u + (uint32_t)(e->seed & 7u) * 97u * (uint32_t)c) & 4095u;
            const int32_t sx = sh2z_sin12_u30(a) >> 16, cx = sh2z_cos12_u30(a) >> 16;    /* Q14 */
            xy[c][0] = (int16_t)(pt.x + ((r * cx) >> 14));
            xy[c][1] = (int16_t)(pt.y + ((r * sx) >> 14) / (c == 1 ? 2 : 1));
        }
        xy[3][0] = xy[2][0]; xy[3][1] = xy[2][1];
        const uint32_t id = e->user < CARS ? e->user : 0u;
        const uint16_t colour = id ? rgb(fzx_fx_body[family_of(id)]) : rgb(player_body);
        /* alpha 255 - sin(vibration & 0x7FF) x 255: opaque or half-transparent */
        const bool half = (sh2z_sin12_u30(ph & 0x7ffu) >> 22) >= 128;
        if (sh2z_ep_emit_shape(t->w, SH2Z_EP_POLYGON, (const int16_t (*)[2])xy, colour, half ? pmod_poly_half : pmod_poly, 0u)) commit(t);
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
            if (cars[k].id == 0u) continue;
            const int32_t d = view_depth(&v, cars[k].pos);   /* no divide: depth first */
            if (d <= v.near_z || d > NEAR_BOOST * 16384) continue;
            cd[m] = d; ck[m] = (uint8_t)k; ++m;
        }
        sh2z_ep_sort(co, cd, m);
            for (unsigned j = m; j-- > 0;) boosters(&v, &cars[ck[co[j]]], cd[co[j]], hover_frac, false);
    }
    /* Far to near, after the cars. */
    static int32_t depth[FZX_FX_SLOTS];
    static uint8_t order[FZX_FX_SLOTS];
    for (unsigned i = 0; i < n_items; ++i) depth[i] = items[i].depth;
    sh2z_ep_sort(order, depth, n_items);
    unsigned out = 0;
    for (unsigned i = 0; i < n_items && out < capacity; ++i) memcpy(&commands[out++], items[order[i]].w, sizeof(vdp1_cmdt_t));
    job_out.n = out; job_out.demand = demand; job_out.dropped = dropped; job_out.boost = boost_cmds;
    job_out.live = p_spark.count + p_exp1.count + p_exp2.count + p_fly.count + p_smoke.count + p_debris.count;
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
    fzx_fx_demand[0] = o->demand; if (o->demand > fzx_fx_demand[1]) fzx_fx_demand[1] = o->demand;
    if (slave) ++fzx_fx_on_slave; else { fzx_fx_report[13] = o->ticks; if (o->ticks > fzx_fx_report[14]) fzx_fx_report[14] = o->ticks; }
}
