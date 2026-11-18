/* FZX_ROAD_PLAN_FIXED (default 0): the source planner's per-chunk visibility test in
 * fixed point. Included by generated/road_plan/course.c inside ref_road_plan's
 * translation unit, so it sees the source chunk storage and course constants.
 *
 * Render-only approximation. The source projects every chunk in binary32: a depth
 * row, then x/w and y/w with one reciprocal, and draws the chunk when depth is in
 * [0, far] and both quotients are in [-1, 1]. Here chunk positions are int16 at a
 * per-course power-of-two scale (built once per course, keyed by the chunk storage
 * and a checksum of the position bits), the frame's matrix columns are scaled to
 * int32 at one exponent for x, y and w (another for depth), the dot products are
 * 64-bit, and |x| <= |w|, |y| <= |w| replace the divides (the same decision as
 * x/w in [-1, 1], including w == 0). Chunks whose decision sits within rounding of
 * a frustum edge or the depth bounds can flip; depth is kept at 1/16 unit and
 * stored back as binary32 for the source grouping that follows. Nothing here feeds
 * simulation, physics or gameplay state. */
#include <stdint.h>

static int16_t ref_road_fixed_pos[1024][3];
static const SegmentChunk *ref_road_fixed_base;
static s32 ref_road_fixed_count, ref_road_fixed_shift;
static uint32_t ref_road_fixed_key;
uint32_t ref_road_plan_fixed_builds;

static uint32_t ref_road_fixed_bits(f32 v) { uint32_t b; memcpy(&b, &v, 4); return b; }

/* Power-of-two float for exponents in the normal range (|e| <= 126). */
static f32 ref_road_fixed_pow2(s32 e) {
    uint32_t b = (uint32_t)(e + 127) << 23; f32 v; memcpy(&v, &b, 4); return v;
}

/* Exponent of the largest magnitude among N values (binary32 exponent field). */
static s32 ref_road_fixed_exponent(const f32 *v, s32 n) {
    s32 i, e = -126;
    for (i = 0; i < n; i++) {
        const s32 x = (s32)((ref_road_fixed_bits(v[i]) >> 23) & 0xff) - 127;
        if (x > e) e = x;
    }
    return e;
}

static void ref_road_fixed_build(void) {
    const SegmentChunk *c;
    s32 i, j, e = -126;
    for (c = gSegmentChunks; c != sLastSegmentChunk; c++) {
        const f32 p[3] = { c->pos.x, c->pos.y, c->pos.z };
        const s32 x = ref_road_fixed_exponent(p, 3);
        if (x > e) e = x;
    }
    /* Largest |position| < 2^(e+1); keep it below 2^15 after scaling by 2^-shift. */
    ref_road_fixed_shift = e + 1 - 15;
    if (ref_road_fixed_shift < 0) ref_road_fixed_shift = 0;
    {
        const f32 scale = ref_road_fixed_pow2(-ref_road_fixed_shift);
        for (c = gSegmentChunks, i = 0; c != sLastSegmentChunk; c++, i++) {
            const f32 p[3] = { c->pos.x, c->pos.y, c->pos.z };
            for (j = 0; j < 3; j++) {
                f32 v = p[j] * scale;
                s32 n = (s32)(v < 0.0f ? v - 0.5f : v + 0.5f);
                if (n > 32767) n = 32767;
                if (n < -32768) n = -32768;
                ref_road_fixed_pos[i][j] = (int16_t)n;
            }
        }
    }
    ++ref_road_plan_fixed_builds;
}

static uint32_t ref_road_fixed_checksum(void) {
    const SegmentChunk *c;
    uint32_t h = 2166136261u;
    for (c = gSegmentChunks; c != sLastSegmentChunk; c++) {
        h = (h ^ ref_road_fixed_bits(c->pos.x)) * 16777619u;
        h = (h ^ ref_road_fixed_bits(c->pos.y)) * 16777619u;
        h = (h ^ ref_road_fixed_bits(c->pos.z)) * 16777619u;
    }
    return h;
}

