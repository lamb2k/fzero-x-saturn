/* Engine attacks, racer-racer contact, damage and KOs (see engine_attacks.h). Plain C. */
#include <string.h>
#include "engine_attacks.h"
#include "attacks_tables.h"
#ifdef ATK_PROF
#include <time.h>
uint64_t atk_prof[8];
static uint64_t pn(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
#define PROF(k) do { const uint64_t q_ = pn(); atk_prof[k] += q_ - prof_t; prof_t = q_; } while (0)
#else
#define PROF(k) do { } while (0)
#endif

volatile uint32_t fzx_atk_difficulty = 1, fzx_atk_vis_gate = 1, fzx_atk_ai = 1;
volatile uint32_t fzx_atk_report[16];
volatile uint32_t fzx_atk_dmg_src[3][2];
fzx_atk_ko_t fzx_atk_kos[FZX_ATK_KO_LOG];
uint8_t fzx_atk_ko_count[FZX_ATK_MAX];
int32_t fzx_atk_retire_tick[FZX_ATK_MAX];
uint8_t fzx_atk_retire_place[FZX_ATK_MAX];
void (*volatile fzx_atk_ko_hook)(uint32_t victim);
uint32_t fzx_atk_chk[FZX_ATK_CHECKPOINTS];

#define Q8(x) ((int32_t)((x) * 256.0 + ((x) >= 0 ? 0.5 : -0.5)))
#define Q16(x) ((int32_t)((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))

/* The original's numbers (racer.c, cpu.c; generated/ATTACKS-PLAN.md section 1). */
enum {
    REACH_LONG_Q8 = 92 * 256,     /* along-line pair gap tested */
    PRE_Q8 = 92 * 256,            /* line-space box around a body that can hold a contact (reach 46, the lines' 15-unit mismatch) */
    HIT_TICKS = 60,               /* unk_208: no grip after an attack lands */
    CREDIT_TICKS = 60,            /* func_8008DBB8(victim, 60) */
    SPIN_OUT_TICKS = 150,         /* spinOutTimer */
    SIDE_TICKS = 15,              /* side attack (E1 P07) */
    SPIN_TICKS = 112,             /* spin attack (E1 P08) */
    FREE_TICKS = 30,              /* free (slide) state after an attack (grip_tap) */
    DOUBLE_TAP = 15,              /* zButtonTimer / rButtonTimer window */
    THINK_AFTER_TICKS = 60,       /* raceTime > 1000 ms */
    REVERSE_TICKS = 30,           /* gPlayerReverseTimer > 30 */
    REVERSE_AFTER_TICKS = 600     /* raceTime > 10000 ms */
};
/* Line-space body of a rival driven here: lateral in Q8 units, speeds Q8 per tick. */
static const int32_t SIDE_FORCE = Q8(5.0), SIDE_FADE = Q16(0.8333333), SPIN_FORCE = Q8(0.5), SPIN_FADE = Q16(0.25);
static const int32_t SLIDE_CUT = Q8(0.38), GRIP_CUT = Q8(1.25);
static const int32_t KEEP_SPIN = Q16(0.997), KEEP_CRASHED = Q16(0.98305196), WALL_KEEP = Q16(0.998);
static const int32_t BOUNCE = Q16(1.7), BOUNCE_SPIN = Q16(2.0), WALL_DAMAGE = Q16(0.7), WALL_MIN = Q8(0.01);
static const int32_t DESTROY_DAMAGE = Q16(25.0), OUT_OF_VIEW_ENERGY = Q8(0.1);
static const int32_t VIEW_FAR = Q16(2500.0);
/* The visibility stand-in: engine_drive.c's chase camera (eye 98.869 back, 60.294 up; pitched down
 * by atan(84.466 / 123.059)) and a fixed view of tan 0.75 across, 0.5625 up. */
static const int32_t EYE_F = Q16(-98.869), EYE_U = Q16(60.294);
static const int32_t PITCH_C = (int32_t)(0.824477 * 1073741824.0), PITCH_S = (int32_t)(0.565912 * 1073741824.0);
static const int32_t TAN_X = Q16(0.75), TAN_Y = Q16(0.5625);

enum { M_EXT = 1, M_SIDE = 2, M_SPIN = 4, M_KNOCK = 8, M_SPUN = 16, M_CRASHED = 32, M_GONE = 64 };
typedef struct {
    uint8_t mode;
    int8_t dir;          /* attack side: +1 right, -1 left */
    int8_t drift;        /* driftingCounter: +-5 .. 0, moved on think ticks */
    uint8_t character;
    int16_t attack_t, hit_t, free_t, spin_t;
    int32_t lv;          /* lateral velocity, Q8 per tick, + right */
    int32_t push_fwd, push_lat; /* this tick's contact, Q8 */
    uint32_t ext_from;   /* field tick when driven here began */
    uint32_t last_press; /* field tick of the last attack button press (double tap) */
    uint32_t strength;
} rival_t;

static fzx_atk_field_t F;
static rival_t R[FZX_ATK_MAX];
static sh2z_bc_body_t body[FZX_ATK_MAX];
static uint32_t last_touch[FZX_ATK_MAX * (FZX_ATK_MAX - 1u) / 2u];
static sh2z_bc_field_t bcf;
static sh2z_bc_rules_t rules;
static uint8_t order[FZX_ATK_MAX];
static int16_t pose_rows[FZX_ATK_MAX][3][3];
static uint8_t cand_list[FZX_ATK_MAX], prev_push[FZX_ATK_MAX];
static uint32_t ncand, nprev_push;
static int32_t along[FZX_ATK_MAX];
static uint32_t rng, n_kos, reverse_run;
static int32_t regain_pending, player_max_q8;
static bool live;
static uint32_t hunt_tap_at, hunt_cool;
static int hunt_dir;

static int32_t m16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + 0x8000) >> 16); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
static int32_t total_of(const sh2z_vehicle_t *v) { return v->t.lap * F.line->length + v->t.dist; }
static uint32_t race_ticks(void) { return F.field->started ? F.field->tick - F.field->start_tick : 0u; }

