/* Fog-band corners of the VDP2 floor and clouds (ENG_DSP_BANDS, DSP lane 2026-11-19).
 *
 * The band loops in fzx_floor_rbg.c and fzx_clouds_rbg.c place each band
 * corner on an inner->edge rail:
 *     outer[i][j] = positions[inner][j] + trunc((positions[edge][j] - positions[inner][j]) * f / 65536)
 * in 64-bit arithmetic (about 2.7 us per coordinate on the SH-2, 168 per
 * surface). With ENG_DSP_BANDS:
 *   1  the same values on the SH-2 from 32-bit deltas (one dmuls.l and a
 *      truncation fix per coordinate); deltas that do not fit 32 bits take
 *      the original 64-bit expression;
 *   2  the products on the SCU DSP (SH2ZAM dsp-1 scale16): begin() starts
 *      one job per surface for every delta x fraction pair, after the
 *      positions are known; the first corners() call collects it. A
 *      fraction or delta outside the job takes the arm-1 path.
 * Both arms give exactly the original values (bit-identical corners).
 * 0 (default) leaves both files' original loops compiled unchanged. */
#ifndef FZX_BAND_CORNERS_H
#define FZX_BAND_CORNERS_H
#include <stdint.h>
#ifndef ENG_DSP_BANDS
#define ENG_DSP_BANDS 0
#endif
#if ENG_DSP_BANDS
/* Fractions per band 1..7 (index band - 1), as the caller's loop computes them;
 * skip[b] marks a band the loop will not draw. */
typedef struct { int32_t low[7], high[7]; uint8_t skip[7]; } fzx_band_fractions_t;
/* The floor's fixed thresholds; the clouds' thresholds for inner alpha inner. */
void fzx_band_fractions_floor(fzx_band_fractions_t *f);
void fzx_band_fractions_clouds(fzx_band_fractions_t *f, int32_t inner);
/* surface: 0 floor, 1 clouds. Rails as both loops: inner vertex 2 + 2 * half + side, edge vertex 6 * half + side. */
void fzx_band_begin(unsigned surface, const int32_t positions[8][3], const fzx_band_fractions_t *f);
void fzx_band_corners(unsigned surface, int32_t outer[4][3], const int32_t positions[8][3],
                      unsigned half, unsigned band, const int32_t fractions[4]);
/* [0] surfaces begun, [1] DSP jobs, [2] fallback coordinates (delta or fraction outside the job),
 * [3] DSP wait polls max, [4] corner coordinates, [5] program loads, [6] job refusals,
 * [7] mismatches against the SH-2 form (ENG_DSP_BANDS_CHECK=1 diagnostic builds). */
extern volatile uint32_t fzx_band_stats[8];
#endif
#endif
