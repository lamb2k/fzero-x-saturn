/* Engine line, E0: camera-derived scene state in fixed point. See engine_scene.h.
 * The layout rules (backdrop quads that follow the camera, cloud scroll, the
 * skyline ring, the track pick's pulled-back eye) and their tuning values are
 * the source game's; the arithmetic is this engine's own. */
#include "engine_scene.h"
#include <string.h>

typedef int64_t i64;

/* Shift right, truncating toward zero (as a float-to-int conversion does). */
static i64 tz(i64 v, unsigned s) { return v >= 0 ? v >> s : -((-v) >> s); }

#define Q16(n) ((i64)(n) * 65536)
/* binary32 values of the source's scales, as Q32. */
#define FLOOR_SCALE_Q32 INT64_C(128849016) /* 0.03f */
#define CLOUD_SCALE_Q32 INT64_C(25769804)  /* 0.006f */
#define CLOUD_SPEED_X_Q32 INT64_C(6442451) /* 3 * 0.0005f */
#define CLOUD_SPEED_Z_Q32 INT64_C(-8589935) /* -4 * 0.0005f */
#define WRAP_Q16 Q16(1024)

static i64 wrap1024(i64 q16) {
    q16 %= WRAP_Q16;
    return q16 < 0 ? q16 + WRAP_Q16 : q16;
}

/* Texture base: the smallest coordinate moved into [-1024, 0) or [0, 1024). */
static i64 uv_base(const i64 v[4]) {
    i64 b = v[0], n;
    for (unsigned i = 1; i < 4; ++i) if (v[i] < b) b = v[i];
    if (b > 0) n = ((b >> 16) + 1024) / 1024;
    else n = tz(b, 16) / 1024;
    return -Q16(n * 1024);
}

typedef struct {
    i64 px, pz, xr, zr, ex, ez;
} plane_t;

/* One camera-following quad on a horizontal plane at Y. RATIO (Q16.16) scales
 * the texture origin toward the eye when RELATIVE is set. */
static void plane_quad(RefBackdropVertex *v, const plane_t *p, i64 xo1, i64 zo1, i64 xo2, i64 zo2,
                       i64 y, bool relative, i64 ratio, i64 scale_q32, i64 scroll_x, i64 scroll_z,
                       const uint8_t first[4], const uint8_t second[4]) {
    const i64 x[4] = { p->px + xo1 + p->xr, p->px + xo1 - p->xr, p->px + xo2 + p->xr, p->px + xo2 - p->xr };
    const i64 z[4] = { p->pz + zo1 + p->zr, p->pz + zo1 - p->zr, p->pz + zo2 + p->zr, p->pz + zo2 - p->zr };
    i64 s[4], t[4];
    for (unsigned i = 0; i < 4; ++i) {
        const i64 sx = relative ? (((x[i] - p->ex) * ratio) >> 16) + p->ex : x[i];
        const i64 sz = relative ? (((z[i] - p->ez) * ratio) >> 16) + p->ez : z[i];
        s[i] = ((sx * scale_q32) >> 32) + scroll_x;
        t[i] = ((sz * scale_q32) >> 32) + scroll_z;
    }
    const i64 bs = uv_base(s), bt = uv_base(t);
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *c = i < 2 ? first : second;
        v[i].position[0] = (short)tz(x[i], 16);
        v[i].position[1] = (short)tz(y, 16);
        v[i].position[2] = (short)tz(z[i], 16);
        v[i].flag = 0;
        v[i].uv[0] = (short)tz(s[i] + bs, 11); /* texels * 32 */
        v[i].uv[1] = (short)tz(t[i] + bt, 11);
        memcpy(v[i].color, c, 4);
    }
}

static i64 clamp32000(i64 v) {
    return v < -Q16(32000) ? -Q16(32000) : v > Q16(32000) ? Q16(32000) : v;
}