const fzx_atk_field_t *fzx_atk_field(void) { return &F; }
bool fzx_atk_external(uint32_t i) { return i < F.count && (R[i].mode & M_EXT) != 0; }

void fzx_atk_init(const fzx_atk_field_t *f, uint32_t seed) {
    F = *f;
    memset(R, 0, sizeof(R));
    memset((void *)fzx_atk_report, 0, sizeof(fzx_atk_report));
    memset((void *)fzx_atk_dmg_src, 0, sizeof(fzx_atk_dmg_src));
    memset(fzx_atk_kos, 0, sizeof(fzx_atk_kos));
    memset(fzx_atk_ko_count, 0, sizeof(fzx_atk_ko_count));
    for (uint32_t i = 0; i < FZX_ATK_MAX; ++i) { fzx_atk_retire_tick[i] = -1; fzx_atk_retire_place[i] = 0; }
    memset(body, 0, sizeof(body));
    for (uint32_t i = 0; i < F.count; ++i) {
        uint32_t c = (uint32_t)F.config[i * F.config_stride + 5u];
        if (c >= 30u) c = 0u;
        R[i].character = (uint8_t)c;
        R[i].strength = fzx_atk_machine[c][0];
        R[i].last_press = 0xFFFFFF00u;
        body[i].strength = R[i].strength;
    }
    player_max_q8 = F.config[3];
    sh2z_bc_field_init(&bcf, body, last_touch, F.count);
    memset(&rules, 0, sizeof(rules));
    rules.reach_long = REACH_LONG_Q8; rules.reach = Q16(46.0); rules.near = Q16(0.01);
    rules.impact_gain = Q16(1.1); rules.attack_push = Q16(3.0); rules.attack_min = Q16(3.0);
    rules.weak_share = 21823; rules.heavy_gain = Q16(10.0); rules.credit_window = CREDIT_TICKS;
    rng = seed * 2654435761u + 0x5bd1e995u;
    if (!rng) rng = 1u;
    n_kos = 0; reverse_run = 0; regain_pending = 0;
    hunt_tap_at = 0; hunt_cool = 0; hunt_dir = 0;
    ncand = 0; nprev_push = 0;
    memset(fzx_atk_chk, 0, sizeof(fzx_atk_chk));
    live = F.count >= 2u && F.count <= FZX_ATK_MAX;
}

/* ---- visibility (the stand-in for machine LOD != 0) ---------------------------------------- */
bool fzx_atk_visible(const sh2z_vd_body_t *p, uint32_t i) {
    if (!fzx_atk_vis_gate) return true;
    int32_t pos[3]; int16_t rows[3][3];
    F.pose(&F.veh[i].t, pos, rows);
    const int32_t *f = p->body[0], *u = p->body[1];
    int32_t eye[3], cf[3], cu[3], cr[3];
    for (int c = 0; c < 3; ++c) {
        eye[c] = p->pos[c] + mul30(EYE_F, f[c]) + mul30(EYE_U, u[c]);
        cf[c] = mul30(PITCH_C, f[c]) - mul30(PITCH_S, u[c]);
        cu[c] = mul30(PITCH_S, f[c]) + mul30(PITCH_C, u[c]);
    }
    cr[0] = (int32_t)(((int64_t)cf[1] * cu[2] - (int64_t)cf[2] * cu[1]) >> 30);
    cr[1] = (int32_t)(((int64_t)cf[2] * cu[0] - (int64_t)cf[0] * cu[2]) >> 30);
    cr[2] = (int32_t)(((int64_t)cf[0] * cu[1] - (int64_t)cf[1] * cu[0]) >> 30);
    int64_t z = 0, x = 0, y = 0;
    for (int c = 0; c < 3; ++c) {
        const int64_t d = (int64_t)pos[c] - eye[c];
        z += d * cf[c]; x += d * cr[c]; y += d * cu[c];
    }
    z >>= 30; x >>= 30; y >>= 30;                 /* Q16 */
    if (z <= 0 || z > VIEW_FAR) return false;
    const int64_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    return ax * 65536 <= z * TAN_X && ay * 65536 <= z * TAN_Y;
}

