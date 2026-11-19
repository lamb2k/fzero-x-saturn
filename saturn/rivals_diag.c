/* Rivals diagnostic (see rivals_diag.h). Fixed point only, SH2ZAM line follower. */
#include "rivals_diag.h"
#include <yaul.h>
#include <string.h>
#include "sh2zam/line_follower_experimental.h"
#include "texture_cd.h"
#include "rivals_course.h"   /* generated from the owner's ROM; never committed */

#if FZX_DIAG_RIVALS < 1 || FZX_DIAG_RIVALS > FZX_RIVALS_MAX
#error FZX_DIAG_RIVALS must be 1..29 when built
#endif

fzx_rival_pose_t fzx_rival_poses[FZX_RIVALS_MAX];
volatile uint32_t fzx_rival_count = FZX_DIAG_RIVALS;
volatile uint32_t fzx_rivals_prof[8];

/* The baked tables live in low WRAM, loaded once from RIVALS.PAK (high WRAM is full). */
static const sh2z_line_node_t *fzx_rivals_nodes;
static const fzx_rival_node_pose_t *fzx_rivals_poses;
static sh2z_line_t line;
static int loaded;
volatile uint32_t fzx_rivals_load_status;
static const sh2z_follow_params_t follow = { 24, 48, 3277, 64, 6 * 256 };
static sh2z_track_state_t rv[FZX_DIAG_RIVALS];
static int32_t expect[FZX_DIAG_RIVALS], lane[FZX_DIAG_RIVALS], pace_v[FZX_DIAG_RIVALS];
static uint8_t order[FZX_DIAG_RIVALS];
static int started;

#define RIVALS_GUARD 0x52495641u /* "RIVA" */
uint32_t fzx_rivals_diag_load(uint32_t low_end) {
    /* As every low-WRAM allocator: the previous owner's trailing guard at low_end stays untouched. */
    const uintptr_t base = (low_end + 95u) & ~(uintptr_t)31u;
    const uintptr_t end = base + ((FZX_RIVALS_PACK_BYTES + 31u) & ~31u);
    const uintptr_t scratch = end + 32u;
    const uint32_t scratch_bytes = fzx_texture_cd_scratch_bytes();
    if (scratch + scratch_bytes > 0x00300000u) { fzx_rivals_load_status = 2; return low_end; }
    if (!fzx_texture_cd_load("RIVALS.PAK", FZX_RIVALS_PACK_BYTES, FZX_RIVALS_PACK_HASH, (void *)base,
                             (void *)scratch, scratch_bytes)) {
        fzx_rivals_load_status = 3u + fzx_texture_cd_status.status;
        return low_end;
    }
    for (unsigned i = 0; i < 8u; ++i) ((volatile uint32_t *)end)[i] = RIVALS_GUARD;
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
        fzx_rival_poses[i].valid = 1;
        fzx_rival_poses[i].id = (uint8_t)i;
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
static uint16_t lerp_angle(uint16_t a, uint16_t b, int32_t f) {
    const int32_t d = (int16_t)(uint16_t)(b - a);
    return (uint16_t)(a + ((d * f) >> 16));
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
        fzx_rival_pose_t *p = &fzx_rival_poses[r];
        for (int c = 0; c < 3; ++c) {
            /* right (Q14) * lateral (Q8) -> Q4: >> 18 */
            p->pos[c] = lerp_q16(a->pos[c], b->pos[c], f) + (int32_t)(((int64_t)a->right[c] * rv[r].lateral) >> 18);
        }
        p->yaw = lerp_angle(a->yaw, b->yaw, f);
        p->pitch = lerp_angle(a->pitch, b->pitch, f);
        p->roll = lerp_angle(a->roll, b->roll, f);
    }
    for (uint32_t k = 0; k < FZX_DIAG_RIVALS; ++k) fzx_rival_poses[order[k]].rank = (uint8_t)k;
    const uint16_t dt = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_rivals_prof[3]++;
    fzx_rivals_prof[4] += dt;
    if (dt > fzx_rivals_prof[5]) fzx_rivals_prof[5] = dt;
}
