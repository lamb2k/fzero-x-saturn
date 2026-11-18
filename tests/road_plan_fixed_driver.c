/* HOST driver for ref/road_plan_fixed.inc (FZX_ROAD_PLAN_FIXED). Built with UBSan
 * (shift counts, signed overflow, float-to-int conversions). Minimal stand-ins for
 * the source types the include uses; the float reference is the source per-chunk
 * test (depth row, then x/w and y/w with one reciprocal) in binary32.
 * Usage: driver edges | driver random N SEED   (exit 0 = pass) */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint32_t u32;
typedef float f32;
typedef struct { f32 x, y, z; } Vec3f;
typedef struct { f32 m[4][4]; } MtxF;
typedef struct { Vec3f pos; f32 depth; s32 drawState; } SegmentChunk;

static SegmentChunk chunks[1025];
SegmentChunk *gSegmentChunks = chunks, *sLastSegmentChunk;
s32 gSegmentChunkCount;
f32 sCourseFarRenderDistance;

#include "road_plan_fixed.inc"

static void set_chunks(s32 n) { gSegmentChunkCount = n; sLastSegmentChunk = chunks + n; }

/* Source loop, binary32. */
static void reference(const MtxF *m, f32 depth[], s32 draw[]) {
    s32 i;
    for (i = 0; i < gSegmentChunkCount; i++) {
        const Vec3f p = chunks[i].pos;
        const f32 d = ((m->m[0][2] * p.x) + (m->m[1][2] * p.y) + (m->m[2][2] * p.z)) + m->m[3][2];
        depth[i] = d;
        if (d < 0.0f || sCourseFarRenderDistance < d) { draw[i] = 0; continue; }
        {
            const f32 r = 1.0f / (((m->m[0][3] * p.x) + (m->m[1][3] * p.y) + (m->m[2][3] * p.z)) + m->m[3][3]);
            const f32 x = r * (((m->m[0][0] * p.x) + (m->m[1][0] * p.y) + (m->m[2][0] * p.z)) + m->m[3][0]);
            if (x < -1.0f || x > 1.0f) { draw[i] = 0; continue; }
            {
                const f32 y = r * (((m->m[0][1] * p.x) + (m->m[1][1] * p.y) + (m->m[2][1] * p.z)) + m->m[3][1]);
                draw[i] = (y < -1.0f || y > 1.0f) ? 0 : 1;
            }
        }
    }
}

static u32 rng = 1;
static f32 urand(void) { rng = rng * 1664525u + 1013904223u; return (f32)((rng >> 8) & 0xffffff) / 16777216.0f; }
static f32 srand1(f32 a) { return (urand() * 2.0f - 1.0f) * a; }

/* A view-projection like the planner's sp60: rotation about Y, perspective, eye. */
static void camera(MtxF *m, f32 yaw, f32 ex, f32 ey, f32 ez, f32 focal) {
    const f32 c = cosf(yaw), s = sinf(yaw), n = 10.0f, f = 20000.0f;
    const f32 a = (n + f) / (n - f), b = 2.0f * n * f / (n - f);
    /* view rows (camera x, y, -z) applied to (p - eye) */
    const f32 v[3][3] = { { c, 0, -s }, { 0, 1, 0 }, { s, 0, c } };
    const f32 e[3] = { ex, ey, ez };
    s32 i, j;
    memset(m, 0, sizeof(*m));
    for (i = 0; i < 3; i++) {
        m->m[i][0] = focal * v[0][i];
        m->m[i][1] = focal * v[1][i];
        m->m[i][2] = -a * v[2][i];   /* depth grows forward */
        m->m[i][3] = v[2][i];        /* w = forward distance */
    }
    for (j = 0; j < 4; j++) {
        f32 t = 0.0f;
        for (i = 0; i < 3; i++) t -= m->m[i][j] * e[i];
        m->m[3][j] = t;
    }
    m->m[3][2] += b * 0.0f;
}