bool fzx_engine_backdrop(RefBackdrop *out, const fzx_engine_cam_t *cam,
                         const fzx_engine_consts_t *k, uint32_t tick) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!cam || !k || cam->tan_half <= 0 || k->aspect <= 0 ||
        (k->lateral_sign != 1 && k->lateral_sign != -1)) { out->faults = 1; return false; }
    const i64 nx = cam->xzn[0], nz = cam->xzn[1];
    const i64 ex = cam->eye[0], ey = cam->eye[1], ez = cam->eye[2];
    /* (scroll depth + scale) * tan * 1.2: 6100 * 1.2 = 7320. */
    const i64 hr = (i64)7320 * cam->tan_half;
    const i64 vr = (hr * k->aspect) >> 16;
    if (hr <= 0 || vr <= 0) { out->faults = 1; return false; }
    plane_t p;
    p.px = ex + ((nx * FZX_ENGINE_BG_SCALE) >> 14);
    p.pz = ez + ((nz * FZX_ENGINE_BG_SCALE) >> 14);
    p.xr = (hr * nz) >> 30;
    p.zr = -((hr * nx) >> 30);
    p.ex = ex; p.ez = ez;
    const i64 xo = (nx * FZX_ENGINE_SCROLL_DEPTH) >> 14, zo = (nz * FZX_ENGINE_SCROLL_DEPTH) >> 14;
    static const uint8_t bright[4] = { 255, 255, 255, 255 }, dark[4] = { 0, 0, 0, 255 };

    /* Floor 0..7: far quad fades out toward the horizon, near quad the reverse. */
    plane_quad(&out->vertices[0], &p, xo, zo, 0, 0, Q16(FZX_ENGINE_BG_Y), false, 0, FLOOR_SCALE_Q32, 0, 0, bright, dark);
    plane_quad(&out->vertices[4], &p, 0, 0, -xo, -zo, Q16(FZX_ENGINE_BG_Y), false, 0, FLOOR_SCALE_Q32, 0, 0, dark, bright);

    /* Sky 8..11: a quad facing the camera at the skybox depth. */
    {
        const int32_t *r = cam->rotation[0], *u = cam->rotation[1], *f = cam->rotation[2];
        const i64 radius = (i64)FZX_ENGINE_SKYBOX_DEPTH * cam->tan_half;
        const i64 hc = (radius * k->center_h) >> 16;
        const i64 vc = (((radius * k->aspect) >> 16) * k->center_v) >> 16;
        const i64 e[3] = { ex, ey, ez };
        i64 x[4], y[4], z[4];
        for (unsigned i = 0; i < 4; ++i) {
            const i64 vertical = i < 2 ? vr + vc : -(vr - vc);
            const i64 lateral = (i & 1) ? -(hr + hc) : hr - hc;
            i64 q[3];
            for (unsigned j = 0; j < 3; ++j)
                q[j] = clamp32000(e[j] + (((i64)f[j] * FZX_ENGINE_SKYBOX_DEPTH) >> 14) +
                                  (((i64)u[j] * vertical) >> 30) +
                                  ((k->lateral_sign * (i64)r[j] * lateral) >> 30));
            x[i] = q[0]; y[i] = q[1]; z[i] = q[2];
        }
        for (unsigned i = 0; i < 4; ++i) {
            RefBackdropVertex *v = &out->vertices[8 + i];
            const i64 tt = ((x[i] - x[0]) * 512 * 65536) / (2 * hr) - 32768;
            const i64 ts = Q16(64) - ((y[i] - Q16(FZX_ENGINE_BG_Y)) * 64 * 65536) / (2 * vr) - 32768;
            v->position[0] = (short)tz(x[i], 16);
            v->position[1] = (short)tz(y[i], 16);
            v->position[2] = (short)tz(z[i], 16);
            v->uv[0] = (short)tz(ts, 11);
            v->uv[1] = (short)tz(tt, 11);
            v->color[3] = 255;
        }
    }

    /* Clouds 12..19: as the floor, at the cloud height or a fixed height above
     * the eye, fading as the eye nears the layer. Two scroll steps per tick. */
    {
        i64 h = Q16(FZX_ENGINE_BG_Y + FZX_ENGINE_CLOUD_BG_HEIGHT) - ey;
        if (h == 0) h = 1;
        const i64 mag = h < 0 ? -h : h;
        const uint8_t a = mag >= Q16(100) ? 0 : (uint8_t)(((Q16(100) - mag) * 255) / Q16(100));
        const bool rel = h > 0;
        const i64 y = rel ? ey + Q16(FZX_ENGINE_CLOUD_EYE_HEIGHT) : Q16(FZX_ENGINE_BG_Y + FZX_ENGINE_CLOUD_BG_HEIGHT);
        const i64 ratio = h / FZX_ENGINE_CLOUD_EYE_HEIGHT;
        const uint8_t solid[4] = { 255, 255, 255, 255 }, faded[4] = { 255, 255, 255, a };
        const i64 n1 = (i64)tick * 2 - 1, n2 = (i64)tick * 2;
        plane_quad(&out->vertices[12], &p, xo, zo, 0, 0, y, rel, ratio, CLOUD_SCALE_Q32,
                   wrap1024((n1 * CLOUD_SPEED_X_Q32) >> 16), wrap1024((n1 * CLOUD_SPEED_Z_Q32) >> 16), solid, faded);
        plane_quad(&out->vertices[16], &p, 0, 0, -xo, -zo, y, rel, ratio, CLOUD_SCALE_Q32,
                   wrap1024((n2 * CLOUD_SPEED_X_Q32) >> 16), wrap1024((n2 * CLOUD_SPEED_Z_Q32) >> 16), faded, solid);
    }
    out->count = 20;
    return true;
}

