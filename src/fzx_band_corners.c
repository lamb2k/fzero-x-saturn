/* Fog-band corners (see fzx_band_corners.h). ENG_DSP_BANDS 1: SH-2 exact 32-bit form;
 * 2: the products on the SCU DSP (SH2ZAM dsp-1 scale16), the SH-2 form for anything
 * outside the job. Both bit-identical to the original 64-bit expression. */
#include "fzx_band_corners.h"
#if ENG_DSP_BANDS
#include <stddef.h>
#if ENG_DSP_BANDS == 2
#include "sh2zam/dsp_experimental.h"
#endif

volatile uint32_t fzx_band_stats[8];

static const uint8_t sides[4] = {0, 1, 1, 0};

/* base + trunc((to - base) * f / 65536), exactly as the original int64 expression. */
static inline int32_t lerp_exact(int32_t base, int32_t to, int32_t f) {
    const int64_t d = (int64_t)to - base;
    if (d >= INT32_MIN && d <= INT32_MAX) {
        const int64_t p = (int64_t)(int32_t)d * f;   /* one dmuls.l; |p| < 2^47 */
        int32_t q = (int32_t)(p >> 16);              /* floor */
        if (p < 0 && (p & 0xffff)) ++q;              /* toward zero */
        return base + q;
    }
    ++fzx_band_stats[2];
    return base + (int32_t)(d * f / 65536);
}

void fzx_band_fractions_floor(fzx_band_fractions_t *f) {
    for (unsigned band = 1; band < 8; ++band) {
        f->low[band - 1] = (int32_t)(2 * band - 1) * 65536 / 14;
        f->high[band - 1] = band == 7 ? 65536 : (int32_t)(2 * band + 1) * 65536 / 14;
        f->skip[band - 1] = 0;
    }
}

void fzx_band_fractions_clouds(fzx_band_fractions_t *f, int32_t inner) {
    for (unsigned band = 1; band < 8; ++band) {
        const int32_t alpha_low = (int32_t)(2 * band - 1) * 255 / 14, alpha_high = band == 7 ? 255 : (int32_t)(2 * band + 1) * 255 / 14;
        int32_t low = (int32_t)((int64_t)(alpha_low - inner) * 65536 / (255 - inner));
        int32_t high = band == 7 ? 65536 : (int32_t)((int64_t)(alpha_high - inner) * 65536 / (255 - inner));
        uint8_t skip = high <= 0;
        if (low < 0) low = 0;
        if (high > 65536) high = 65536;
        if (low >= high) skip = 1;
        f->low[band - 1] = low; f->high[band - 1] = high; f->skip[band - 1] = skip;
    }
}

#if ENG_DSP_BANDS == 2
/* Diagnostics (default 0): CHECK recomputes every DSP corner on the SH-2 and counts
 * mismatches in fzx_band_stats[7]; CORRUPT (control) alters one kernel word after load. */
#ifndef ENG_DSP_BANDS_CHECK
#define ENG_DSP_BANDS_CHECK 0
#endif
#ifndef ENG_DSP_BANDS_CORRUPT
#define ENG_DSP_BANDS_CORRUPT 0
#endif
enum { DELTAS = 12, FRACS = 14 };
static sh2z_dsp_scale16_t job[2] __attribute__((aligned(16)));
static int32_t products[2][DELTAS * FRACS] __attribute__((aligned(16)));
static int32_t job_fraction[2][FRACS];
static uint8_t negative[2][DELTAS], state[2], loaded;
#define POLL_LIMIT 200000u
/* The DSP's results, read cache-through on the SH-2 (never cached, so no purge). */
#if SH2Z_DSP_NATIVE
#define DSP_RESULTS(p) ((const volatile int32_t *)((uintptr_t)(p) | 0x20000000u))
#else
#define DSP_RESULTS(p) ((const volatile int32_t *)(p))
#endif

static void dsp_idle(void) {
    uint32_t polls = 0;
    while (sh2z_dsp_busy() && polls < POLL_LIMIT) ++polls;
    if (polls > fzx_band_stats[3]) fzx_band_stats[3] = polls;
}

