/* Rivals diagnostic (see rivals_diag.h). Fixed point only, SH2ZAM line follower. */
#include "rivals_diag.h"
#include <yaul.h>
#include <string.h>
#include "sh2zam/line_follower_experimental.h"
#include "texture_cd.h"
#include "sh2zam/unit30_experimental.h"
#include "rivals_course.h"   /* generated from the owner's ROM; never committed */

#if FZX_DIAG_RIVALS < 1 || FZX_DIAG_RIVALS > FZX_RIVALS_MAX
#error FZX_DIAG_RIVALS must be 1..29 when built
#endif

fzx_racer_pose16_t *fzx_rival_poses;
uint8_t *fzx_rival_ranks;
volatile uint32_t fzx_rival_count = FZX_DIAG_RIVALS;
volatile uint32_t fzx_rivals_prof[8];

/* The baked tables live in low WRAM, loaded once from RIVALS.PAK (high WRAM is full). */
static const sh2z_line_node_t *fzx_rivals_nodes;
static const fzx_rival_node_pose_t *fzx_rivals_poses;
static sh2z_line_t line;
static int loaded;
volatile uint32_t fzx_rivals_load_status;
static const sh2z_follow_params_t follow = { 24, 48, 3277, 64, 6 * 256 };
/* Rival state also lives in the low-WRAM carve (high WRAM keeps its stack margin). */
typedef struct {
    sh2z_track_state_t rv[FZX_DIAG_RIVALS];
    int32_t expect[FZX_DIAG_RIVALS], lane[FZX_DIAG_RIVALS], pace_v[FZX_DIAG_RIVALS];
    uint8_t order[(FZX_DIAG_RIVALS + 3) & ~3];
    fzx_racer_pose16_t poses[FZX_DIAG_RIVALS];
    uint8_t ranks[(FZX_DIAG_RIVALS + 3) & ~3];
} rivals_work_t;
static rivals_work_t *w;
#define rv (w->rv)
#define expect (w->expect)
#define lane (w->lane)
#define pace_v (w->pace_v)
#define order (w->order)
static int started;

#define RIVALS_GUARD 0x52495641u /* "RIVA" */
uint32_t fzx_rivals_diag_load(uint32_t low_end) {
    /* As every low-WRAM allocator: the previous owner's trailing guard at low_end stays untouched. */
    const uintptr_t base = (low_end + 95u) & ~(uintptr_t)31u;
    const uintptr_t work = base + ((FZX_RIVALS_PACK_BYTES + 31u) & ~31u);
    const uintptr_t end = work + ((sizeof(rivals_work_t) + 31u) & ~31u);
    const uintptr_t scratch = end + 32u;
    const uint32_t scratch_bytes = fzx_texture_cd_scratch_bytes();
    if (scratch + scratch_bytes > 0x00300000u) { fzx_rivals_load_status = 2; return low_end; }
    if (!fzx_texture_cd_load("RIVALS.PAK", FZX_RIVALS_PACK_BYTES, FZX_RIVALS_PACK_HASH, (void *)base,
                             (void *)scratch, scratch_bytes)) {
        fzx_rivals_load_status = 3u + fzx_texture_cd_status.status;
        return low_end;
    }
    for (unsigned i = 0; i < 8u; ++i) ((volatile uint32_t *)end)[i] = RIVALS_GUARD;
    w = (rivals_work_t *)work;
    memset(w, 0, sizeof *w);
    fzx_rival_poses = w->poses;
    fzx_rival_ranks = w->ranks;
    fzx_rivals_nodes = (const sh2z_line_node_t *)base;
    fzx_rivals_poses = (const fzx_rival_node_pose_t *)(base + FZX_RIVALS_NODE_COUNT * sizeof(sh2z_line_node_t));
    line.nodes = fzx_rivals_nodes; line.count = FZX_RIVALS_NODE_COUNT; line.length = FZX_RIVALS_LINE_LENGTH;
    loaded = 1;
    fzx_rivals_load_status = 1;
    fzx_rivals_diag_reset();
    return (uint32_t)end;   /* the trailing guard block starts here */
}

void fzx_rivals_diag_reset(void) {
    if (!loaded) return;
    for (uint32_t i = 0; i < FZX_DIAG_RIVALS; ++i) {
        /* pace = length / lap ticks: one divide per rival at set-up only. */
        pace_v[i] = FZX_RIVALS_LINE_LENGTH / fzx_rivals_lap_ticks[i];
        const int32_t back = (int32_t)((i / 2u + 1u) * 40u * 256u);
        rv[i].dist = FZX_RIVALS_LINE_LENGTH - back;
        rv[i].lap = -1;
        rv[i].speed = 0;
        rv[i].lat_vel = 0;
        lane[i] = (i & 1u) ? 30 * 256 : -30 * 256;
        rv[i].lateral = lane[i];
        rv[i].node = sh2z_line_locate(&line, rv[i].dist, 0);
        expect[i] = -back;
        order[i] = (uint8_t)i;
        fzx_rival_poses[i].set = 0;
        fzx_rival_poses[i].flags = 1;
    }
    started = 0;
}