/* ---- damage ------------------------------------------------------------------------------- */
static void record_retire(uint32_t i) {
    if (fzx_atk_retire_tick[i] >= 0) return;
    fzx_atk_retire_tick[i] = (int32_t)race_ticks();
    fzx_atk_retire_place[i] = (uint8_t)(F.veh[i].rank + 1u);
}

/* The player's state as a KO's attacker: not finished, crashed or spinning out. */
static bool player_can_score(const sh2z_vd_body_t *p) {
    return !(p->flags & (SH2Z_VD_FINISHED | SH2Z_VD_CRASHED | SH2Z_VD_SPINNING)) && (p->flags & SH2Z_VD_ACTIVE);
}

static void start_spin_out(const sh2z_vd_body_t *p, uint32_t i) {
    rival_t *r = &R[i];
    sh2z_vehicle_t *v = &F.veh[i];
    v->energy = 0;
    if (r->mode & (M_SPUN | M_CRASHED)) return;
    if (!(r->mode & M_EXT)) { r->lv = v->t.lat_vel; r->ext_from = F.field->tick; }
    r->mode = (uint8_t)((r->mode | M_EXT | M_SPUN) & ~(M_SIDE | M_SPIN | M_KNOCK));
    r->attack_t = 0; r->spin_t = 1; r->drift = 0;
    v->flags |= SH2Z_VEH_EXTERNAL;
    v->flags &= (uint16_t)~SH2Z_VEH_BOOST;
    ++fzx_atk_report[4];
    /* KO credit at the start of the spin-out (func_8008DBB8). */
    if (sh2z_bc_credit(&bcf, i, &rules) == 0 && player_can_score(p)) {
        if (n_kos < FZX_ATK_KO_LOG) {
            fzx_atk_kos[n_kos].tick = race_ticks(); fzx_atk_kos[n_kos].victim = (uint8_t)i; fzx_atk_kos[n_kos].attacker = 0;
        }
        ++n_kos;
        if (fzx_atk_ko_count[0] < 255u) ++fzx_atk_ko_count[0];
        ++fzx_atk_report[3];
        regain_pending += (player_max_q8 << 8) / 8;   /* Q16: energyRegain += maxEnergy * 0.125 */
        if (fzx_atk_ko_hook) fzx_atk_ko_hook(i);
    }
}

/* Damage (Q16) to rival i (Racer_ReceiveDamage). */
static void damage_rival(const sh2z_vd_body_t *p, uint32_t i, int32_t amount) {
    rival_t *r = &R[i];
    sh2z_vehicle_t *v = &F.veh[i];
    if (amount <= 0) return;
    if (r->mode & M_CRASHED) {
        if (amount > DESTROY_DAMAGE && !(r->mode & M_GONE)) r->mode |= M_GONE;   /* the wreck leaves contact */
        return;
    }
    if (v->flags & SH2Z_VEH_FINISHED) return;
    v->energy -= (amount + 128) >> 8;
    if (v->energy < 0) {
        if (r->mode & M_SPUN) { v->energy = 0; return; }
        if (!fzx_atk_visible(p, i)) { v->energy = OUT_OF_VIEW_ENERGY; ++fzx_atk_report[9]; return; }
        start_spin_out(p, i);
    }
}

void fzx_atk_test_damage(const sh2z_vd_body_t *p, uint32_t i, int32_t amount) {
    if (live && i > 0u && i < F.count) damage_rival(p, i, amount);
}