void fzx_band_begin(unsigned surface, const int32_t positions[8][3], const fzx_band_fractions_t *f) {
    ++fzx_band_stats[0];
    state[surface] = 0;
    if (!loaded) {
        dsp_idle(); loaded = (sh2z_dsp_scale16_load() || !SH2Z_DSP_NATIVE) ? 1u : 0u; ++fzx_band_stats[5]; /* portable: the CPU form */
#if ENG_DSP_BANDS_CORRUPT
        /* Control: the product's 48-bit add becomes a 32-bit ADD (the SH2ZAM host test's first scale16 mutation). */
        uint32_t p[SH2Z_DSP_PROGRAM_WORDS];
        for (uint32_t i = 0; i < sh2z_dsp_scale16_program_words; ++i)
            p[i] = sh2z_dsp_scale16_program[i] == 0x18040000u ? 0x10040000u : sh2z_dsp_scale16_program[i];
        loaded = sh2z_dsp_load(p, sh2z_dsp_scale16_program_words) ? 1u : 0u;
#endif
        if (!loaded) return;
    }
    int32_t a[DELTAS], b[FRACS];
    for (unsigned half = 0; half < 2; ++half) for (unsigned side = 0; side < 2; ++side) for (unsigned j = 0; j < 3; ++j) {
        const unsigned k = (half * 2 + side) * 3 + j;
        const int64_t d = (int64_t)positions[6 * half + side][j] - positions[2 + 2 * half + side][j];
        if (d <= INT32_MIN || d > INT32_MAX) return; /* the SH-2 form for this surface */
        negative[surface][k] = d < 0;
        a[k] = (int32_t)(d < 0 ? -d : d);
    }
    for (unsigned band = 0; band < 7; ++band) {
        b[2 * band] = f->skip[band] ? 0 : f->low[band];
        b[2 * band + 1] = f->skip[band] ? 0 : f->high[band];
        job_fraction[surface][2 * band] = f->skip[band] ? -1 : f->low[band];
        job_fraction[surface][2 * band + 1] = f->skip[band] ? -1 : f->high[band];
    }
    dsp_idle();
    if (!sh2z_dsp_scale16_prepare(&job[surface], a, DELTAS, b, FRACS, products[surface])) { ++fzx_band_stats[6]; return; }
    sh2z_dsp_scale16_start(&job[surface]);
    state[surface] = 1;
    ++fzx_band_stats[1];
}

void fzx_band_corners(unsigned surface, int32_t outer[4][3], const int32_t positions[8][3],
                      unsigned half, unsigned band, const int32_t fractions[4]) {
    if (state[surface] == 1) { dsp_idle(); state[surface] = sh2z_dsp_busy() ? 0u : 2u; }
    /* Results are read cache-through: never cached, so no purge. */
    const volatile int32_t *const q = DSP_RESULTS(products[surface]);
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned side = sides[i], ib = (band - 1) * 2 + (i >= 2);
        const int32_t *base = positions[2 + 2 * half + side], *to = positions[6 * half + side];
        if (state[surface] == 2 && job_fraction[surface][ib] == fractions[i]) {
            for (unsigned j = 0; j < 3; ++j) {
                const unsigned k = (half * 2 + side) * 3 + j;
                const int32_t v = q[k * FRACS + ib];
                outer[i][j] = base[j] + (negative[surface][k] ? -v : v);
#if ENG_DSP_BANDS_CHECK
                if (outer[i][j] != lerp_exact(base[j], to[j], fractions[i])) ++fzx_band_stats[7];
#endif
            }
        } else {
            for (unsigned j = 0; j < 3; ++j) outer[i][j] = lerp_exact(base[j], to[j], fractions[i]);
            fzx_band_stats[2] += 3;
        }
    }
    fzx_band_stats[4] += 12;
}

void fzx_band_point(unsigned surface, int32_t p[3], const int32_t positions[8][3], unsigned half, unsigned side, int32_t fraction) {
    if (state[surface] == 1) { dsp_idle(); state[surface] = sh2z_dsp_busy() ? 0u : 2u; }
    const int32_t *base = positions[2 + 2 * half + side], *to = positions[6 * half + side];
    unsigned ib = FRACS;
    if (state[surface] == 2)
        for (unsigned k = 0; k < FRACS; ++k) if (job_fraction[surface][k] == fraction) { ib = k; break; }
    if (ib < FRACS) {
        const volatile int32_t *const q = DSP_RESULTS(products[surface]);
        for (unsigned j = 0; j < 3; ++j) {
            const unsigned k = (half * 2 + side) * 3 + j;
            const int32_t v = q[k * FRACS + ib];
            p[j] = base[j] + (negative[surface][k] ? -v : v);
#if ENG_DSP_BANDS_CHECK
            if (p[j] != lerp_exact(base[j], to[j], fraction)) ++fzx_band_stats[7];
#endif
        }
    } else {
        for (unsigned j = 0; j < 3; ++j) p[j] = lerp_exact(base[j], to[j], fraction);
        fzx_band_stats[2] += 3;
    }
    fzx_band_stats[4] += 3;
}
#else
void fzx_band_point(unsigned surface, int32_t p[3], const int32_t positions[8][3], unsigned half, unsigned side, int32_t fraction) {
    (void)surface;
    const int32_t *base = positions[2 + 2 * half + side], *to = positions[6 * half + side];
    for (unsigned j = 0; j < 3; ++j) p[j] = lerp_exact(base[j], to[j], fraction);
    fzx_band_stats[4] += 3;
}

void fzx_band_begin(unsigned surface, const int32_t positions[8][3], const fzx_band_fractions_t *f) {
    (void)surface; (void)positions; (void)f; ++fzx_band_stats[0];
}

void fzx_band_corners(unsigned surface, int32_t outer[4][3], const int32_t positions[8][3],
                      unsigned half, unsigned band, const int32_t fractions[4]) {
    (void)surface; (void)band;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned side = sides[i];
        const int32_t *base = positions[2 + 2 * half + side], *to = positions[6 * half + side];
        for (unsigned j = 0; j < 3; ++j) outer[i][j] = lerp_exact(base[j], to[j], fractions[i]);
    }
    fzx_band_stats[4] += 12;
}
#endif
#endif
