/* STAND-IN PENDING SH2ZAM TAGS fixed-unit-1 (K1-K3) and trig12-1 (K4-K5).
 *
 * The port pins SH2ZAM 7de29ac, which has neither tag. Until the integration
 * session moves the pin, this file supplies the subset the fixed-point camera
 * uses, with the contract names and definitions of the in-progress SH2ZAM
 * lanes (unit30_experimental.h / trig12_experimental.h drafts of 2026-11-17,
 * MIT, SH2ZAM Contributors). Delete this file and include the tag headers when
 * the pin moves; the names collide on purpose so a stale stand-in cannot
 * coexist with the tag. No game code; no floating point; host and SH-2 give
 * identical bits (all products are exact 32x32->64; DMULS.L/DMULU.L on SH-2).
 *
 * Formats: Q2.30 ("u30", 1.0 = 2^30), Q20.12 ("q12", 1.0 = 4096), Q16.16. */
#ifndef FZX_SIM_STANDIN_H
#define FZX_SIM_STANDIN_H

#include "fzx_sim_types.h"

#if defined(SH2ZAM_UNIT30_EXPERIMENTAL_H) || defined(SH2ZAM_TRIG12_EXPERIMENTAL_H)
#error "SH2ZAM fixed-unit-1/trig12-1 present: remove the sim stand-in"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- K1 (fixed-unit-1) ---------------------------------------------------- */
typedef int32_t sh2z_u30_t;
#define SH2Z_U30_ONE  INT32_C(0x40000000)
#define SH2Z_Q12_ONE  INT32_C(4096)
typedef struct { int32_t x[3],y[3],z[3]; } sh2z_basis30_t;

static inline int32_t sh2zi_u30_s32(uint32_t u) {
    return u<=UINT32_C(0x7fffffff) ? (int32_t)u : (int32_t)(u-UINT32_C(0x80000000))-INT32_C(0x7fffffff)-1;
}
/* floor(q / 2^30) of (exact + 2^29), saturated. */
static inline int32_t sh2zi_u30_shift30(uint64_t q) {
    const uint32_t hi=(uint32_t)(q>>32);
    if(hi+UINT32_C(0x20000000)>=UINT32_C(0x40000000)) return (hi>>31) ? INT32_MIN : INT32_MAX;
    return sh2zi_u30_s32((uint32_t)(q>>30));
}
/** floor((a*b + 2^29) / 2^30), saturated: Q2.30 product, |error| <= 0.5 LSB. */
static inline int32_t sh2z_u30_mul(int32_t a,int32_t b) {
    return sh2zi_u30_shift30((uint64_t)((int64_t)a*b)+UINT64_C(0x20000000));
}
/** Any-format value times a Q2.30 factor, same arithmetic (named for Q20.12). */
static inline int32_t sh2z_q12_mul_u30(int32_t q12,int32_t u) {
    return sh2zi_u30_shift30((uint64_t)((int64_t)q12*u)+UINT64_C(0x20000000));
}
int32_t sh2zi_u30_dot3_saturate(const int32_t a[3],const int32_t b[3]);
/** Q2.30 dot product rounded once: floor((sum a_k*b_k + 2^29) / 2^30), saturated. */
static inline int32_t sh2z_u30_dot3(const int32_t a[3],const int32_t b[3]) {
    const uint64_t q=(uint64_t)((int64_t)a[0]*b[0])+(uint64_t)((int64_t)a[1]*b[1])
        +(uint64_t)((int64_t)a[2]*b[2])+UINT64_C(0x20000000);
    const uint32_t hi=(uint32_t)(q>>32);
    if(hi+UINT32_C(0x20000000)>=UINT32_C(0x40000000)) return sh2zi_u30_dot3_saturate(a,b);
    return sh2zi_u30_s32((uint32_t)(q>>30));
}
/** Q2.30 cross product, each component rounded once. out may alias a or b. */
static inline void sh2z_u30_cross(int32_t out[3],const int32_t a[3],const int32_t b[3]) {
    const int32_t a0=a[0],a1=a[1],a2=a[2],b0=b[0],b1=b[1],b2=b[2];
    const uint64_t k=UINT64_C(0x20000000);
    const int32_t c0=sh2zi_u30_shift30((uint64_t)((int64_t)a1*b2)-(uint64_t)((int64_t)a2*b1)+k);
    const int32_t c1=sh2zi_u30_shift30((uint64_t)((int64_t)a2*b0)-(uint64_t)((int64_t)a0*b2)+k);
    const int32_t c2=sh2zi_u30_shift30((uint64_t)((int64_t)a0*b1)-(uint64_t)((int64_t)a1*b0)+k);
    out[0]=c0;out[1]=c1;out[2]=c2;
}

/* ---- K2/K3 (fixed-unit-1) -------------------------------------------------- */
/** Unit vector of any int32 vector in Q2.30 (|error| <= 2 LSB per component);
 *  false (out untouched) for the zero vector. out may alias v. */
bool sh2z_vec3_normalize_u30(int32_t out[3],const int32_t v[3]);
/** floor(sqrt(s) + 1/2), exact; the Q20.12 length of a sum of Q20.12 squares. */
uint32_t sh2z_isqrt64_q12(uint64_t s);
/** Orthonormalise around x (Math_OrthonormalizeAroundForward order); x may be
 *  any int32 vector, y Q2.30. Returns -1 (as the source) for x == 0 or z == 0. */
int sh2z_basis30_orthonormalize_forward(sh2z_basis30_t *basis);

/* ---- K4 (trig12-1) --------------------------------------------------------- */
extern const int32_t sh2z_sin12_quarter_u30[1025];
static inline int32_t sh2z_sin12_u30(uint32_t angle12) {
    const uint32_t o=angle12&1023u;
    const int32_t v=sh2z_sin12_quarter_u30[(angle12&1024u)?1024u-o:o];
    return (angle12&2048u)?-v:v;
}
static inline int32_t sh2z_cos12_u30(uint32_t angle12) { return sh2z_sin12_u30(angle12+1024u); }
static inline void sh2z_sincos12_u30(uint32_t angle12,int32_t *s,int32_t *c) {
    const uint32_t o=angle12&1023u;
    int32_t sv=sh2z_sin12_quarter_u30[o],cv=sh2z_sin12_quarter_u30[1024u-o];
    if(angle12&1024u) {const int32_t t=sv;sv=cv;cv=-t;}
    if(angle12&2048u) {sv=-sv;cv=-cv;}
    *s=sv;*c=cv;
}

/* ---- K5 (trig12-1): num * 2^f / den, truncated toward zero, saturated ------ */
/* Stand-in through the pinned sh2z_divu_div64 (same rule, blocking). */
int32_t sh2z_divu_q(int32_t num,int32_t den,unsigned frac_bits);

/* ---- binary32 <-> fixed conversion (no SH2ZAM kernel yet; requested) -------
 * Integer-only, on the IEEE bit pattern, so host and SH-2 agree bit for bit
 * and no soft-float routine runs. STAND-IN PENDING an SH2ZAM conversion tag.
 * fzx_f32_to_fix: round(f * 2^frac), ties away from zero, saturated to int32;
 *                 NaN gives 0, infinities saturate; frac <= 40.
 * fzx_fix_to_f32: the binary32 nearest to v / 2^frac (ties to even), as bits;
 *                 exact whenever |v| < 2^24; frac <= 60. */
int32_t fzx_f32_to_fix(uint32_t bits,unsigned frac);
uint32_t fzx_fix_to_f32(int32_t v,unsigned frac);

#ifdef __cplusplus
}
#endif
#endif