static int fail(const char *what) { printf("FAIL %s\n", what); return 1; }

static int edges(void) {
    MtxF m; f32 depth[1024]; s32 draw[1024], i, r;
    set_chunks(64);
    for (i = 0; i < 64; i++) { chunks[i].pos = (Vec3f){ (f32)(i * 37 - 1000), (f32)(i % 5), (f32)(i * 53 - 1500) }; }
    sCourseFarRenderDistance = 8000.0f;
    camera(&m, 0.3f, 0.0f, 10.0f, 0.0f, 1.7f);
    /* 1. Normal frame: handled. */
    for (i = 0; i < 64; i++) chunks[i].drawState = 7;
    r = ref_road_plan_cull_fixed(&m);
    if (r != 1) return fail("normal frame not handled");
    printf("normal handled shift %d\n", ref_road_fixed_shift);
    /* 2. Negative dd: chunks behind the camera keep drawState 0 and a negative depth. */
    reference(&m, depth, draw);
    for (i = 0; i < 64; i++) if (depth[i] < -1.0f && (chunks[i].drawState != 0 || chunks[i].depth >= 0.0f)) return fail("negative depth");
    printf("negative-depth chunks ok\n");
    /* 3. Zero depth column (shift 0 would give dshift 116): fallback, nothing written. */
    {
        MtxF z = m; u32 before = ref_road_plan_fixed_fallbacks;
        z.m[0][2] = z.m[1][2] = z.m[2][2] = 0.0f;
        for (i = 0; i < 64; i++) { chunks[i].drawState = 7; chunks[i].depth = 123.0f; }
        if (ref_road_plan_cull_fixed(&z) != 0 || ref_road_plan_fixed_fallbacks != before + 1) return fail("zero column");
        for (i = 0; i < 64; i++) if (chunks[i].drawState != 7 || chunks[i].depth != 123.0f) return fail("zero column wrote");
        printf("zero depth column -> fallback\n");
    }
    /* 4. Maximum and minimum coefficient scales, huge translation, nonfinite entries, far. */
    {
        static const struct { const char *name; s32 row, col; f32 value; } cases[] = {
            { "max scale 2^100", 0, 2, 1.2676506e30f }, { "min scale 2^-120", -1, 2, 7.5231638e-37f },
            { "huge translation", 3, 0, 1.0e30f }, { "inf entry", 1, 1, INFINITY }, { "nan entry", 2, 3, NAN },
            { "max xyw 2^60", 0, 0, 1.1529215e18f },
        };
        for (i = 0; i < (s32)(sizeof(cases) / sizeof(cases[0])); i++) {
            MtxF z = m;
            if (cases[i].row < 0) { z.m[0][2] = z.m[1][2] = z.m[2][2] = cases[i].value; }
            else z.m[cases[i].row][cases[i].col] = cases[i].value;
            r = ref_road_plan_cull_fixed(&z);
            printf("%s -> %s\n", cases[i].name, r ? "handled" : "fallback");
        }
        {
            const f32 saved = sCourseFarRenderDistance;
            sCourseFarRenderDistance = INFINITY;
            printf("inf far -> %s\n", ref_road_plan_cull_fixed(&m) ? "handled" : "fallback");
            sCourseFarRenderDistance = 1.0e30f;
            printf("huge far -> %s\n", ref_road_plan_cull_fixed(&m) ? "handled" : "fallback");
            sCourseFarRenderDistance = saved;
        }
    }
    /* 5. Courses: all-zero positions, very large positions (shift > 0), a NaN position. */
    {
        const Vec3f saved = chunks[3].pos;
        for (i = 0; i < 64; i++) chunks[i].pos.x *= 4096.0f;
        printf("large course -> %s shift %d\n", ref_road_plan_cull_fixed(&m) ? "handled" : "fallback", ref_road_fixed_shift);
        for (i = 0; i < 64; i++) chunks[i].pos.x /= 4096.0f;
        chunks[3].pos.y = NAN;
        if (ref_road_plan_cull_fixed(&m) != 0) return fail("nan position handled");
        printf("nan position -> fallback\n");
        chunks[3].pos = saved;
        for (i = 0; i < 64; i++) chunks[i].pos = (Vec3f){ 0, 0, 0 };
        printf("zero course -> %s shift %d\n", ref_road_plan_cull_fixed(&m) ? "handled" : "fallback", ref_road_fixed_shift);
    }
    printf("edges pass\n");
    return 0;
}

