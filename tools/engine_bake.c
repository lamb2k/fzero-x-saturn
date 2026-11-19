/* HOST tool for the engine line (docs/ENGINE_PLAN.md, E0). Built and run by
 * tools/engine_path.py; never part of a Saturn image.
 *
 *   engine_bake bake CAMERA COURSE OUT.PAK
 *       CAMERA: fzx-sim --camera-trace (camera 0 lines are used); COURSE:
 *       fzx-sim --engine-course. Writes the big-endian path pack.
 *   engine_bake check CAMERA BACKDROP SKYLINE [MUTATION]
 *       Compares the engine's fixed-point backdrop and skyline with the
 *       reference traces (fzx-ref REF_BACKDROP_TRACE / REF_SKYLINE_TRACE).
 *       MUTATION 1..4 deliberately breaks the engine side; the check must fail.
 *   engine_bake plan CAMERA COURSE PLAN [MUTATION]
 *       Runs the engine's fixed-point track pick (SH2ZAM segmesh) for every
 *       tick and compares the groups with the reference planner's trace
 *       (fzx-ref REF_ROAD_PLAN_TRACE). MUTATION 1..4 as above.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_path.h"
#include "road_plan.h"
#include "sh2zam/segmesh_experimental.h"

#define CAMERA_WORDS 63
typedef struct {
    int mode, setting, timer, look_back;
    float eye[3], at[3], basis[3][3], fov, near_plane, far_plane, viewport[4], scissor[4];
    float view[4][4], projection[4][4];
    unsigned faults;
} camera_t;
_Static_assert(sizeof(camera_t) == CAMERA_WORDS * 4, "camera trace layout");

static void die(const char *m) { fprintf(stderr, "engine_bake: %s\n", m); exit(1); }

/* "<tick> [index] w0 w1 ..." -> words; returns the count after the prefix. */
static int words(char *line, int prefix, long *tick, long *index, uint32_t *out, int cap) {
    char *save = NULL, *t = strtok_r(line, " \t\r\n", &save);
    int n = 0;
    if (!t) return -1;
    *tick = strtol(t, &t, 10);
    if (*t) return -1; /* a non-numeric prefix such as "records" */
    if (prefix == 2) {
        if (!(t = strtok_r(NULL, " \t\r\n", &save))) return -1;
        *index = strtol(t, NULL, 10);
    }
    while ((t = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
        if (n >= cap) return -1;
        out[n++] = (uint32_t)strtoul(t, NULL, 16);
    }
    return n;
}

static int32_t q(double v, double scale) {
    const double r = floor(v * scale + 0.5);
    if (!(r >= -2147483648.0 && r <= 2147483647.0)) die("value out of fixed range");
    return (int32_t)r;
}

static int from_source(fzx_engine_cam_t *cam, fzx_engine_consts_t *k, const camera_t *c, int first) {
    static const float scissor[4] = { 12.0f, 16.0f, 308.0f, 224.0f }; /* the race's draw scissor */
    fzx_view_t v;
    if (c->faults) return 0;
    if (!fzx_view_from_source(&v, c->view, c->projection, c->viewport, scissor, c->near_plane, c->far_plane)) return 0;
    memcpy(cam->rotation, v.rotation, sizeof(v.rotation));
    memcpy(cam->translation, v.translation, sizeof(v.translation));
    cam->focal[0] = v.focal[0]; cam->focal[1] = v.focal[1];
    for (int j = 0; j < 3; ++j) cam->eye[j] = q(c->eye[j], 65536.0);
    {
        /* tan of the source's table angle: 4096 steps per turn. */
        const int idx = (int)(0.5f * c->fov * (4096.0f / 360.0f) + 0.5f);
        const double a = idx * (2.0 * M_PI / 4096.0);
        const float s = (float)sin(a), co = (float)cos(a);
        cam->tan_half = q((double)(s / co), 65536.0);
    }
    {
        const double n = sqrt((double)c->basis[0][0] * c->basis[0][0] + (double)c->basis[0][2] * c->basis[0][2]);
        if (n < 1e-6) return 0;
        cam->xzn[0] = q(c->basis[0][0] / n, 1073741824.0);
        cam->xzn[1] = q(c->basis[0][2] / n, 1073741824.0);
    }
    {
        /* Rows must be the camera's own axes: forward = basis x, up = basis y. */
        double df = 0, du = 0, dl = 0;
        for (int j = 0; j < 3; ++j) {
            df += v.rotation[2][j] / 1073741824.0 * c->basis[0][j];
            du += v.rotation[1][j] / 1073741824.0 * c->basis[1][j];
            dl += v.rotation[0][j] / 1073741824.0 * c->basis[2][j];
        }
        if (df < 0.9999 || du < 0.9999 || fabs(fabs(dl) - 1.0) > 1e-4) die("view rows are not the camera basis");
        const int sign = dl > 0 ? 1 : -1;
        if (!first && sign != k->lateral_sign) die("lateral axis changed sign");
        k->lateral_sign = sign;
    }
    {
        const int32_t pa = (int32_t)(-c->projection[2][2] * 1073741824.0f), pb = (int32_t)(c->projection[3][2] * 4096.0f);
        const int vp0 = (int)c->viewport[0], vp1 = (int)c->viewport[1];
        /* Matrix_SetFrustrum: P20 = 2*centreX/fovScaleX, P21 = 2*centreY/fovScaleY, P00/P11 = fovScaleY/fovScaleX. */
        const int32_t ch = q(c->projection[2][0], 65536.0), cv = q(c->projection[2][1], 65536.0);
        const int32_t aspect = q((double)c->projection[0][0] / c->projection[1][1], 65536.0);
        if (!first && (abs(ch - k->center_h) > 2 || abs(cv - k->center_v) > 2 || abs(aspect - k->aspect) > 2))
            return 2; /* the camera left the race view (the finish) */
        if (first) { k->center_h = ch; k->center_v = cv; k->aspect = aspect; }
        if ((float)vp0 != c->viewport[0] || (float)vp1 != c->viewport[1]) die("fractional viewport scale");
        if (!first && (pa != k->pa || pb != k->pb || vp0 != k->viewport[0] || vp1 != k->viewport[1] ||
                       v.near_plane != k->near_plane || v.far_plane != k->far_plane ||
                       abs(v.center[0] - k->center[0]) > 64 || abs(v.center[1] - k->center[1]) > 64 ||
                       memcmp(v.clip, k->clip, sizeof(v.clip))))
            die("a per-course camera constant changed during the run");
        k->pa = pa; k->pb = pb; k->viewport[0] = vp0; k->viewport[1] = vp1;
        k->near_plane = v.near_plane; k->far_plane = v.far_plane;
        if (first) { k->center[0] = v.center[0]; k->center[1] = v.center[1]; } /* float noise below 1/1000 px after that */
        memcpy(k->clip, v.clip, sizeof(v.clip));
    }
    return 1;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static int plan_check(const char *plan_path, const fzx_engine_cam_t *cams, size_t n, const fzx_engine_consts_t *k,
                      const uint8_t *anchor_bytes, unsigned nchunks, int mutation);

static int bake(const char *camera_path, const char *course_path, const char *out_path, const char *plan_path, int mutation) {
    static char line[4096];
    static uint32_t w[CAMERA_WORDS + 4];
    FILE *f = fopen(course_path, "r");
    float constants[10] = { 0 }, (*chunks)[3] = NULL;
    unsigned nchunks = 0, got = 0;
    fzx_engine_consts_t k;
    memset(&k, 0, sizeof(k));
    if (!f) die("cannot read the course dump");
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "constants", 9)) {
            char *t = line + 9; int n = 0;
            while (n < 10) { char *e; uint32_t v = (uint32_t)strtoul(t, &e, 16); if (e == t) break; memcpy(&constants[n++], &v, 4); t = e; }
            if (n != 10) die("course constants malformed");
        } else if (!strncmp(line, "chunks", 6)) {
            nchunks = (unsigned)strtoul(line + 6, NULL, 10);
            if (nchunks < 3 || nchunks > FZX_EPATH_MAX_CHUNKS) die("unsupported chunk count");
            chunks = calloc(nchunks, sizeof(*chunks));
        } else if (chunks && got < nchunks) {
            uint32_t v[3];
            if (sscanf(line, "%x %x %x", &v[0], &v[1], &v[2]) != 3) die("chunk line malformed");
            memcpy(chunks[got++], v, 12);
        }
    }
    fclose(f);
    if (!chunks || got != nchunks) die("course dump incomplete");
    /* Anchors: positions >> shift so every component fits int16. */
    double big = 0;
    for (unsigned i = 0; i < nchunks; ++i) for (int j = 0; j < 3; ++j) if (fabs(chunks[i][j]) > big) big = fabs(chunks[i][j]);
    unsigned shift = 0;
    while (big / (double)(1u << shift) >= 32767.0) ++shift;
    if (shift > 8) die("course too large for the anchor format");
    k.anchor_shift = shift;
    k.origin_distance = q(constants[0], 65536.0); k.far_distance = q(constants[1], 65536.0);
    k.split_distance = q(constants[2], 65536.0);
    const double dump_aspect = constants[3] > 0 ? constants[4] / constants[3] : 0;
    k.fog_start = (int32_t)constants[9];
    k.fog[0] = 252; k.fog[1] = 192; k.fog[2] = 253; k.fog[3] = 255; /* overwritten by the wrapper's --fog */
    if (getenv("ENGINE_FOG")) {
        unsigned r, g, b;
        if (sscanf(getenv("ENGINE_FOG"), "%u,%u,%u", &r, &g, &b) != 3 || r > 255 || g > 255 || b > 255) die("ENGINE_FOG malformed");
        k.fog[0] = (uint8_t)r; k.fog[1] = (uint8_t)g; k.fog[2] = (uint8_t)b;
    }

    if (!(f = fopen(camera_path, "r"))) die("cannot read the camera trace");
    size_t cap = 16384, n = 0;
    fzx_engine_cam_t *cams = malloc(cap * sizeof(*cams));
    long expect = 0;
    while (fgets(line, sizeof(line), f)) {
        long tick = 0, index = 0; camera_t c;
        const int count = words(line, 2, &tick, &index, w, CAMERA_WORDS + 4);
        if (count < 0 || index != 0) continue;
        if (count != CAMERA_WORDS) die("camera line malformed");
        if (tick != expect++) die("camera ticks are not consecutive from 0");
        memcpy(&c, w, sizeof(c));
        if (n == cap) die("too many ticks");
        {
            const int r = from_source(&cams[n], &k, &c, n == 0);
            if (r == 2) { fprintf(stderr, "engine_bake: path ends at tick %ld, where the camera leaves the race view\n", tick); break; }
            if (r != 1) die("camera sample unusable");
        }
        for (int j = 0; j < 3; ++j) if (abs(cams[n].eye[j]) > 32000 * 65536) die("eye outside the supported range");
        ++n;
    }
    fclose(f);
    if (!n) die("no camera samples");
    if (fabs(dump_aspect - k.aspect / 65536.0) > 1e-3) die("projection aspect differs from the camera's fov scales");

    const uint32_t anchors = FZX_EPATH_HEADER_WORDS * 4u;
    const uint32_t records = (anchors + nchunks * 6u + 3u) & ~3u;
    const uint32_t bytes = records + (uint32_t)n * sizeof(fzx_engine_cam_t);
    uint8_t *p = calloc(bytes, 1);
    put32(p, FZX_EPATH_MAGIC); put32(p + 4, FZX_EPATH_VERSION); put32(p + 8, bytes);
    put32(p + 16, (uint32_t)n); put32(p + 20, sizeof(fzx_engine_cam_t)); put32(p + 24, nchunks); put32(p + 28, shift);
    put32(p + 32, (uint32_t)k.near_plane); put32(p + 36, (uint32_t)k.far_plane);
    put32(p + 40, (uint32_t)k.center[0]); put32(p + 44, (uint32_t)k.center[1]);
    for (int i = 0; i < 4; ++i) put32(p + 48 + 4 * i, (uint32_t)(int32_t)k.clip[i]);
    put32(p + 64, (uint32_t)k.pa); put32(p + 68, (uint32_t)k.pb);
    put32(p + 72, (uint32_t)k.viewport[0]); put32(p + 76, (uint32_t)k.viewport[1]);
    put32(p + 80, (uint32_t)k.origin_distance); put32(p + 84, (uint32_t)k.far_distance); put32(p + 88, (uint32_t)k.split_distance);
    put32(p + 92, (uint32_t)k.fog_start); put32(p + 96, (uint32_t)k.aspect); put32(p + 100, (uint32_t)k.lateral_sign);
    put32(p + 104, (uint32_t)k.center_h); put32(p + 108, (uint32_t)k.center_v);
    memcpy(p + 112, k.fog, 4);
    put32(p + 116, anchors); put32(p + 120, records);
    for (unsigned i = 0; i < nchunks; ++i) for (int j = 0; j < 3; ++j) {
        const int a = (int)floor(chunks[i][j] / (double)(1u << shift) + 0.5);
        p[anchors + i * 6 + j * 2] = (uint8_t)((unsigned)a >> 8); p[anchors + i * 6 + j * 2 + 1] = (uint8_t)a;
    }
    for (size_t i = 0; i < n; ++i) {
        const int32_t *s = (const int32_t *)&cams[i];
        for (unsigned j = 0; j < sizeof(fzx_engine_cam_t) / 4; ++j) put32(p + records + i * sizeof(fzx_engine_cam_t) + 4 * j, (uint32_t)s[j]);
    }
    const uint32_t hash = fzx_engine_fnv(p, bytes, 3);
    put32(p + 12, hash);
    if (plan_path) return plan_check(plan_path, cams, n, &k, p + anchors, nchunks, mutation);
    if (!(f = fopen(out_path, "wb")) || fwrite(p, 1, bytes, f) != bytes || fclose(f)) die("cannot write the pack");
    printf("ticks %zu chunks %u shift %u bytes %u hash %08x lateral %d aspect %d tan0 %d\n",
           n, nchunks, shift, bytes, hash, k.lateral_sign, k.aspect, cams[0].tan_half);
    return 0;
}