/* ---- contact ------------------------------------------------------------------------------- */
void fzx_atk_contact(const sh2z_vd_body_t *p, sh2z_vd_contact_t *out) {
    memset(out, 0, sizeof(*out));
    if (!live) return;
    out->regain = regain_pending; regain_pending = 0;
#ifdef ATK_PROF
    uint64_t prof_t = pn();
#endif
    sh2z_bc_field_begin(&bcf);
    PROF(0);
    const uint32_t n = F.count;
    const int32_t length = F.line->length, half = length / 2;
    /* Physical order by current distance (the field's order, nearly sorted, fixed by insertion). */
    for (uint32_t s = 0; s < n; ++s) { order[s] = F.field->order[s]; }
    for (uint32_t i = 0; i < n; ++i) along[i] = F.veh[i].t.dist;
    for (uint32_t s = 1; s < n; ++s) {
        const uint8_t k = order[s]; uint32_t j = s;
        while (j > 0 && (along[order[j - 1]] < along[k] || (along[order[j - 1]] == along[k] && order[j - 1] > k))) { order[j] = order[j - 1]; --j; }
        order[j] = k;
    }
    PROF(1);
    /* Candidates: another vehicle within PRE_Q8 along and across the line. Two bodies in reach (46)
     * are within it on any part of the course, so only candidates get a world pose and enter the walk;
     * a pair that leaves it simply loses its contact history, as one out of reach does. Only vehicles
     * current on this tick take part (the player, rivals driven here, and agents the field stepped on
     * its last tick): far from the player the field's level of detail steps agents every 2, 4 or 8
     * ticks, and a pair is resolved on the ticks both are current, as line-field-1's own contact did. */
    uint8_t cand[FZX_ATK_MAX], cur[FZX_ATK_MAX];
    memset(cand, 0, sizeof(cand));
    for (uint32_t i = 0; i < n; ++i)
        cur[i] = (uint8_t)(i == 0u || (F.veh[i].flags & SH2Z_VEH_EXTERNAL) || F.brain[i].last_tick == F.field->tick);
    for (uint32_t s = 0; s < n; ++s) {
        const uint32_t a = order[s];
        if (!cur[a]) continue;
        for (uint32_t k = 1; k < n; ++k) {
            const uint32_t b = order[s + k < n ? s + k : s + k - n];
            int32_t gap = along[a] - along[b];
            if (gap > half) gap -= length;
            else if (gap <= -half) gap += length;
            if (gap > PRE_Q8) break;
            if (gap < 0 || !cur[b]) continue;
            if (iabs(F.veh[a].t.lateral - F.veh[b].t.lateral) <= PRE_Q8) cand[a] = cand[b] = 1;
        }
    }
    PROF(2);
    /* Last tick's candidates leave the walk; this tick's enter it. */
    for (uint32_t ci = 0; ci < ncand; ++ci) body[cand_list[ci]].flags = 0;
    ncand = 0;
    for (uint32_t i = 0; i < n; ++i) if (cand[i]) cand_list[ncand++] = (uint8_t)i;
    for (uint32_t ci = 0; ci < ncand; ++ci) {
        const uint32_t i = cand_list[ci];
        sh2z_bc_body_t *b = &body[i];
        b->flags = 0;
        if (i == 0) {
            if (!(p->flags & SH2Z_VD_ACTIVE)) continue;
            for (int c = 0; c < 3; ++c) { b->pos[c] = p->pos[c]; b->vel[c] = p->vel[c]; }
            b->flags = SH2Z_BC_ACTIVE | SH2Z_BC_HEAVY | SH2Z_BC_CREDIT;
            if (p->attack != SH2Z_VD_ATTACK_NONE) b->flags |= SH2Z_BC_ATTACK;
            if (p->flags & SH2Z_VD_CRASHED) b->flags |= SH2Z_BC_WEAK;
            continue;
        }
        const rival_t *r = &R[i];
        const sh2z_vehicle_t *v = &F.veh[i];
        if ((r->mode & M_GONE) || (v->flags & SH2Z_VEH_STOPPED)) continue;
        int16_t (*rows)[3] = pose_rows[i];
        F.pose(&v->t, b->pos, rows);
        const int32_t lv = (r->mode & M_EXT) ? r->lv : v->t.lat_vel;
        for (int c = 0; c < 3; ++c)   /* forward x speed, right (= -left) x lateral speed; Q14 x Q8 -> Q16 */
            b->vel[c] = (int32_t)(((int64_t)rows[0][c] * v->t.speed - (int64_t)rows[2][c] * lv) >> 6);
        b->flags = SH2Z_BC_ACTIVE;
        if (r->mode & (M_SIDE | M_SPIN)) b->flags |= SH2Z_BC_ATTACK;
        if (r->mode & M_CRASHED) b->flags |= SH2Z_BC_WEAK;
    }
    PROF(3);
    const uint32_t touching = sh2z_bc_field_resolve(&bcf, order, along, length, &rules);
    PROF(4);
    fzx_atk_report[1] = bcf.tested; fzx_atk_report[2] = touching; fzx_atk_report[0] = bcf.contacts;
    /* The player: everything through its own step. */
    out->push[0] = body[0].push[0]; out->push[1] = body[0].push[1]; out->push[2] = body[0].push[2];
    out->damage = body[0].damage; out->hit = body[0].hit;
    fzx_atk_report[10] += (uint32_t)((body[0].damage + 128) >> 8);
    /* Rivals: damage now (before anyone moves, as the original), the push in this tick's move. */
    for (uint32_t ci = 0; ci < nprev_push; ++ci) R[prev_push[ci]].push_fwd = R[prev_push[ci]].push_lat = 0;
    nprev_push = 0;
    for (uint32_t ci = 0; ci < ncand; ++ci) {
        const uint32_t i = cand_list[ci];
        if (i == 0u) continue;
        rival_t *r = &R[i];
        const sh2z_bc_body_t *b = &body[i];
        r->push_fwd = r->push_lat = 0;
        if (!b->flags) continue;
        prev_push[nprev_push++] = (uint8_t)i;
        if (b->push[0] | b->push[1] | b->push[2]) {
            int16_t (*rows)[3] = pose_rows[i];   /* this tick's pose, from above */
            int64_t pf = 0, pl = 0;
            for (int c = 0; c < 3; ++c) { pf += (int64_t)rows[0][c] * b->push[c]; pl -= (int64_t)rows[2][c] * b->push[c]; }
            r->push_fwd = (int32_t)(pf >> 22);   /* Q14 x Q16 -> Q8 */
            r->push_lat = (int32_t)(pl >> 22);
        }
        if (b->hit && !(r->mode & (M_SPUN | M_CRASHED))) {
            ++fzx_atk_report[8];
            if (body[0].flags & SH2Z_BC_ATTACK) ++fzx_atk_report[12];
            if (!(r->mode & M_EXT)) { r->lv = F.veh[i].t.lat_vel; r->ext_from = F.field->tick; }
            r->mode |= M_EXT | M_KNOCK;
            r->hit_t = HIT_TICKS;
            F.veh[i].flags |= SH2Z_VEH_EXTERNAL;
        }
        if (b->damage > 0) {
            const int src = b->hit || (b->flags & SH2Z_BC_ATTACK) ? 1 : 0;
            fzx_atk_dmg_src[src][0]++; fzx_atk_dmg_src[src][1] += (uint32_t)((b->damage + 128) >> 8);
        }
        damage_rival(p, i, b->damage);
    }
    PROF(5);
}

