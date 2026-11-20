/* HOST check for saturn/engine/emit_yaw.h (sprite emit lane, E2_EMIT_YAW): the climb against the
 * library's scan (rspr_select_core's yaw loop) on a rotation-sprite set file (RSP1).
 *
 *   cc -O2 -Isaturn/engine tools/emit_yaw_host.c -o emit_yaw_host && ./emit_yaw_host SET.PAK [random|all]
 *
 * Checks every (x, z) with |x|, |z| <= 1536, a grid over the whole input range (step 37), `random`
 * points (default 20,000,000) from the whole range, or with `all` every (x, z) with |x|, |z| <= 32767
 * (about 4.3e9 directions), and both directions perpendicular to every edge
 * (where two bins tie), each climbed from four starts. Controls, each must be caught: a table with two
 * bins swapped is refused by emit_yaw_convex; a climb without the tie rule and a climb that only moves
 * up the index differ from the scan. The set is read at run time; nothing derived from it is written.
 * Exit 0 when every check passes and every control is caught. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emit_yaw.h"

enum { MAX_YAW = 64 };
static int16_t yaw[MAX_YAW][2];
static unsigned n;

static unsigned scan(int32_t x, int32_t z, int32_t *best) {
    unsigned k_best = 0;
    for (unsigned k = 0; k < n; ++k) {
        const int32_t v = yaw[k][0] * x + yaw[k][1] * z;
        if (k == 0 || v > *best) { *best = v; k_best = k; }
    }
    return k_best;
}
/* Controls: 1 no tie rule, 2 only climbs up the index. */
static unsigned mutant(int mode, unsigned start, int32_t x, int32_t z) {
    unsigned k = start;
    int32_t fk = yaw[k][0] * x + yaw[k][1] * z;
    for (;;) {
        const unsigned kn = k + 1u == n ? 0u : k + 1u, kp = k == 0u ? n - 1u : k - 1u;
        const int32_t fn = yaw[kn][0] * x + yaw[kn][1] * z;
        if (fn > fk) { k = kn; fk = fn; continue; }
        if (mode == 2) break;
        const int32_t fp = yaw[kp][0] * x + yaw[kp][1] * z;
        if (fp > fk) { k = kp; fk = fp; continue; }
        break;
    }
    return k;
}
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }

static unsigned long long checked, bad, caught_tie, caught_up;
static void check(int32_t x, int32_t z) {
    if (!x && !z) return;
    int32_t sb = 0;
    const unsigned s = scan(x, z, &sb);
    const unsigned starts[4] = { (s + n / 2u) % n, next() % n, 0u, (s + 1u) % n };
    for (unsigned i = 0; i < 4u; ++i) {
        int32_t cb = 0;
        const unsigned c = emit_yaw_climb((const int16_t (*)[2])yaw, n, starts[i], x, z, &cb);
        ++checked;
        if (c != s || cb != sb) {
            if (bad < 5u) printf("MISMATCH x %d z %d start %u: scan %u (%d) climb %u (%d)\n", x, z, starts[i], s, sb, c, cb);
            ++bad;
        }
        if (mutant(1, starts[i], x, z) != s) ++caught_tie;
        if (mutant(2, starts[i], x, z) != s) ++caught_up;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s SET.PAK [random]\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    unsigned char h[24 + 4 * MAX_YAW];
    if (!f || fread(h, 1, sizeof(h), f) != sizeof(h)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    fclose(f);
    if (memcmp(h, "RSP1", 4)) { fprintf(stderr, "not an RSP1 set\n"); return 2; }
    n = (unsigned)(h[8] << 8 | h[9]);
    const unsigned flags = (unsigned)(h[12] << 8 | h[13]);
    if (n < 1u || n > MAX_YAW) { fprintf(stderr, "yaw count %u\n", n); return 2; }
    for (unsigned k = 0; k < n; ++k)
        for (unsigned c = 0; c < 2u; ++c) yaw[k][c] = (int16_t)(h[24 + 4 * k + 2 * c] << 8 | h[25 + 4 * k + 2 * c]);
    const bool convex = !(flags & 1u) && emit_yaw_convex((const int16_t (*)[2])yaw, n);
    printf("set %s: %u yaw bins, flags %u, convex %s\n", argv[1], n, flags, convex ? "yes (climb)" : "no (scan)");
    int fail = 0;
    /* Control: two bins swapped must be refused. */
    {
        int16_t t[MAX_YAW][2];
        memcpy(t, yaw, sizeof(t));
        const int16_t a0 = t[1][0], a1 = t[1][1];
        t[1][0] = t[2][0]; t[1][1] = t[2][1]; t[2][0] = a0; t[2][1] = a1;
        const bool refused = !emit_yaw_convex((const int16_t (*)[2])t, n);
        printf("control swapped bins: %s\n", refused ? "refused (caught)" : "ACCEPTED (control failed)");
        if (!refused) fail = 1;
    }
    if (!convex) { printf("the climb is not used for this set\n"); return fail; }
    const bool all = argc > 2 && !strcmp(argv[2], "all");
    const unsigned long long random = argc > 2 && !all ? strtoull(argv[2], 0, 10) : 20000000ull;
    if (all) for (int32_t x = -32767; x <= 32767; ++x) for (int32_t z = -32767; z <= 32767; ++z) check(x, z);
    for (int32_t x = -1536; x <= 1536; ++x) for (int32_t z = -1536; z <= 1536; ++z) check(x, z);
    for (int32_t x = -32767; x <= 32767; x += 37) for (int32_t z = -32767; z <= 32767; z += 37) check(x, z);
    for (unsigned long long i = 0; i < random; ++i) check((int32_t)(next() % 65535u) - 32767, (int32_t)(next() % 65535u) - 32767);
    unsigned long long ties = 0, tie_caught_before = caught_tie;
    for (unsigned k = 0; k < n; ++k) {
        const unsigned k1 = (k + 1u) % n;
        const int32_t dx = yaw[k1][0] - yaw[k][0], dz = yaw[k1][1] - yaw[k][1];
        int32_t px = dz, pz = -dx;
        while (px > 32767 || px < -32767 || pz > 32767 || pz < -32767) { px /= 2; pz /= 2; }
        check(px, pz); check(-px, -pz); ties += 2;
    }
    printf("checked %llu climbs (%llu edge-perpendicular directions): mismatches %llu\n", checked, ties, bad);
    printf("control no tie rule: %llu differ%s; control climbs up only: %llu differ\n", caught_tie,
           caught_tie > tie_caught_before ? " (some at the edge directions)" : "", caught_up);
    if (bad) fail = 1;
    if (!caught_tie || !caught_up) { printf("a control was not caught\n"); fail = 1; }
    printf("%s\n", fail ? "FAIL" : "PASS");
    return fail;
}
