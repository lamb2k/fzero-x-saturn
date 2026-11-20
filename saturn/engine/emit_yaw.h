/* Sprite emit lane (E2_EMIT_YAW): the rotation-sprite yaw bin by climbing instead of scanning.
 *
 * The scan (SH2ZAM rotation-sprites-2, rspr_select_core) picks the first bin k with the largest
 * yaw[k][0] * x + yaw[k][1] * z. When the bins, in index order, are the corners of a strictly convex
 * polygon wound once around the origin, these exact integer products rise to one maximum (a corner, or
 * one edge of two equal corners) and fall to one minimum around the polygon. So a climb from any bin
 * that stops where neither neighbour is larger stands on the maximum, and the lowest index of an equal
 * neighbour is the scan's tie rule. emit_yaw_convex says whether a table qualifies; the caller scans
 * otherwise. HOST check against the scan: tools/emit_yaw_host.c. */
#ifndef FZX_ENGINE_EMIT_YAW_H
#define FZX_ENGINE_EMIT_YAW_H
#include <stdbool.h>
#include <stdint.h>

static inline int emit_yaw_half(const int16_t v[2]) { return (v[1] > 0 || (v[1] == 0 && v[0] > 0)) ? 0 : 1; }
static inline int64_t emit_yaw_cross(const int16_t a[2], const int16_t b[2]) {
    return (int64_t)a[0] * b[1] - (int64_t)a[1] * b[0];
}
/* a before b in polar angle from the +[0] axis, counter-clockwise, in [0, 2 pi). */
static inline bool emit_yaw_before(const int16_t a[2], const int16_t b[2]) {
    const int ha = emit_yaw_half(a), hb = emit_yaw_half(b);
    return ha < hb || (ha == hb && emit_yaw_cross(a, b) > 0);
}
/* True when yaw[0..n) in order is a strictly convex polygon wound once around the origin: every turn
 * and every step around the origin has the same strict sign, and the polar angle wraps exactly once. */
static inline bool emit_yaw_convex(const int16_t (*yaw)[2], unsigned n) {
    if (n < 3u) return false;
    int sign = 0;
    unsigned wraps = 0;
    for (unsigned k = 0; k < n; ++k) {
        const unsigned k1 = k + 1u < n ? k + 1u : k + 1u - n, k2 = k + 2u < n ? k + 2u : k + 2u - n;
        const int16_t *a = yaw[k], *b = yaw[k1], *c = yaw[k2];
        const int64_t turn = (int64_t)(b[0] - a[0]) * (c[1] - b[1]) - (int64_t)(b[1] - a[1]) * (c[0] - b[0]);
        const int64_t step = emit_yaw_cross(a, b);
        const int st = turn > 0 ? 1 : turn < 0 ? -1 : 0, ss = step > 0 ? 1 : step < 0 ? -1 : 0;
        if (!st || ss != st || (sign && st != sign)) return false;
        sign = st;
        if (sign > 0 ? !emit_yaw_before(a, b) : !emit_yaw_before(b, a)) ++wraps;
    }
    return wraps == 1u;
}
/* The scan's bin for (x, z), climbing from start (any bin); *best receives its product. Only for a
 * table emit_yaw_convex accepted; |x|, |z| < 2^15 as the scan's inputs. */
static inline unsigned emit_yaw_climb(const int16_t (*yaw)[2], unsigned n, unsigned start, int32_t x, int32_t z,
                                      int32_t *best) {
    unsigned k = start < n ? start : 0u;
    int32_t fk = yaw[k][0] * x + yaw[k][1] * z;
    for (;;) {
        const unsigned kn = k + 1u == n ? 0u : k + 1u, kp = k == 0u ? n - 1u : k - 1u;
        const int32_t fn = yaw[kn][0] * x + yaw[kn][1] * z;
        if (fn > fk) { k = kn; fk = fn; continue; }
        const int32_t fp = yaw[kp][0] * x + yaw[kp][1] * z;
        if (fp > fk) { k = kp; fk = fp; continue; }
        if (fn == fk && kn < k) k = kn;
        if (fp == fk && kp < k) k = kp;
        break;
    }
    *best = fk;
    return k;
}
#endif