/* ---- rivals driven here -------------------------------------------------------------------- */
static void start_attack(uint32_t i, bool spin, int dir) {
    rival_t *r = &R[i];
    sh2z_vehicle_t *v = &F.veh[i];
    if (r->mode & (M_SIDE | M_SPIN | M_SPUN | M_CRASHED)) return;
    if (!(r->mode & M_EXT)) { r->lv = v->t.lat_vel; r->ext_from = F.field->tick; }
    r->mode |= M_EXT | (spin ? M_SPIN : M_SIDE);
    r->attack_t = spin ? SPIN_TICKS : SIDE_TICKS;
    r->free_t = FREE_TICKS;
    r->dir = (int8_t)(dir < 0 ? -1 : 1);
    v->flags |= SH2Z_VEH_EXTERNAL;
    ++fzx_atk_report[spin ? 7 : 6];
}

static void to_agent(uint32_t i) {
    rival_t *r = &R[i];
    sh2z_vehicle_t *v = &F.veh[i];
    sh2z_field_brain_t *b = &F.brain[i];
    const uint32_t away = F.field->tick - r->ext_from;
    r->mode = 0; r->lv = 0;
    v->flags &= (uint16_t)~SH2Z_VEH_EXTERNAL;
    b->expect += b->pace * (int32_t)away;   /* the field did not run its expectation meanwhile */
    b->last_tick = F.field->tick;           /* one tick due at the next field step */
    b->target_lat = v->t.lateral;
    b->pass_ticks = 0; b->pass_dir = 0;
}

