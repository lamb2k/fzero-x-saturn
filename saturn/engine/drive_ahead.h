/* Test driver R03, "look ahead" (engine_drive.c E1_DRIVER=2; tools/physics_host fzx-phys --driver-ahead).
 *
 * The original CPU driver's steering (cpu.c Cpu_GenerateInputs, road spans): the stick is
 *   v = 1.2 * (0.3 * lateral + 360 * heading_ahead), clamped to +-70, stick = trunc(v * 5 / 8),
 * with LEAN_R held while v > 70 and LEAN_L while v < -70. heading_ahead is the sine of the
 * angle between the machine's nose and the road direction at a point `0.1 * speed` ticks of
 * travel ahead (unk_330 = speed * 0.1). R01 is the same law with no look-ahead, kl 0.004 and
 * kh 2.5 in full-stick units (0.3 * 1.2 * 5 / 8 / 63 = 0.0036; 360 * 1.2 * 5 / 8 / 63 = 4.3).
 * Simplifications against the original (a test driver, not the port's CPU): the racing line
 * target is 0 (the centre), the stick updates every tick (not every fourth), and pipes,
 * cylinders and jumps use the road law.
 *
 * Shared by the SH-2 image and the HOST tool so both compute the same inputs: single-precision
 * IEEE arithmetic only (+, -, *, /; the square root is three Newton steps from a bit-pattern guess), no library
 * calls (the SH-2 image links no libm). */
#ifndef FZX_DRIVE_AHEAD_H
#define FZX_DRIVE_AHEAD_H
#include <stdint.h>
#include <string.h>
#include "sh2zam/vehicle_dynamics_experimental.h"

#define FZX_DRIVE_AHEAD_LEAN_L 0x2000u /* N64 Z (fzx_phys_buttons_n64: LEAN_L) */
#define FZX_DRIVE_AHEAD_LEAN_R 0x0010u /* N64 R (LEAN_R) */

static inline float fzx_drive_ahead_sqrt(float x) {
    uint32_t i;
    float y;
    memcpy(&i, &x, 4);
    i = 0x1fbd1df5u + (i >> 1);
    memcpy(&y, &i, 4);
    for (int k = 0; k < 3; ++k) y = 0.5f * (y + x / y);
    return y;
}

/* Returns the stick x for this tick and ORs the lean button (N64 bits) into *buttons. */
static inline signed char fzx_drive_ahead(const sh2z_vd_track_t *track, const sh2z_vd_body_t *b,
                                          float look, uint16_t *buttons) {
    const float q16 = 1.0f / 65536.0f, q30 = 1.0f / 1073741824.0f;
    const float lateral = (float)sh2z_vd_body_lateral(b) * q16;
    const float speed = (float)b->speed * q16;
    /* Walk the spline: distance ahead in units, span by span (t taken as the arc share). */
    float d = look * speed * (speed > 0.0f ? speed : 0.0f);
    uint32_t n = b->node < track->count ? b->node : 0u;
    float t = (float)b->t * q30;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    for (unsigned k = 0; k < 8u && d > 0.0f; ++k) {
        const float len = (float)track->nodes[n].length * (1.0f / 4096.0f);
        const float rest = (1.0f - t) * len;
        if (d < rest) { t += d / len; d = 0.0f; }
        else { d -= rest; t = 0.0f; n = n + 1u < track->count ? n + 1u : 0u; }
    }
    int32_t pos[3], fwd[3];
    sh2z_vd_spline(track, n, (int32_t)(t * 1073741824.0f), pos, fwd);
    const sh2z_vd_node_t *a = &track->nodes[n], *c = &track->nodes[n + 1u < track->count ? n + 1u : 0u];
    float f[3], u[3], l[3];
    for (int i = 0; i < 3; ++i) {
        f[i] = (float)fwd[i] * q16;
        u[i] = ((float)a->up[i] + ((float)c->up[i] - (float)a->up[i]) * t) * q30;
    }
    /* Road left ahead = up x forward (rows forward, up, left), made unit length. */
    l[0] = u[1] * f[2] - u[2] * f[1];
    l[1] = u[2] * f[0] - u[0] * f[2];
    l[2] = u[0] * f[1] - u[1] * f[0];
    const float m2 = l[0] * l[0] + l[1] * l[1] + l[2] * l[2];
    float heading = 0.0f;
    if (m2 > 0.0f) {
        heading = ((float)b->body[0][0] * l[0] + (float)b->body[0][1] * l[1] + (float)b->body[0][2] * l[2]) * q30;
        heading = heading / fzx_drive_ahead_sqrt(m2);
    }
    float v = 1.2f * (0.3f * lateral + 360.0f * heading);
    if (v > 70.0f) { v = 70.0f; *buttons |= FZX_DRIVE_AHEAD_LEAN_R; }
    else if (v < -70.0f) { v = -70.0f; *buttons |= FZX_DRIVE_AHEAD_LEAN_L; }
    const int r = v >= 0.0f ? (int)(v + 0.5f) : -(int)(-v + 0.5f); /* Math_Round */
    return (signed char)((r * 5) / 8);
}
#endif