static int random_cases(s32 n, u32 seed) {
    MtxF m; f32 depth[1024]; s32 draw[1024], i, t, flips = 0, edge_flips = 0, handled = 0;
    double worst = 0.0;
    rng = seed;
    for (t = 0; t < n; t++) {
        const s32 count = 300;
        const f32 extent = t % 3 == 0 ? 4000.0f : t % 3 == 1 ? 30000.0f : 120000.0f;
        set_chunks(count);
        for (i = 0; i < count; i++) chunks[i].pos = (Vec3f){ srand1(extent), srand1(400.0f), srand1(extent) };
        sCourseFarRenderDistance = 6000.0f + urand() * 8000.0f;
        camera(&m, urand() * 6.2831853f, srand1(extent), srand1(300.0f), srand1(extent), 1.0f + urand() * 2.0f);
        reference(&m, depth, draw);
        if (!ref_road_plan_cull_fixed(&m)) continue;
        ++handled;
        {
            /* Depth error bound: position rounding 2^(shift-1) per axis times the depth
             * column's magnitude sum, coefficient rounding, and the 1/16 truncation. */
            const double colsum = fabs(m.m[0][2]) + fabs(m.m[1][2]) + fabs(m.m[2][2]);
            const double bound = colsum * ldexp(1.0, ref_road_fixed_shift - 1) + 0.0625 + 1e-3 * extent / 1000.0;
            for (i = 0; i < count; i++) {
                const double err = fabs((double)chunks[i].depth - (double)depth[i]);
                if (!isfinite(chunks[i].depth) || err > bound) {
                    printf("FAIL depth err %.4f bound %.4f shift %d\n", err, bound, ref_road_fixed_shift);
                    return 1;
                }
                if (err > worst) worst = err;
                if (chunks[i].drawState != draw[i]) {
                    ++flips;
                    /* A flip must sit near a frustum or depth edge in the float reference. */
                    {
                        const Vec3f p = chunks[i].pos;
                        const f32 w = ((m.m[0][3] * p.x) + (m.m[1][3] * p.y) + (m.m[2][3] * p.z)) + m.m[3][3];
                        const f32 x = ((m.m[0][0] * p.x) + (m.m[1][0] * p.y) + (m.m[2][0] * p.z)) + m.m[3][0];
                        const f32 y = ((m.m[0][1] * p.x) + (m.m[1][1] * p.y) + (m.m[2][1] * p.z)) + m.m[3][1];
                        const double margin = bound * 4.0 + 1.0;
                        const int near_edge = fabs(fabs(x) - fabs(w)) < margin * (fabs(m.m[0][0]) + 1) ||
                                              fabs(fabs(y) - fabs(w)) < margin * (fabs(m.m[0][1]) + fabs(m.m[1][1]) + 1) ||
                                              fabs(depth[i]) < margin || fabs(depth[i] - sCourseFarRenderDistance) < margin;
                        if (near_edge) ++edge_flips;
                        else { printf("FAIL flip away from an edge (chunk %d)\n", i); return 1; }
                    }
                }
            }
        }
    }
    printf("random %d frames, %d handled, %d flips (all near an edge %d), worst depth error %.4f\n",
           n, handled, flips, edge_flips, worst);
    return handled == n ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "edges")) return edges();
    if (argc == 4 && !strcmp(argv[1], "random")) return random_cases(atoi(argv[2]), (u32)strtoul(argv[3], 0, 10));
    return 2;
}