/* Column COL of rows 0..2 scaled by 2^E, and row 3 by 2^(E - shift), so that
 * sum(a[i] * pos_fixed[i]) + t is the source value times 2^(E - shift). */
static void ref_road_fixed_column(const MtxF *m, s32 col, s32 e, s32 a[3], s64 *t) {
    const f32 scale = ref_road_fixed_pow2(e);
    const f32 tscale = ref_road_fixed_pow2(e - ref_road_fixed_shift);
    s32 i;
    for (i = 0; i < 3; i++) a[i] = (s32)(m->m[i][col] * scale);
    *t = (s64)(m->m[3][col] * tscale);
}

static inline s64 ref_road_fixed_dot(const s32 a[3], const int16_t p[3], s64 t) {
    return (s64)a[0] * p[0] + (s64)a[1] * p[1] + (s64)a[2] * p[2] + t;
}

static void ref_road_plan_cull_fixed(const MtxF *sp60) {
    SegmentChunk *chunk;
    s32 i, exy, ed, ax[3], ay[3], aw[3], ad[3], dshift;
    s64 tx, ty, tw, td, far16;
    const uint32_t key = ref_road_fixed_checksum();
    if (ref_road_fixed_base != gSegmentChunks || ref_road_fixed_count != gSegmentChunkCount ||
        ref_road_fixed_key != key) {
        ref_road_fixed_build();
        ref_road_fixed_base = gSegmentChunks; ref_road_fixed_count = gSegmentChunkCount;
        ref_road_fixed_key = key;
    }
    {
        const f32 xyw[9] = { sp60->m[0][0], sp60->m[1][0], sp60->m[2][0], sp60->m[0][1], sp60->m[1][1],
                             sp60->m[2][1], sp60->m[0][3], sp60->m[1][3], sp60->m[2][3] };
        const f32 d[3] = { sp60->m[0][2], sp60->m[1][2], sp60->m[2][2] };
        /* Coefficients below 2^30 in magnitude after scaling. */
        exy = 29 - ref_road_fixed_exponent(xyw, 9);
        ed = 29 - ref_road_fixed_exponent(d, 3);
    }
    if (exy > 120) exy = 120;
    if (ed > 120) ed = 120;
    ref_road_fixed_column(sp60, 0, exy, ax, &tx);
    ref_road_fixed_column(sp60, 1, exy, ay, &ty);
    ref_road_fixed_column(sp60, 3, exy, aw, &tw);
    ref_road_fixed_column(sp60, 2, ed, ad, &td);
    /* Depth in 1/16 units: the dot product is depth * 2^(ed - shift). */
    dshift = ed - ref_road_fixed_shift - 4;
    far16 = (s64)(sCourseFarRenderDistance * 16.0f);
    for (chunk = gSegmentChunks, i = 0; chunk != sLastSegmentChunk; chunk++, i++) {
        const int16_t *p = ref_road_fixed_pos[i];
        const s64 dd = ref_road_fixed_dot(ad, p, td);
        const s64 d16 = dshift >= 0 ? dd >> dshift : dd << -dshift;
        chunk->depth = (f32)(s32)(d16 > 0x7fffffff ? 0x7fffffff : d16 < -0x7fffffff ? -0x7fffffff : d16) * 0.0625f;
        if (d16 < 0 || far16 < d16) {
            chunk->drawState = 0;
        } else {
            s64 w = ref_road_fixed_dot(aw, p, tw), x = ref_road_fixed_dot(ax, p, tx);
            if (w < 0) w = -w;
            if (x < 0) x = -x;
            if (x > w) {
                chunk->drawState = 0;
            } else {
                s64 y = ref_road_fixed_dot(ay, p, ty);
                if (y < 0) y = -y;
                chunk->drawState = y > w ? 0 : 1;
            }
        }
    }
}