void fzx_rivals_diag_ticks(uint32_t ticks, int32_t race_time) {
    if (!loaded) return;
    if (!started) {
        if (race_time <= 0) return;
        started = 1;
        fzx_rivals_prof[6] = fzx_rivals_prof[0];
    }
    for (uint32_t k = 0; k < ticks; ++k) {
        const uint16_t t0 = cpu_frt_count_get();
        for (uint32_t i = 0; i < FZX_DIAG_RIVALS; ++i) {
            sh2z_line_sample_t at;
            expect[i] += pace_v[i];
            const int32_t want = sh2z_pace_target(pace_v[i], expect[i], sh2z_track_total(&line, &rv[i]), 655, 0, pace_v[i] * 2);
            sh2z_line_sample(&line, rv[i].node, rv[i].dist, &at);
            const int32_t lap = rv[i].lap;
            sh2z_follow_step(&line, &follow, &rv[i], want, at.lateral + lane[i], 1);
            if (rv[i].lap != lap && rv[i].lap >= 1) ++fzx_rivals_prof[7];
        }
        sh2z_rank_sort(order, &line, rv, FZX_DIAG_RIVALS);
        const uint16_t dt = (uint16_t)(cpu_frt_count_get() - t0);
        fzx_rivals_prof[0]++;
        fzx_rivals_prof[1] += dt;
        if (dt > fzx_rivals_prof[2]) fzx_rivals_prof[2] = dt;
    }
}

static int32_t lerp_q16(int32_t a, int32_t b, int32_t f) { return a + (int32_t)(((int64_t)(b - a) * f) >> 16); }
/* Q1.14 row from a Q2.30 unit vector, rounded. */
static void row14(int16_t out[3], const int32_t u[3]) {
    for (int c = 0; c < 3; ++c) out[c] = (int16_t)((u[c] + (1 << 15)) >> 16);
}
/* Orthonormal basis from interpolated forward and up (Q1.14 inputs), Gram-Schmidt in Q2.30:
 * forward = n(f); up = n(u - (u.f) f); left = up x forward (the source basis is right-handed
 * with z = x cross y when the bake reports a negative handedness; see RIVALS-NATIVE-PLAN.md). */
static void basis16(int16_t out[3][3], const int32_t f14[3], const int32_t u14[3]) {
    int32_t f[3], u[3], l[3], t[3];
    if (!sh2z_vec3_normalize_u30(f, f14)) { f[0] = 0; f[1] = 0; f[2] = 1 << 30; }
    const int64_t dot = ((int64_t)u14[0] * f[0] + (int64_t)u14[1] * f[1] + (int64_t)u14[2] * f[2]) >> 30; /* Q14 */
    for (int c = 0; c < 3; ++c) t[c] = (u14[c] << 16) - (int32_t)((dot * f[c]) >> 14);
    if (!sh2z_vec3_normalize_u30(u, t)) { u[0] = 0; u[1] = 1 << 30; u[2] = 0; }
    /* left = FZX_RIVALS_LEFT_SIGN * (forward x up), Q2.30 x Q2.30 -> Q2.30 */
    l[0] = (int32_t)((((int64_t)f[1] * u[2] - (int64_t)f[2] * u[1]) >> 30) * FZX_RIVALS_LEFT_SIGN);
    l[1] = (int32_t)((((int64_t)f[2] * u[0] - (int64_t)f[0] * u[2]) >> 30) * FZX_RIVALS_LEFT_SIGN);
    l[2] = (int32_t)((((int64_t)f[0] * u[1] - (int64_t)f[1] * u[0]) >> 30) * FZX_RIVALS_LEFT_SIGN);
    row14(out[0], f); row14(out[1], u); row14(out[2], l);
}

void fzx_rivals_diag_poses(void) {
    if (!loaded) return;
    const uint16_t t0 = cpu_frt_count_get();
    for (uint32_t r = 0; r < FZX_DIAG_RIVALS; ++r) {
        const uint32_t n = rv[r].node;
        const uint32_t m = n + 1u < FZX_RIVALS_NODE_COUNT ? n + 1u : 0u;
        const fzx_rival_node_pose_t *a = &fzx_rivals_poses[n], *b = &fzx_rivals_poses[m];
        const int32_t d = rv[r].dist - fzx_rivals_nodes[n].start;
        int32_t f = (int32_t)(((uint64_t)(uint32_t)(d < 0 ? 0 : d) * fzx_rivals_nodes[n].inv_span) >> 16);
        if (f > 65535) f = 65535;
        fzx_racer_pose16_t *p = &fzx_rival_poses[r];
        int32_t fw[3], up[3];
        for (int c = 0; c < 3; ++c) {
            /* lateral is + right = - left: left (Q14) * lateral (Q8) -> Q4 is >> 18 */
            const int32_t q4 = lerp_q16(a->pos[c], b->pos[c], f) - (int32_t)(((int64_t)a->left[c] * rv[r].lateral) >> 18);
            p->pos[c] = q4 * 4096;   /* Q4 -> Q16.16 */
            fw[c] = lerp_q16(a->fwd[c], b->fwd[c], f);
            up[c] = lerp_q16(a->up[c], b->up[c], f);
        }
        basis16(p->basis, fw, up);
    }
    for (uint32_t k = 0; k < FZX_DIAG_RIVALS; ++k) fzx_rival_ranks[order[k]] = (uint8_t)k;
    const uint16_t dt = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_rivals_prof[3]++;
    fzx_rivals_prof[4] += dt;
    if (dt > fzx_rivals_prof[5]) fzx_rivals_prof[5] = dt;
}