bool fzx_engine_skyline(RefSkyline *out, const fzx_engine_cam_t *cam) {
    /* Ring positions relative to the eye: -sin(a)*8000, -cos(a)*8000 for
     * a = 0, 60, 120, 150, 240, 300 degrees, Q16.16. */
    static const int32_t ring[REF_SKYLINE_COUNT][2] = {
        { 0, -524288000 }, { -454046727, -262144000 }, { -454046727, 262144000 },
        { -262144000, 454046727 }, { 454046727, 262144000 }, { 454046727, -262144000 } };
    static const uint8_t ids[REF_SKYLINE_COUNT] = { 1, 2, 3, 4, 1, 2 };
    static const short quad[4][5] = { { -32, 32, 0, 0, 255 }, { 32, 32, 0x800, 0, 255 },
                                      { -32, -32, 0, 0x800, 0 }, { 32, -32, 0x800, 0x800, 0 } };
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!cam) { out->faults = 1; return false; }
    const i64 fx = cam->rotation[2][0], fy = cam->rotation[2][1], fz = cam->rotation[2][2];
    const i64 nx = cam->xzn[0], nz = cam->xzn[1];
    const i64 h = (fx * nx + fz * nz) >> 30; /* horizontal length of forward, Q2.30 */
    int32_t m[12];
    const i64 s = FZX_ENGINE_SKYLINE_SCALE;
    if (h < 33953000) { /* looking straight up or down: an unrotated billboard */
        memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = (int32_t)Q16(s);
    } else {
        m[0] = (int32_t)((-nz * s) >> 14); m[1] = 0; m[2] = (int32_t)((nx * s) >> 14); m[3] = 0;
        m[4] = (int32_t)((((-nx * fy) >> 30) * s) >> 14); m[5] = (int32_t)((h * s) >> 14);
        m[6] = (int32_t)((((-nz * fy) >> 30) * s) >> 14); m[7] = 0;
        m[8] = (int32_t)((-fx * s) >> 14); m[9] = (int32_t)((-fy * s) >> 14); m[10] = (int32_t)((-fz * s) >> 14); m[11] = 0;
    }
    out->count = REF_SKYLINE_COUNT;
    out->tint[0] = 200; out->tint[1] = 130; out->tint[2] = 240; out->tint[3] = 255;
    for (unsigned i = 0; i < 4; ++i) {
        RefBackdropVertex *v = &out->vertices[i];
        v->position[0] = quad[i][0]; v->position[1] = quad[i][1];
        v->uv[0] = quad[i][2]; v->uv[1] = quad[i][3];
        v->color[0] = v->color[1] = v->color[2] = 255; v->color[3] = (uint8_t)quad[i][4];
    }
    for (unsigned i = 0; i < REF_SKYLINE_COUNT; ++i) {
        RefSkylineInstance *o = &out->instances[i];
        const i64 x = (i64)cam->eye[0] + ring[i][0], z = (i64)cam->eye[2] + ring[i][1];
        o->sprite_id = ids[i];
        o->visible = x <= Q16(FZX_ENGINE_SKYLINE_LIMIT) && x >= -Q16(FZX_ENGINE_SKYLINE_LIMIT) &&
                     z <= Q16(FZX_ENGINE_SKYLINE_LIMIT) && z >= -Q16(FZX_ENGINE_SKYLINE_LIMIT);
        if (!o->visible) continue;
        memcpy(o->matrix, m, sizeof(m));
        o->matrix[12] = (int)x; o->matrix[13] = (int)Q16(FZX_ENGINE_SKYLINE_Y); o->matrix[14] = (int)z;
        o->matrix[15] = 65536;
    }
    return true;
}