/* ---- track pick against the reference road plan --------------------------- */
static int plan_check(const char *plan_path, const fzx_engine_cam_t *cams, size_t n, const fzx_engine_consts_t *k,
                      const uint8_t *anchor_bytes, unsigned nchunks, int mutation) {
    static char line[8192];
    static sh2z_segmesh_anchor_t anchors[FZX_EPATH_MAX_CHUNKS];
    static int32_t depth[FZX_EPATH_MAX_CHUNKS + 1];
    static int8_t state[FZX_EPATH_MAX_CHUNKS + 1];
    FILE *f = fopen(plan_path, "r");
    unsigned long samples = 0, bad = 0, groups = 0; double worst = 0;
    if (!f) die("cannot read the plan trace");
    for (unsigned i = 0; i < nchunks; ++i) {
        anchors[i].x = (int16_t)((anchor_bytes[i * 6] << 8) | anchor_bytes[i * 6 + 1]);
        anchors[i].y = (int16_t)((anchor_bytes[i * 6 + 2] << 8) | anchor_bytes[i * 6 + 3]);
        anchors[i].z = (int16_t)((anchor_bytes[i * 6 + 4] << 8) | anchor_bytes[i * 6 + 5]);
    }
    while (fgets(line, sizeof(line), f)) {
        char *save = NULL, *t = strtok_r(line, " \t\r\n", &save);
        long v[13]; int i = 0;
        for (; t && i < 13; t = strtok_r(NULL, " \t\r\n", &save)) v[i++] = strtol(t, NULL, 10);
        if (i != 13) die("plan line malformed");
        if ((size_t)v[0] >= n) break;
        fzx_engine_cam_t cam = cams[v[0]];
        fzx_engine_consts_t kk = *k;
        if (mutation == 1) kk.origin_distance = 0;
        if (mutation == 2) kk.center_v = 0;
        if (mutation == 3) kk.far_distance /= 2;
        if (mutation == 4) cam.eye[0] += 400 * 65536;
        fzx_engine_pick_t pv; sh2z_segmesh_view_t sv; sh2z_segmesh_run_t runs[REF_ROAD_GROUPS];
        const sh2z_segmesh_scratch_t scratch = { depth, state };
        uint32_t count = 0, marked = 0;
        if (!fzx_engine_pick_view(&pv, &cam, &kk)) { fprintf(stderr, "tick %ld: pick view outside the contract\n", v[0]); return 2; }
        for (unsigned j = 0; j < 4; ++j) { sv.x[j] = pv.rows[j]; sv.y[j] = pv.rows[4 + j]; sv.w[j] = pv.rows[8 + j]; sv.d[j] = pv.rows[12 + j]; }
        sv.far = pv.thresholds[0]; sv.slope = pv.thresholds[1]; sv.split = pv.thresholds[2]; sv.floor = pv.thresholds[3];
        const int r = sh2z_segmesh_pick(anchors, nchunks, &sv, &scratch, runs, REF_ROAD_GROUPS, &count, &marked);
        int ok = r == SH2Z_SEGMESH_OK && count == (uint32_t)v[1] && marked == (uint32_t)v[2];
        for (uint32_t g = 0; ok && g < count; ++g, t = strtok_r(NULL, " \t\r\n", &save)) {
            unsigned a, b, bits; int d; float ref;
            if (!t || sscanf(t, "%u,%u,%d,%x", &a, &b, &d, &bits) != 4) die("plan group malformed");
            memcpy(&ref, &bits, 4);
            const double got = runs[g].average / (double)(1u << pv.depth_shift), diff = fabs(got - ref);
            if (a != runs[g].start || b != runs[g].end || d != runs[g].direction) ok = 0;
            else if (diff > worst) worst = diff;
            ++groups;
        }
        if (!ok) { if (bad < 5) fprintf(stderr, "tick %ld: plan differs (result %d, runs %u vs %ld, marked %u vs %ld)\n", v[0], r, count, v[1], marked, v[2]); ++bad; }
        ++samples;
    }
    fclose(f);
    printf("plan samples %lu differing %lu groups %lu worst average depth error %.3f\n", samples, bad, groups, worst);
    /* Group boundaries are decided by depth comparisons, so a sample exactly on
     * a threshold may differ; the gate allows 1 in 200 and 4 depth units. */
    if (!samples || bad * 200 > samples || worst > 4.0) { printf("FAIL\n"); return 2; }
    printf("PASS\n");
    return 0;
}