static void move_external(const sh2z_vd_body_t *p, uint32_t i) {
    rival_t *r = &R[i];
    sh2z_vehicle_t *v = &F.veh[i];
    sh2z_track_state_t *t = &v->t;
    int32_t lv = r->lv;
    /* Speed: coasting while spinning out or crashed; otherwise the engine holds the agent's target. */
    if (r->mode & M_CRASHED) t->speed = m16(t->speed, KEEP_CRASHED), lv = m16(lv, KEEP_CRASHED);
    else if (r->mode & M_SPUN) t->speed = m16(t->speed, KEEP_SPIN), lv = m16(lv, KEEP_SPIN);
    else {
        const int32_t want = F.brain[i].target_speed;
        if (t->speed < want) { t->speed += v->accel; if (t->speed > want) t->speed = want; }
    }
    /* Attack force across the line, fading with the speed already going that way. */
    if (r->mode & (M_SIDE | M_SPIN)) {
        const int32_t into = r->dir > 0 ? lv : -lv;
        const int32_t force = (r->mode & M_SIDE) ? SIDE_FORCE - m16(into < 0 ? 0 : into, SIDE_FADE)
                                                 : SPIN_FORCE - m16(into < 0 ? 0 : into, SPIN_FADE);
        if (into < 0) lv += r->dir * ((r->mode & M_SIDE) ? SIDE_FORCE : SPIN_FORCE);
        else if (force > 0) lv += r->dir * force;
    }
    /* Grip takes the side speed away: the slide rate while attacking, hit or just after; else grip. */
    if (!(r->mode & (M_SPUN | M_CRASHED))) {
        const int32_t cut = (r->mode & (M_SIDE | M_SPIN | M_KNOCK)) || r->free_t ? SLIDE_CUT : GRIP_CUT;
        if (lv > cut) lv -= cut; else if (lv < -cut) lv += cut; else lv = 0;
    }
    /* This tick's contact push. */
    lv += r->push_lat;
    t->speed += r->push_fwd;
    if (t->speed < 0) t->speed = 0;
    /* Across the line, then the walls (Racer_HitWall: back inside, bounce, keep 0.998, damage). */
    sh2z_line_sample_t at;
    sh2z_line_sample(F.line, t->node, t->dist, &at);
    int32_t lat = t->lateral + lv;
    const int32_t hw = at.half_width;
    if (lat > hw || lat < -hw) {
        const int32_t into = lat > hw ? lv : -lv;
        lat = lat > hw ? hw : -hw;
        if (into > 0) {
            lv -= m16(lv, (r->mode & M_SPUN) ? BOUNCE_SPIN : BOUNCE);
            t->speed = m16(t->speed, WALL_KEEP);
            ++fzx_atk_report[13];
            if (into > WALL_MIN) {
                fzx_atk_dmg_src[2][0]++; fzx_atk_dmg_src[2][1] += (uint32_t)((m16(into << 8, WALL_DAMAGE) + 128) >> 8);
                damage_rival(p, i, m16(into << 8, WALL_DAMAGE));
            }
        }
    }
    t->lateral = lat;
    t->lat_vel = lv;
    r->lv = lv;
    sh2z_track_advance(F.line, t, 1u);
    /* Timers. */
    if (r->attack_t && --r->attack_t == 0) r->mode &= (uint8_t)~(M_SIDE | M_SPIN);
    if (r->hit_t && --r->hit_t == 0) r->mode &= (uint8_t)~M_KNOCK;
    if (!(r->mode & (M_SIDE | M_SPIN)) && r->free_t) --r->free_t;
    if ((r->mode & M_SPUN) && ++r->spin_t > SPIN_OUT_TICKS) {
        /* Crashed and retired (Racer_RetireRacer), with the place it held a moment ago. */
        r->mode = (uint8_t)((r->mode & ~M_SPUN) | M_CRASHED);
        r->spin_t = 0;
        if (!(v->flags & (SH2Z_VEH_RETIRED | SH2Z_VEH_FINISHED))) {
            record_retire(i);
            v->flags |= SH2Z_VEH_RETIRED;
            F.brain[i].retire_tick = (int32_t)race_ticks();
            ++F.field->retired;
            ++fzx_atk_report[5];
        }
    }
    if ((r->mode & M_CRASHED) && t->speed == 0) v->flags |= SH2Z_VEH_STOPPED;
    /* Back to the field when nothing keeps it here. */
    if (!(r->mode & (M_SIDE | M_SPIN | M_KNOCK | M_SPUN | M_CRASHED)) && !r->free_t && iabs(lv) <= GRIP_CUT) to_agent(i);
}

void fzx_atk_move(const sh2z_vd_body_t *p) {
    if (!live) return;
    if ((p->flags & SH2Z_VD_CRASHED) && fzx_atk_retire_tick[0] < 0) record_retire(0);
    uint32_t ext = 0;
    for (uint32_t i = 1; i < F.count; ++i) {
        rival_t *r = &R[i];
        if (r->mode & M_EXT) { move_external(p, i); ext += (r->mode & M_EXT) != 0; continue; }
        if (!(r->push_fwd | r->push_lat)) continue;
        /* An agent bumped: the push moves it this tick; its follower steers back. */
        sh2z_vehicle_t *v = &F.veh[i];
        sh2z_line_sample_t at;
        sh2z_line_sample(F.line, v->t.node, v->t.dist, &at);
        int32_t lat = v->t.lateral + r->push_lat;
        if (lat > at.half_width || lat < -at.half_width) {
            const int32_t into = iabs(r->push_lat);
            lat = lat > 0 ? at.half_width : -at.half_width;
            ++fzx_atk_report[13];
            if (into > WALL_MIN) {
                fzx_atk_dmg_src[2][0]++; fzx_atk_dmg_src[2][1] += (uint32_t)((m16(into << 8, WALL_DAMAGE) + 128) >> 8);
                damage_rival(p, i, m16(into << 8, WALL_DAMAGE));
            }
        }
        v->t.lateral = lat;
        v->t.speed += r->push_fwd;
        if (v->t.speed < 0) v->t.speed = 0;
    }
    fzx_atk_report[11] = ext;
}

/* ---- CPU attack decisions (cpu.c) ---------------------------------------------------------- */
static int32_t near_gap(const sh2z_vehicle_t *a, const sh2z_vehicle_t *b) {  /* a ahead of b: positive */
    int32_t g = a->t.dist - b->t.dist;
    const int32_t L = F.line->length;
    if (g > L / 2) g -= L; else if (g < -L / 2) g += L;
    return g;
}

/* One button press of a side attack (Z or R): a second press within the double-tap window starts it. */
static void press(uint32_t i, int dir) {
    rival_t *r = &R[i];
    const uint32_t now = F.field->tick;
    if (now - r->last_press < DOUBLE_TAP) start_attack(i, false, dir);
    r->last_press = now;
}

static bool road_wide(const sh2z_vehicle_t *v) {
    sh2z_line_sample_t at;
    sh2z_line_sample(F.line, v->t.node, v->t.dist, &at);
    return at.half_width * 2 > 138 * 256;
}

