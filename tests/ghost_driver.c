/* HOST fixture (tests/test_modes.py): the Time Attack ghost codec and container (saturn/engine/engine_ghost.c).
 * usage: ghost_driver [TRAJECTORY.tsv]  (the fzx-phys trace of one input: columns tick..., x y z at 20..22, race_time at 39)
 * Prints one PASS/FAIL line per check, and the stream size of the trajectory when given. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_ghost.h"

static int bad;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) bad = 1; }
static fzx_ghost_t rec_g, copy_g;

static int32_t q16(double units) { return (int32_t)floor(units * 65536.0 + 0.5); }
/* As the engine: the position is a Q16 integer first, then rounded to whole units (halves up). */
static int32_t round_units(double v) { return (q16(v) + 0x8000) >> 16; }

/* Records the positions (one per tick), then plays the ghost back and compares tick by tick: even ticks are the sample (whole
 * units), odd ticks the midpoint of the neighbours. Returns the number of mismatches. */
static unsigned roundtrip(const double (*p)[3], unsigned n, uint32_t *bytes) {
    fzx_ghost_rec_begin(&rec_g, 3u);
    for (unsigned t = 0; t < n; ++t) { int32_t q[3] = { q16(p[t][0]), q16(p[t][1]), q16(p[t][2]) }; fzx_ghost_rec_tick(&rec_g, q); }
    if (!fzx_ghost_rec_end(&rec_g, 100000u)) { printf("INFO rec_end refused (over %d)\n", (int)fzx_ghost_rec_overrun(&rec_g)); return n + 1u; }
    *bytes = rec_g.bytes;
    if (!fzx_ghost_valid(&rec_g, FZX_GHOST_FILE_BYTES(&rec_g))) { printf("INFO valid refused bytes %u samples %u\n", rec_g.bytes, rec_g.samples); return n + 2u; }
    fzx_ghost_play_t pl;
    fzx_ghost_play_begin(&pl, &rec_g);
    unsigned bad_ticks = 0;
    for (unsigned t = 0; t + 1u < n; ++t) {
        int32_t o[3];
        if (!fzx_ghost_play_tick(&pl, o)) { ++bad_ticks; continue; }
        for (int i = 0; i < 3; ++i) {
            const int32_t a = round_units(p[t & ~1u][i]);
            int32_t want;
            if (t & 1u) { const int32_t b = round_units(p[(t & ~1u) + 2u < n ? (t & ~1u) + 2u : (t & ~1u)][i]); want = (int32_t)(((int64_t)a + b) << 15); }
            else want = (int32_t)((int64_t)a << 16);
            if (o[i] != want) { if (!bad_ticks) printf("INFO first mismatch tick %u axis %d: got %d want %d\n", t, i, o[i], want); ++bad_ticks; break; }
        }
    }
    return bad_ticks;
}

int main(int argc, char **argv) {
    static double traj[9000][3];
    /* a synthetic drive: smooth turns, a few jumps over the escape range */
    uint32_t s = 12345u; double x = -3000, y = 0, z = 2500, vx = 1, vy = 0, vz = -1;
    for (unsigned t = 0; t < 9000u; ++t) {
        s = s * 1664525u + 1013904223u;
        vx += ((int)(s >> 24) - 128) / 400.0; vz += ((int)((s >> 16) & 255) - 128) / 500.0; vy += ((int)((s >> 8) & 255) - 128) / 900.0;
        if (t == 4000u) vx += 12.0;             /* a launch: second difference past the nibble range */
        x += vx / 2; y += vy / 2; z += vz / 2; traj[t][0] = x; traj[t][1] = y; traj[t][2] = z;
    }
    uint32_t bytes = 0;
    check(roundtrip((const double (*)[3])traj, 6200u, &bytes) == 0, "synthetic drive: playback equals the recorded samples, odd ticks the midpoint");
    printf("INFO synthetic 6200 ticks: %u bytes\n", bytes);
    check(bytes < 12000u, "synthetic stream fits the cap");

    /* validation: every damage is refused */
    const uint32_t size = FZX_GHOST_FILE_BYTES(&rec_g);
    memcpy(&copy_g, &rec_g, sizeof(copy_g));
    check(fzx_ghost_valid(&copy_g, size), "intact ghost accepted");
    copy_g.stream[size / 3] ^= 0x10u;
    check(!fzx_ghost_valid(&copy_g, size), "control: one flipped bit refused (crc)");
    memcpy(&copy_g, &rec_g, sizeof(copy_g));
    check(!fzx_ghost_valid(&copy_g, size / 2), "control: a file cut in half refused (size)");
    memcpy(&copy_g, &rec_g, sizeof(copy_g));
    copy_g.bytes = copy_g.bytes / 2u; fzx_ghost_stamp(&copy_g, 7u);
    check(!fzx_ghost_valid(&copy_g, FZX_GHOST_FILE_BYTES(&copy_g)), "control: a short stream with a valid crc refused (sample count)");
    memcpy(&copy_g, &rec_g, sizeof(copy_g));
    copy_g.magic ^= 1u;
    check(!fzx_ghost_valid(&copy_g, size), "control: wrong magic refused");
    memcpy(&copy_g, &rec_g, sizeof(copy_g));
    fzx_ghost_stamp(&copy_g, 9u);
    check(fzx_ghost_valid(&copy_g, size) && copy_g.generation == 9u, "stamping a generation keeps the ghost valid");

    /* the cap: a drive that cannot fit is not kept */
    static double wild[9000][3];
    for (unsigned t = 0; t < 9000u; ++t) { s = s * 1664525u + 1013904223u; wild[t][0] = (double)(int)((s >> 20) & 0xFFFu) * 5.0 - 10000.0; wild[t][1] = (double)(int)((s >> 8) & 0x3FFFu) - 8000.0; wild[t][2] = (double)(int)(s & 0xFFFFu) - 30000.0; }
    fzx_ghost_rec_begin(&rec_g, 1u);
    for (unsigned t = 0; t < 9000u; ++t) { int32_t q[3] = { q16(wild[t][0]), q16(wild[t][1]), q16(wild[t][2]) }; fzx_ghost_rec_tick(&rec_g, q); }
    check(!fzx_ghost_rec_end(&rec_g, 1u) && fzx_ghost_rec_overrun(&rec_g), "a recording past the cap is dropped, not kept");

    if (argc > 1) {
        static double tr[12000][3];
        FILE *f = fopen(argv[1], "r");
        unsigned n = 0; char line[2048];
        while (f && fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            char *tok[48]; unsigned k = 0; for (char *c = strtok(line, "\t\n"); c && k < 48; c = strtok(NULL, "\t\n")) tok[k++] = c;
            if (k < 40 || atoi(tok[39]) <= 0 || n >= 12000u) continue;
            for (int i = 0; i < 3; ++i) tr[n][i] = atof(tok[20 + i]);
            if (strtoul(tok[28], NULL, 16) & (1u << 25)) break;
            ++n;
        }
        if (f) fclose(f);
        const unsigned mism = roundtrip((const double (*)[3])tr, n, &bytes);
        printf("INFO trajectory %u ticks: %u bytes (the source format: about %u)\n", n, bytes, 3u * (n / 2u));
        check(n > 1000u && mism == 0, "HOST trajectory (fzx-phys, same input): playback equals the HOST positions at every tick");
    }
    return bad;
}