/* ---- check against the reference backdrop and skyline traces ------------- */
static int absd(int a, int b) { return a > b ? a - b : b - a; }

static int check(const char *camera_path, const char *backdrop_path, const char *skyline_path, int mutation) {
    static char lc[4096], lb[8192], ls[8192];
    static uint32_t wc[CAMERA_WORDS + 4], wb[sizeof(RefBackdrop) / 4 + 4], wsk[sizeof(RefSkyline) / 4 + 4];
    FILE *fc = fopen(camera_path, "r"), *fb = fopen(backdrop_path, "r"), *fs = fopen(skyline_path, "r");
    fzx_engine_consts_t k;
    unsigned long samples = 0, worst_pos = 0, worst_uv = 0, worst_matrix = 0, cloud_skipped = 0;
    if (!fc || !fb || !fs) die("cannot read the traces");
    memset(&k, 0, sizeof(k));
    while (fgets(lb, sizeof(lb), fb)) {
        long tick = 0, index = 0, t2 = 0; RefBackdrop ref; RefSkyline sky; camera_t c;
        fzx_engine_cam_t cam; RefBackdrop got; RefSkyline gsky;
        if (words(lb, 1, &tick, &index, wb, (int)(sizeof(ref) / 4 + 4)) != (int)(sizeof(ref) / 4)) die("backdrop line malformed");
        memcpy(&ref, wb, sizeof(ref));
        if (!fgets(ls, sizeof(ls), fs) || words(ls, 1, &t2, &index, wsk, (int)(sizeof(sky) / 4 + 4)) != (int)(sizeof(sky) / 4) || t2 != tick)
            die("skyline line malformed");
        memcpy(&sky, wsk, sizeof(sky));
        for (;;) { /* the camera-0 line of the same tick */
            long tc = 0, ic = 0;
            if (!fgets(lc, sizeof(lc), fc)) die("camera trace ended early");
            const int count = words(lc, 2, &tc, &ic, wc, CAMERA_WORDS + 4);
            if (count != CAMERA_WORDS || ic != 0 || tc < tick) continue;
            if (tc != tick) die("camera trace has no line for a backdrop tick");
            memcpy(&c, wc, sizeof(c));
            break;
        }
        if (ref.faults || ref.count != 20 || sky.faults) die("reference packet reports a fault");
        if (from_source(&cam, &k, &c, samples == 0) != 1) die("camera sample unusable");
        if (abs(k.aspect - q(ref.state[23], 65536.0)) > 2) die("projection aspect differs from the source backdrop's");
        /* The source's own range must match the baked tangent rule. */
        if (fabs((double)ref.state[3] - 7320.0 * cam.tan_half / 65536.0) > 0.75) die("tangent rule differs from the source range");
        fzx_engine_consts_t kk = k;
        if (mutation == 1) kk.lateral_sign = -kk.lateral_sign;
        if (mutation == 2) cam.xzn[0] = -cam.xzn[0];
        if (mutation == 3) cam.tan_half += cam.tan_half / 16;
        if (!fzx_engine_backdrop(&got, &cam, &kk, 1)) die("engine backdrop faulted");
        if (!fzx_engine_skyline(&gsky, &cam)) die("engine skyline faulted");
        if (mutation == 4) gsky.instances[0].matrix[0] += 4096;
        /* Floor and sky: positions and texture coordinates. Clouds: positions,
         * colours and the texture span between corners (the scroll phase is the
         * reference run's own state and wraps with the texture). */
        for (unsigned i = 0; i < 20; ++i) {
            const RefBackdropVertex *a = &ref.vertices[i], *b = &got.vertices[i];
            const int cloud = i >= 12;
            if (cloud && fabsf((ref.state[2 - 1] + 1700.0f - 750.0f) - c.eye[1]) < 1e-6f) { ++cloud_skipped; continue; }
            for (int j = 0; j < 3; ++j) { const unsigned long d = (unsigned long)absd(a->position[j], b->position[j]); if (d > worst_pos) worst_pos = d; }
            if (!cloud) for (int j = 0; j < 2; ++j) {
                const unsigned long d = (unsigned long)absd((short)(a->uv[j] - b->uv[j]), 0);
                if (d > worst_uv) worst_uv = d;
            } else if ((i & 3) != 0) for (int j = 0; j < 2; ++j) {
                const short ra = (short)(a->uv[j] - ref.vertices[i & ~3u].uv[j]), rb = (short)(b->uv[j] - got.vertices[i & ~3u].uv[j]);
                const unsigned long d = (unsigned long)absd((short)(ra - rb), 0);
                if (d > worst_uv) worst_uv = d;
            }
            if (memcmp(a->color, b->color, 4)) {
                const int da = absd(a->color[3], b->color[3]);
                if (memcmp(a->color, b->color, 3) || da > 1) { fprintf(stderr, "tick %ld vertex %u colour differs\n", tick, i); return 2; }
            }
        }
        if (gsky.count != sky.count || memcmp(gsky.tint, sky.tint, 4) || memcmp(gsky.vertices, sky.vertices, sizeof(sky.vertices))) {
            fprintf(stderr, "tick %ld skyline static data differs\n", tick); return 2;
        }
        for (unsigned i = 0; i < REF_SKYLINE_COUNT; ++i) {
            const RefSkylineInstance *a = &sky.instances[i], *b = &gsky.instances[i];
            if (a->sprite_id != b->sprite_id || a->visible != b->visible) { fprintf(stderr, "tick %ld skyline %u id/visibility differs\n", tick, i); return 2; }
            if (!a->visible) continue;
            for (int j = 0; j < 16; ++j) {
                unsigned long d = (unsigned long)absd(a->matrix[j], b->matrix[j]);
                if (j >= 12 && j < 15) d /= 4096; /* translation: 1/16 unit steps */
                if (d > worst_matrix) worst_matrix = d;
            }
        }
        ++samples;
    }
    printf("samples %lu worst position %lu uv %lu matrix %lu cloud-skipped %lu\n", samples, worst_pos, worst_uv, worst_matrix, cloud_skipped);
    /* Positions within 2 units, texture within 3 texels (32 per texel), matrix within 2/256. */
    if (!samples || worst_pos > 2 || worst_uv > 96 || worst_matrix > 512) { printf("FAIL\n"); return 2; }
    printf("PASS\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "bake")) return bake(argv[2], argv[3], argv[4], NULL, 0);
    if ((argc == 5 || argc == 6) && !strcmp(argv[1], "plan")) return bake(argv[2], argv[3], NULL, argv[4], argc == 6 ? atoi(argv[5]) : 0);
    if ((argc == 5 || argc == 6) && !strcmp(argv[1], "check")) return check(argv[2], argv[3], argv[4], argc == 6 ? atoi(argv[5]) : 0);
    die("usage: engine_bake bake CAMERA COURSE OUT.PAK | check CAMERA BACKDROP SKYLINE [MUTATION] | plan CAMERA COURSE PLAN [MUTATION]");
    return 1;
}
