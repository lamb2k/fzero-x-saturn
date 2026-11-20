/* HOST check for src/fzx_band_corners.c (ENG_DSP_BANDS 1 or 2; 2 uses SH2ZAM dsp-1's
 * portable form, which equals the DSP kernel by SH2ZAM's tests). Every corner and rail
 * point must equal the original int64 expression of fzx_floor_rbg.c / fzx_clouds_rbg.c.
 * No game data: random view-space positions, the real fraction rules, edge values.
 * Build: gcc -O2 -DENG_DSP_BANDS=N -Iinclude -I<SH2ZAM>/saturn/include tools/band_corners_host.c
 *        src/fzx_band_corners.c [<SH2ZAM>/saturn/src/sh2zam_dsp.c]
 * --control flips one result after each call: the check must fail. */
#include <stdio.h>
#include <string.h>
#include "fzx_band_corners.h"

static uint32_t s = 0x2545f491u;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static int32_t pick(unsigned kind) {
    static const int32_t edge[] = {0, 1, -1, 65535, -65536, 0x3fffffff, -0x40000000, 0x7fffffff, (int32_t)0x80000000, 123456789};
    if (kind == 0) return (int32_t)rnd();
    if (kind == 1) return (int32_t)(rnd() % 400000001u) - 200000000;
    return edge[rnd() % 10u];
}
static int32_t original(int32_t base, int32_t to, int32_t f) {
    return base + (int32_t)(((int64_t)to - base) * f / 65536);
}

int main(int argc, char **argv) {
    const int control = argc > 1 && !strcmp(argv[1], "--control");
    unsigned long long checked = 0, bad = 0;
    for (unsigned rep = 0; rep < 200000u; ++rep) {
        int32_t pos[8][3];
        const unsigned kind = rep % 3u;
        for (unsigned i = 0; i < 8; ++i) for (unsigned j = 0; j < 3; ++j) pos[i][j] = pick(kind);
        const unsigned surface = rep & 1u;
        fzx_band_fractions_t f;
        if (surface) fzx_band_fractions_clouds(&f, (int32_t)(rnd() % 255u)); else fzx_band_fractions_floor(&f);
        fzx_band_begin(surface, (const int32_t (*)[3])pos, &f);
        for (unsigned half = 0; half < 2; ++half) for (unsigned band = 1; band < 8; ++band) {
            if (f.skip[band - 1]) continue;
            const int32_t fr[4] = {f.low[band - 1], f.low[band - 1], f.high[band - 1], f.high[band - 1]};
            static const unsigned sides[4] = {0, 1, 1, 0};
            int32_t outer[4][3];
            fzx_band_corners(surface, outer, (const int32_t (*)[3])pos, half, band, fr);
            if (control) outer[rep % 4u][rep % 3u] ^= 1;
            for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 3; ++j, ++checked)
                bad += outer[i][j] != original(pos[2 + 2 * half + sides[i]][j], pos[6 * half + sides[i]][j], fr[i]);
            for (unsigned side = 0; side < 2; ++side) {
                int32_t p[3];
                fzx_band_point(surface, p, (const int32_t (*)[3])pos, half, side, fr[side ? 2 : 0]);
                for (unsigned j = 0; j < 3; ++j, ++checked)
                    bad += p[j] != original(pos[2 + 2 * half + side][j], pos[6 * half + side][j], fr[side ? 2 : 0]);
            }
        }
    }
    printf("ENG_DSP_BANDS=%d: %llu coordinates, %llu differ from the int64 expression; stats jobs %u fallbacks %u\n",
           ENG_DSP_BANDS, checked, bad, (unsigned)fzx_band_stats[1], (unsigned)fzx_band_stats[2]);
    return bad ? 1 : 0;
}