static void think(const sh2z_vd_body_t *p, uint32_t i) {
    rival_t *r = &R[i];
    sh2z_vehicle_t *v = &F.veh[i];
    const sh2z_field_brain_t *b = &F.brain[i];
    if (v->flags & (SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED) || (r->mode & (M_SPUN | M_CRASHED))) return;
    const sh2z_vehicle_t *pl = &F.veh[0];
    const int32_t self_total = total_of(v), player_total = total_of(pl);
    const uint32_t diff = fzx_atk_difficulty > 3u ? 3u : fzx_atk_difficulty;
    const uint32_t pers = fzx_atk_machine[r->character][5];
    const bool aware = pers < 2u;
    const uint32_t chance_side = fzx_atk_chance[(pers * 4u + diff) % (FZX_ATK_PERSONALITIES * 4u)][0];
    const uint32_t chance_spin = fzx_atk_chance[(pers * 4u + diff) % (FZX_ATK_PERSONALITIES * 4u)][1];
    const uint32_t race = race_ticks();
    bool vis = false, vis_known = false;
#define VIS() (vis_known ? vis : (vis_known = true, vis = fzx_atk_visible(p, i)))
    bool wide = false, wide_known = false;
#define WIDE() (wide_known ? wide : (wide_known = true, wide = road_wide(v)))
    /* Chasers on their target. */
    if (b->chase >= 0 && (uint32_t)b->chase < F.count && race > THINK_AFTER_TICKS && WIDE() &&
        !(r->mode & (M_SIDE | M_SPIN))) {
        const sh2z_vehicle_t *c = &F.veh[b->chase];
        if (!(c->flags & (SH2Z_VEH_RETIRED | SH2Z_VEH_FINISHED)) && iabs(near_gap(c, v)) < 184 * 256 && VIS()) {
            const uint32_t u = rnd() & 0x7fffu;
            if (iabs(c->t.lateral - v->t.lateral) < 92 * 256) {
                const bool cpu_target = b->chase != 0;
                if ((cpu_target && u < 4915u && aware) || u < chance_side) {          /* 0.15 */
                    if (r->drift == 0) r->drift = (int8_t)(v->t.lateral < c->t.lateral ? 5 : -5);
                } else if (((cpu_target && u < 6554u && aware) || u < chance_spin) &&    /* 0.2 */
                           ((diff >= 2u && player_total < self_total) || player_total + 138 * 256 < self_total)) {
                    start_attack(i, true, (rnd() & 1u) ? -1 : 1);
                }
            }
        }
    }
    /* EXPERT and MASTER: the racer just behind. */
    if (diff >= 2u && aware && race > THINK_AFTER_TICKS && VIS() && !(r->mode & (M_SIDE | M_SPIN))) {
        const uint32_t s = F.field->slot[i];
        const uint32_t bi = F.field->order[s + 1u < F.count ? s + 1u : 0u];
        const sh2z_vehicle_t *bv = &F.veh[bi];
        if (bi != i && near_gap(v, bv) < 23 * 256 && near_gap(v, bv) >= 0) {
            if ((player_total < self_total || player_total + 138 * 256 < self_total) && (rnd() & 0x7fffu) < 16u)
                start_attack(i, true, (rnd() & 1u) ? -1 : 1);
            if (iabs(bv->t.lateral - v->t.lateral) < 138 * 256 && r->drift == 0 && (rnd() & 0x7fffu) < 328u)  /* 0.01 */
                r->drift = (int8_t)(v->t.lateral < bv->t.lateral ? 5 : -5);
        }
    }
    /* The side attack's two presses (driftingCounter, moved on think ticks). */
    if (r->drift) {
        const int dir = r->drift > 0 ? 1 : -1;
        r->drift = (int8_t)(r->drift - dir);
        const int32_t k = r->drift * dir;
        const bool ahead_of_player = player_total < self_total;
        const bool far = dir > 0 ? (self_total - player_total) > 184 * 256 : iabs(player_total - self_total) > 184 * 256;
        if ((k == 3 || k == 1) && (diff >= 2u || far) && ahead_of_player) press(i, dir);
    }
    /* A player driving backwards near CPUs (any difficulty). */
    if (reverse_run > REVERSE_TICKS && race > REVERSE_AFTER_TICKS && !(p->flags & SH2Z_VD_CRASHED) &&
        iabs(pl->t.lateral - v->t.lateral) < 138 * 256 && iabs(near_gap(pl, v)) < 500 * 256)
        start_attack(i, true, (rnd() & 1u) ? -1 : 1);
#undef VIS
#undef WIDE
}

static uint32_t mix(uint32_t h, uint32_t x) { return (h ^ x) * 16777619u; }

