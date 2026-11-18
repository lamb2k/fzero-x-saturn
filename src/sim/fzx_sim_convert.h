/* binary32 <-> fixed-point conversion for the simulation lane's float/fixed
 * boundary (import of the source float state, export of the float mirror).
 * MIT; see ../../LICENSE.
 *
 * LANE-LOCAL, PENDING AN SH2ZAM CONVERSION TAG: no SH2ZAM tag (fixed-unit-1,
 * trig12-1, view-camera-1 at 198848f) converts binary32 to fixed point or
 * back; this lane has asked for one. Integer-only, on the IEEE bit pattern,
 * so host and SH-2 agree bit for bit and no soft-float routine runs.
 *   fzx_f32_to_fix: round(f * 2^frac), ties away from zero, saturated to int32;
 *                   NaN gives 0, infinities saturate; frac <= 40.
 *   fzx_fix_to_f32: the binary32 nearest to v / 2^frac (ties to even), as bits;
 *                   exact whenever |v| < 2^24; frac <= 60. */
#ifndef FZX_SIM_CONVERT_H
#define FZX_SIM_CONVERT_H
#include "fzx_sim_types.h"
#ifdef __cplusplus
extern "C" {
#endif
int32_t fzx_f32_to_fix(uint32_t bits,unsigned frac);
uint32_t fzx_fix_to_f32(int32_t v,unsigned frac);
#ifdef __cplusplus
}
#endif
#endif
