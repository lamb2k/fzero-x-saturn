/* FZX_TRACK_BAKED (default 0): the road planner's chunk visibility, grouping and
 * far-to-near ordering run as the Saturn-native SH2ZAM segmesh-1 pick instead of
 * the source's binary32 loops. Included by generated/road_plan/course.c inside
 * ref_road_plan's translation unit, so it sees the course's chunk storage and
 * constants. Render-only: nothing here feeds simulation or gameplay state.
 *
 * Once per course the chunk positions become int16 anchors at a power-of-two
 * scale. The cache is keyed by the ring's storage address, its length and
 * ref_track_ring_generation, which the port bumps wherever it builds or
 * replaces the ring (ref_race_init after Course_Init). The per-frame check is
 * O(1). FZX_TRACK_VERIFY (diagnostic, default 0) also hashes every chunk
 * position each frame, counts unannounced changes and rebuilds. Per frame
 * the camera's clip matrix becomes four fixed-point rows (x/y/w on one exponent,
 * depth on another, coefficients below 2^14, translations below 2^29) and the
 * source thresholds (far distance, the 30-unit direction step, the group split
 * and the -10000 draw floor) are scaled to the same depth units. Outside that
 * contract the function returns 0 and the planner runs the source loops. */
#include "track_pick.h"

static const SegmentChunk *ref_track_base;
static s32 ref_track_count, ref_track_shift, ref_track_ready;
static u32 ref_track_generation_seen;
unsigned ref_track_ring_generation;
#if FZX_TRACK_VERIFY
static u32 ref_track_key;
u32 ref_track_verify_mismatches;
#endif
u32 ref_track_pick_calls, ref_track_pick_fallbacks, ref_track_pick_builds;

static u32 ref_track_bits(f32 v) { u32 b; memcpy(&b, &v, 4); return b; }
/* Binary32 exponent field: -127 for zero/subnormal, 128 for inf/NaN. */
static s32 ref_track_exp(f32 v) { return (s32)((ref_track_bits(v) >> 23) & 0xff) - 127; }
/* Power-of-two float; callers keep E in -126..127. */
static f32 ref_track_pow2(s32 e) { u32 b = (u32)(e + 127) << 23; f32 v; memcpy(&v, &b, 4); return v; }

#if FZX_TRACK_VERIFY
/* FNV-1a over the bits of every chunk position in the current ring. */
static u32 ref_track_ring_key(void) {
    const SegmentChunk *c;
    u32 h = 2166136261u;
    for (c = gSegmentChunks; c != sLastSegmentChunk; c++) {
        h = (h ^ ref_track_bits(c->pos.x)) * 16777619u;
        h = (h ^ ref_track_bits(c->pos.y)) * 16777619u;
        h = (h ^ ref_track_bits(c->pos.z)) * 16777619u;
    }
    return h;
}
#endif

static s32 ref_track_build(void) {
    const SegmentChunk *c;
    s32 e = -127, i, j;
    f32 scale;
    ref_track_ready = 0;
    if (!fzx_track_anchors_begin((unsigned)gSegmentChunkCount)) return 0;
    for (c = gSegmentChunks; c != sLastSegmentChunk; c++) {
        const f32 p[3] = { c->pos.x, c->pos.y, c->pos.z };
        for (j = 0; j < 3; j++) {
            const s32 x = ref_track_exp(p[j]);
            if (x > 127) return 0; /* nonfinite position */
            if (x > e) e = x;
        }
    }
    /* Largest |position| < 2^(e+1); keep it below 2^15 after scaling by 2^-shift. */
    ref_track_shift = e + 1 - 15;
    if (ref_track_shift < 0) ref_track_shift = 0;
    scale = ref_track_pow2(-ref_track_shift);
    for (c = gSegmentChunks, i = 0; c != sLastSegmentChunk; c++, i++) {
        const f32 p[3] = { c->pos.x, c->pos.y, c->pos.z };
        s32 q[3];
        for (j = 0; j < 3; j++) {
            const f32 v = p[j] * scale;
            s32 n = (s32)(v < 0.0f ? v - 0.5f : v + 0.5f);
            if (n > 32767) n = 32767;
            if (n < -32768) n = -32768;
            q[j] = n;
        }
        fzx_track_anchor_set((unsigned)i, (short)q[0], (short)q[1], (short)q[2]);
    }
    ref_track_base = gSegmentChunks;
    ref_track_count = gSegmentChunkCount;
    ref_track_generation_seen = ref_track_ring_generation;
#if FZX_TRACK_VERIFY
    ref_track_key = ref_track_ring_key();
#endif
    ref_track_ready = 1;
    ++ref_track_pick_builds;
    return 1;
}