void fzx_atk_after(const sh2z_vd_body_t *p) {
    if (!live) return;
    /* Reverse driving: the player's speed along the line below zero, in a row. */
    reverse_run = F.veh[0].t.speed <= 0 && race_ticks() > 0 ? reverse_run + 1u : 0u;
    if (fzx_atk_ai && F.field->started) {
        const uint32_t tick = F.field->tick;
        for (uint32_t i = 1; i < F.count; ++i)
            if ((i & 3u) == (tick & 3u)) think(p, i);
    }
    ++fzx_atk_report[15];
    const uint32_t t = F.field->tick;
    if ((t & 255u) == 0u && t / 256u >= 1u && t / 256u <= FZX_ATK_CHECKPOINTS) {
        fzx_atk_report[14] = fzx_atk_hash();   /* at the checkpoints only */
        fzx_atk_chk[t / 256u - 1u] = fzx_atk_report[14] ^ sh2z_field_hash(F.field);
    }
}

/* ---- the scripted hunter ------------------------------------------------------------------- */
int fzx_atk_hunter(const sh2z_vd_body_t *p, uint16_t *buttons, int8_t *stick) {
    *buttons = 0x8000u;  /* A */
    *stick = 0;
    if (!live || !F.field->started) return 0;
    const sh2z_vehicle_t *me = &F.veh[0];
    int best = -1; int32_t best_gap = 0;
    for (uint32_t i = 1; i < F.count; ++i) {
        const sh2z_vehicle_t *v = &F.veh[i];
        if ((v->flags & (SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED)) || (R[i].mode & (M_SPUN | M_CRASHED))) continue;
        const int32_t g = near_gap(v, me);
        if (g < -40 * 256 || g > 900 * 256) continue;
        if (best < 0 || g < best_gap) { best = (int)i; best_gap = g; }
    }
    /* Heading term as the R01 driver (left of the line positive, Q30 rows). */
    const int64_t heading = ((int64_t)p->body[0][0] * p->road[2][0] + (int64_t)p->body[0][1] * p->road[2][1] +
                             (int64_t)p->body[0][2] * p->road[2][2]) >> 30;          /* Q30 */
    const int32_t lat_left = sh2z_vd_body_lateral(p) >> 8;                             /* Q8 */
    int32_t want_left = 0;
    if (best >= 0) {
        const sh2z_vehicle_t *v = &F.veh[best];
        const int32_t side = v->t.lateral >= 0 ? -1 : 1;                              /* come in from the open side */
        want_left = -(v->t.lateral + side * 34 * 256);
    }
    /* steer = 0.02 * (lateral - want) + 2.5 * heading, clamped, times 63 */
    int64_t steer_q16 = ((int64_t)(lat_left - want_left) * 1311) >> 8;                  /* 0.02 in Q16 */
    steer_q16 += (heading * 5) >> 15;                                                   /* 2.5: Q30 -> Q16 */
    if (steer_q16 > 65536) steer_q16 = 65536;
    if (steer_q16 < -65536) steer_q16 = -65536;
    *stick = (int8_t)((steer_q16 * 63 + (steer_q16 >= 0 ? 32768 : -32768)) / 65536);
    /* Side attack: two taps two ticks apart when beside the target. */
    const uint32_t now = F.field->tick;
    if (hunt_cool) --hunt_cool;
    if (best >= 0 && !hunt_cool && p->attack == SH2Z_VD_ATTACK_NONE) {
        const sh2z_vehicle_t *v = &F.veh[best];
        const int32_t across = v->t.lateral - (-lat_left);                              /* + : target to the right */
        if (iabs(best_gap) < 36 * 256 && iabs(across) < 90 * 256) { hunt_dir = across > 0 ? 1 : -1; hunt_tap_at = now; hunt_cool = 22; }
    }
    if (hunt_cool >= 18u) {
        const uint32_t k = now - hunt_tap_at;
        if (k == 0u || k == 2u) *buttons |= hunt_dir > 0 ? 0x0010u : 0x2000u;          /* R right, Z left */
    }
    /* Boost to close a long gap from the second lap, with energy to spare. */
    if (best >= 0 && best_gap > 300 * 256 && me->t.lap >= 1 && p->energy * 2 > (player_max_q8 << 8) && (now & 63u) == 0u)
        *buttons |= 0x4000u;
    return best >= 0;
}

uint32_t fzx_atk_hash(void) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < F.count; ++i) {
        const rival_t *r = &R[i];
        h = mix(h, r->mode); h = mix(h, (uint32_t)r->lv); h = mix(h, (uint32_t)r->attack_t); h = mix(h, (uint32_t)r->hit_t);
        h = mix(h, (uint32_t)r->spin_t); h = mix(h, (uint32_t)r->drift); h = mix(h, (uint32_t)fzx_atk_retire_tick[i]);
        h = mix(h, fzx_atk_retire_place[i]); h = mix(h, (uint32_t)body[i].toucher); h = mix(h, body[i].since);
    }
    h = mix(h, rng); h = mix(h, n_kos); h = mix(h, bcf.contacts); h = mix(h, (uint32_t)regain_pending);
    return h;
}
