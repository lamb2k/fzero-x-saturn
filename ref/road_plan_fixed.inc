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
 * simulation, physics or gameplay state.
 *
 * Supported-range contract. ref_road_plan_cull_fixed returns 0 without writing any
 * chunk, and the caller runs the source float loop, unless all of these hold:
 *   - every used matrix entry, the far distance and every chunk position is finite;
 *   - each coefficient group (x/y/w columns, depth column) has a nonzero largest
 *     magnitude, so its exponent E = 29 - emax gives |a| < 2^30, and both 2^E and
 *     2^(E - shift) are normal binary32 powers (-126..127);
 *   - each translation term satisfies |m3| * 2^(E - shift) < 2^60, so |t| < 2^60;
 *   - the depth shift dshift = Ed - shift - 4 lies in 0..62;
 *   - |far * 16| < 2^60.
 * Then |a * p| < 2^45, every dot product is below 2^61 in magnitude (no signed
 * overflow), the depth shift acts on a non-negative magnitude with a count below
 * 63, and every float-to-integer conversion is in range. */

static s16 ref_road_fixed_pos[1024][3];
static const SegmentChunk *ref_road_fixed_base;
static s32 ref_road_fixed_count, ref_road_fixed_shift, ref_road_fixed_valid;
static u32 ref_road_fixed_key;
u32 ref_road_plan_fixed_builds, ref_road_plan_fixed_fallbacks;
#if FZX_ROAD_CULL_PROFILE
/* Diagnostic: the Saturn image times the chunk loop; chunks reaching the x/y/w tests. */
void fzx_road_cull_mark(s32 phase, s32 chunks);
u32 ref_road_cull_depth_pass;
#endif

static u32 ref_road_fixed_bits(f32 v) { u32 b; memcpy(&b, &v, 4); return b; }
static s32 ref_road_fixed_finite(f32 v) { return ((ref_road_fixed_bits(v) >> 23) & 0xff) != 0xff; }

/* Power-of-two float; callers keep E in -126..127. */
static f32 ref_road_fixed_pow2(s32 e) {
    u32 b = (u32)(e + 127) << 23; f32 v; memcpy(&v, &b, 4); return v;
}

/* Exponent of the largest magnitude among N values (binary32 exponent field; -127
 * for zero or subnormal, 128 for inf/NaN). */
static s32 ref_road_fixed_exponent(const f32 *v, s32 n) {
    s32 i, e = -127;
    for (i = 0; i < n; i++) {
        const s32 x = (s32)((ref_road_fixed_bits(v[i]) >> 23) & 0xff) - 127;
        if (x > e) e = x;
    }
    return e;
}

static void ref_road_fixed_build(void) {
    const SegmentChunk *c;
    s32 i, j, e = -127;
    ref_road_fixed_valid = 0;
    for (c = gSegmentChunks; c != sLastSegmentChunk; c++) {
        const f32 p[3] = { c->pos.x, c->pos.y, c->pos.z };
        const s32 x = ref_road_fixed_exponent(p, 3);
        if (x > 127) return; /* nonfinite position: unsupported course */
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
                const f32 v = p[j] * scale; /* |v| < 2^15 */
                s32 n = (s32)(v < 0.0f ? v - 0.5f : v + 0.5f);
                if (n > 32767) n = 32767;
                if (n < -32768) n = -32768;
                ref_road_fixed_pos[i][j] = (s16)n;
            }
        }
    }
    ref_road_fixed_valid = 1;
    ++ref_road_plan_fixed_builds;
}

static u32 ref_road_fixed_checksum(void) {
    const SegmentChunk *c;
    u32 h = 2166136261u;
    for (c = gSegmentChunks; c != sLastSegmentChunk; c++) {
        h = (h ^ ref_road_fixed_bits(c->pos.x)) * 16777619u;
        h = (h ^ ref_road_fixed_bits(c->pos.y)) * 16777619u;
        h = (h ^ ref_road_fixed_bits(c->pos.z)) * 16777619u;
    }
    return h;
}

/* Exponent for a coefficient group, or 999 when the group is unsupported. */
static s32 ref_road_fixed_group_exponent(const f32 *v, s32 n) {
    const s32 emax = ref_road_fixed_exponent(v, n);
    s32 e;
    if (emax > 127 || emax < -126) return 999; /* nonfinite, or all zero/subnormal */
    e = 29 - emax;
    if (e > 127 || e - ref_road_fixed_shift < -126 || e - ref_road_fixed_shift > 127) return 999;
    return e;
}

/* Column COL of rows 0..2 scaled by 2^E, and row 3 by 2^(E - shift), so that
 * sum(a[i] * pos_fixed[i]) + t is the source value times 2^(E - shift). Returns 0
 * when the translation term is nonfinite or would reach 2^60. */