static s32 ref_track_current(void) {
    if (ref_track_ready && ref_track_base == gSegmentChunks && ref_track_count == gSegmentChunkCount &&
        ref_track_generation_seen == ref_track_ring_generation) {
#if FZX_TRACK_VERIFY
        if (ref_track_ring_key() == ref_track_key) return 1;
        ++ref_track_verify_mismatches; /* ring changed without a generation bump */
#else
        return 1;
#endif
    }
    return ref_track_build();
}

/* Exponent E for a coefficient group (rows 0..2 of the given columns) so that
 * |m * 2^E| < 2^14, or 999 when the group is unsupported. */
static s32 ref_track_group(const MtxF *m, const s32 *cols, s32 ncols) {
    s32 i, k, emax = -127;
    for (k = 0; k < ncols; k++)
        for (i = 0; i < 3; i++) {
            const s32 x = ref_track_exp(m->m[i][cols[k]]);
            if (x > emax) emax = x;
        }
    if (emax > 127 || emax < -114) return 999;
    return 13 - emax;
}

/* Row for column COL at exponent E: coefficients m[i][col] * 2^E and translation
 * m[3][col] * 2^(E - shift). Returns 0 when the translation is nonfinite or
 * would reach 2^29. */
static s32 ref_track_row(const MtxF *m, s32 col, s32 e, int out[4]) {
    const f32 m3 = m->m[3][col];
    const s32 et = ref_track_exp(m3), eshift = e - ref_track_shift;
    s32 i;
    if (et > 127 || eshift < -126 || eshift > 127) return 0;
    if (et >= -126 && et + eshift > 27) return 0; /* |m3| < 2^(et+1); keep |t| < 2^29 */
    for (i = 0; i < 3; i++) out[i] = (int)(m->m[i][col] * ref_track_pow2(e));
    {
        const f32 t = et < -126 ? 0.0f : m3 * ref_track_pow2(eshift);
        out[3] = (int)(t < 0.0f ? t - 0.5f : t + 0.5f);
    }
    return 1;
}

/* Scale a non-negative threshold to depth units; 0 when it would not fit. */
static s32 ref_track_threshold(f32 v, s32 k, int *out) {
    const s32 ev = ref_track_exp(v);
    f32 t;
    if (ev > 127 || v < 0.0f || (ev >= -126 && ev + 1 + k > 31)) return 0; /* v * 2^k < 2^31 */
    t = ev < -126 ? 0.0f : v * ref_track_pow2(k);
    *out = (int)(t + 0.5f);
    return 1;
}

/* 1: plan groups written; -1: the planner returns now; 0: run the source loops. */
static s32 ref_road_plan_segmesh(RefRoadPlan *out, const MtxF *m) {
    static const s32 xyw[3] = { 0, 1, 3 }, depth[1] = { 2 };
    FzxTrackPickView v;
    s32 e, ed, k, r;
    ++ref_track_pick_calls;
    if (!ref_track_current()) goto source;
    e = ref_track_group(m, xyw, 3);
    ed = ref_track_group(m, depth, 1);
    if (e == 999 || ed == 999) goto source;
    k = ed - ref_track_shift;
    if (k > 17) { ed -= k - 17; k = 17; }
    if (k < -2) goto source;
    if (!ref_track_row(m, 0, e, v.rows) || !ref_track_row(m, 1, e, v.rows + 4) ||
        !ref_track_row(m, 3, e, v.rows + 8) || !ref_track_row(m, 2, ed, v.rows + 12)) goto source;
    if (!ref_track_threshold(sCourseFarRenderDistance, k, &v.thresholds[0]) ||
        !ref_track_threshold(30.0f, k, &v.thresholds[1]) ||
        !ref_track_threshold(D_800CF51C, k, &v.thresholds[2])) goto source;
    if (!ref_track_threshold(10000.0f, k, &v.thresholds[3])) goto source;
    v.thresholds[3] = -v.thresholds[3];
    v.depth_unit = ref_track_pow2(-k);
    r = fzx_track_pick(out, &v);
    if (r == 0) goto source;
    return r > 0 ? 1 : -1;
source:
    ++ref_track_pick_fallbacks;
    return 0;
}
