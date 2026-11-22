/* HOST probe of the library's coast damping (attacks lane, C-136 bound): includes the vehicle-dynamics source so its
 * static damp() runs as shipped. Reads lines "b00 .. b22 vx vy vz ks ku kf crashed" (basis rows Q30 forward, up, side;
 * velocity Q16; gains Q30) and prints the velocity after one damp() call. Test tool only; MIT; see ../../LICENSE.
 *   build: cc -I$S/include -I$S/src damp_probe.c $S/src/sh2zam_unit30.c $S/src/sh2zam_divu.c -o damp-probe */
#include "sh2zam_vehicle_dynamics.c"
#include <stdio.h>
#include <string.h>

int main(void) {
    sh2z_vd_body_t b;
    sh2z_vd_rules_t r;
    long v[12], k[3];
    int crashed;
    while (scanf("%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                 &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &k[0], &k[1], &k[2], &crashed) == 16) {
        int i;
        memset(&b, 0, sizeof b);
        memset(&r, 0, sizeof r);
        for (i = 0; i < 9; i++) b.body[i / 3][i % 3] = (int32_t)v[i];
        for (i = 0; i < 3; i++) b.vel[i] = (int32_t)v[9 + i];
        r.damp_crashed = (int32_t)k[0]; r.damp_up = (int32_t)k[1]; r.damp_side = r.damp_fwd = (int32_t)k[2];
        b.flags = crashed ? SH2Z_VD_CRASHED : 0u;
        damp(&b, &r, 0);
        printf("%ld %ld %ld\n", (long)b.vel[0], (long)b.vel[1], (long)b.vel[2]);
    }
    return 0;
}