void fzx_engine_view(fzx_view_t *out, const fzx_engine_cam_t *cam, const fzx_engine_consts_t *k) {
    memcpy(out->rotation, cam->rotation, sizeof(out->rotation));
    memcpy(out->translation, cam->translation, sizeof(out->translation));
    out->focal[0] = cam->focal[0]; out->focal[1] = cam->focal[1];
    out->center[0] = k->center[0]; out->center[1] = k->center[1];
    out->near_plane = k->near_plane; out->far_plane = k->far_plane;
    memcpy(out->clip, k->clip, sizeof(out->clip));
}

static bool row_ok(const int32_t r[4]) {
    for (unsigned i = 0; i < 3; ++i) if (r[i] > 16383 || r[i] < -16383) return false;
    return r[3] <= 0x1fffffff && r[3] >= -0x1fffffff;
}

bool fzx_engine_pick_view(fzx_engine_pick_t *out, const fzx_engine_cam_t *cam,
                          const fzx_engine_consts_t *k) {
    if (!out || !cam || !k || k->viewport[0] <= 0 || k->viewport[1] <= 0 || k->anchor_shift > 8) return false;
    const int32_t *r = cam->rotation[0], *u = cam->rotation[1], *f = cam->rotation[2];
    const unsigned s = k->anchor_shift;
    /* Depth units are 2^-K source units: the largest scale that keeps the
     * depth coefficients (the projection's z scale, about 1) inside the pick's
     * 14-bit contract. A coarser scale moves group boundaries. */
    const unsigned K = 13u - s;
    /* The pick looks from behind the eye, so segments beside the camera stay in view. */
    i64 e[3], dr = 0, du = 0, df = 0;
    for (unsigned j = 0; j < 3; ++j) e[j] = cam->eye[j] - (((i64)f[j] * k->origin_distance) >> 30);
    for (unsigned j = 0; j < 3; ++j) { dr += (i64)r[j] * e[j]; du += (i64)u[j] * e[j]; df += (i64)f[j] * e[j]; }
    dr >>= 30; du >>= 30; df >>= 30; /* Q16.16 */
    const i64 p00 = cam->focal[0] / k->viewport[0], p11 = cam->focal[1] / k->viewport[1]; /* Q16.16 */
    /* Clip x = P00*right - P20*forward and y likewise: the projection's centre
     * offsets (k->center_h, k->center_v) lean the frustum. x, y and w share one
     * scale 2^E: the largest that keeps all three rows inside the contract. */
    const i64 tx = p00 * dr - (i64)k->center_h * df, ty = p11 * du - (i64)k->center_v * df; /* Q32.32 */
    unsigned E = 13;
    for (;; --E) {
        for (unsigned j = 0; j < 3; ++j) {
            out->rows[j] = (int32_t)((p00 * r[j] - (i64)k->center_h * f[j]) >> (16 + 30 - E));
            out->rows[4 + j] = (int32_t)((p11 * u[j] - (i64)k->center_v * f[j]) >> (16 + 30 - E));
            out->rows[8 + j] = (int32_t)((i64)f[j] >> (30 - E));
        }
        const i64 t0 = -(tx >> 16) >> (16 - E + s), t1 = -(ty >> 16) >> (16 - E + s), t2 = -df >> (16 - E + s);
        const bool fits = t0 >= -0x1FFFFFFF && t0 <= 0x1FFFFFFF && t1 >= -0x1FFFFFFF && t1 <= 0x1FFFFFFF &&
                          t2 >= -0x1FFFFFFF && t2 <= 0x1FFFFFFF;
        out->rows[3] = (int32_t)t0; out->rows[7] = (int32_t)t1; out->rows[11] = (int32_t)t2;
        if (fits && row_ok(out->rows) && row_ok(out->rows + 4) && row_ok(out->rows + 8)) break;
        if (E == 6) return false;
    }
    for (unsigned j = 0; j < 3; ++j) out->rows[12 + j] = (int32_t)(((i64)k->pa * f[j]) >> (60 - (K + s)));
    {
        const i64 t = (-(((i64)k->pa * df) >> 30) + ((i64)k->pb << 4)) >> (16 - K);
        if (t < -0x1FFFFFFF || t > 0x1FFFFFFF) return false;
        out->rows[15] = (int32_t)t;
    }
    const i64 far = (i64)k->far_distance >> (16 - K), split = (i64)k->split_distance >> (16 - K);
    if (far < 0 || far > 0x7FFFFFFF || split < 0 || split > 0x7FFFFFFF) return false;
    out->thresholds[0] = (int32_t)far;
    out->thresholds[1] = 30 << K;
    out->thresholds[2] = (int32_t)split;
    out->thresholds[3] = -(10000 << K);
    out->depth_shift = K;
    return row_ok(out->rows + 12);
}