static s32 ref_road_fixed_column(const MtxF *m, s32 col, s32 e, s32 a[3], s64 *t) {
    const f32 scale = ref_road_fixed_pow2(e);
    const f32 m3 = m->m[3][col];
    const s32 et = ref_road_fixed_exponent(&m3, 1);
    s32 i;
    if (et > 127) return 0;
    if (et >= -126 && et + (e - ref_road_fixed_shift) > 58) return 0; /* |m3| < 2^(et+1) */
    for (i = 0; i < 3; i++) a[i] = (s32)(m->m[i][col] * scale); /* |a| < 2^30 */
    *t = et < -126 ? 0 : (s64)(m3 * ref_road_fixed_pow2(e - ref_road_fixed_shift));
    return 1;
}

static inline s64 ref_road_fixed_dot(const s32 a[3], const s16 p[3], s64 t) {
    return (s64)a[0] * p[0] + (s64)a[1] * p[1] + (s64)a[2] * p[2] + t;
}

/* 1 when the chunk visibility was computed here; 0 (nothing written) when the
 * frame or course is outside the supported range and the source loop must run. */
static s32 ref_road_plan_cull_fixed(const MtxF *sp60) {
    SegmentChunk *chunk;
    s32 i, exy, ed, ax[3], ay[3], aw[3], ad[3], dshift;
    s64 tx, ty, tw, td, far16;
    const u32 key = ref_road_fixed_checksum();
    if (ref_road_fixed_base != gSegmentChunks || ref_road_fixed_count != gSegmentChunkCount ||
        ref_road_fixed_key != key) {
        ref_road_fixed_build();
        ref_road_fixed_base = gSegmentChunks; ref_road_fixed_count = gSegmentChunkCount;
        ref_road_fixed_key = key;
    }
    if (!ref_road_fixed_valid) goto fallback;
    {
        const f32 xyw[9] = { sp60->m[0][0], sp60->m[1][0], sp60->m[2][0], sp60->m[0][1], sp60->m[1][1],
                             sp60->m[2][1], sp60->m[0][3], sp60->m[1][3], sp60->m[2][3] };
        const f32 d[3] = { sp60->m[0][2], sp60->m[1][2], sp60->m[2][2] };
        exy = ref_road_fixed_group_exponent(xyw, 9);
        ed = ref_road_fixed_group_exponent(d, 3);
    }
    if (exy == 999 || ed == 999) goto fallback;
    if (!ref_road_fixed_column(sp60, 0, exy, ax, &tx) || !ref_road_fixed_column(sp60, 1, exy, ay, &ty) ||
        !ref_road_fixed_column(sp60, 3, exy, aw, &tw) || !ref_road_fixed_column(sp60, 2, ed, ad, &td))
        goto fallback;
    /* Depth in 1/16 units: the dot product is depth * 2^(ed - shift). */
    dshift = ed - ref_road_fixed_shift - 4;
    if (dshift < 0 || dshift > 62) goto fallback;
    {
        const f32 far = sCourseFarRenderDistance;
        const s32 ef = ref_road_fixed_exponent(&far, 1);
        if (ef > 127 || ef + 4 > 58) goto fallback;
        far16 = ef < -126 ? 0 : (s64)(far * 16.0f);
    }
#if FZX_ROAD_CULL_PROFILE
    fzx_road_cull_mark(0, 0);
#endif
    for (chunk = gSegmentChunks, i = 0; chunk != sLastSegmentChunk; chunk++, i++) {
        const s16 *p = ref_road_fixed_pos[i];
        const s64 dd = ref_road_fixed_dot(ad, p, td);        /* |dd| < 2^61 */
        const s64 q = (dd < 0 ? -dd : dd) >> dshift;           /* magnitude, count 0..62 */
        const s64 d16 = dd < 0 ? -q : q;                      /* truncated toward zero */
        chunk->depth = (f32)(s32)(d16 > 0x7fffffff ? 0x7fffffff : d16 < -0x7fffffff ? -0x7fffffff : d16) * 0.0625f;
        if (d16 < 0 || far16 < d16) {
            chunk->drawState = 0;
#if FZX_ROAD_CULL_PROFILE == 2
        } else if (1) {
            chunk->drawState = 1; /* timing only: x/y/w tests skipped */
            ++ref_road_cull_depth_pass;
#endif
        } else {
#if FZX_ROAD_CULL_PROFILE
            ++ref_road_cull_depth_pass;
#endif
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
#if FZX_ROAD_CULL_PROFILE
    fzx_road_cull_mark(1, i);
#endif
    return 1;
fallback:
    ++ref_road_plan_fixed_fallbacks;
    return 0;
}
