/* Fast IEEE 754 binary32 arithmetic for the SH-2 (no FPU), bit-exact with SSE scalar float.
 * MIT; see ../../LICENSE.
 *
 * Round to nearest even, gradual underflow (subnormal inputs and outputs), signed zeros and
 * infinities as IEEE 754. NaN results follow SSE: an invalid operation gives the default NaN
 * 0xFFC00000; a NaN operand is returned quieted (the first operand's if both are NaN).
 * float -> int truncates and gives 0x80000000 when out of range or NaN, as cvttss2si does.
 *
 * Built two ways from this one file:
 *  - host tests (tests/test_fzx_float.c): functions named fzx_f* and checked against SSE;
 *  - the SH-2 (FZX_FLOAT_LIBGCC defined): the same functions also define libgcc's soft-float
 *    entry points (__addsf3 and friends), replacing libgcc's generic fp-bit code at link time.
 * On the SH-2 the 64/32 divide uses the on-chip division unit (FZX_FLOAT_SH2_DIVU). */
#include "fzx_float.h"

typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;

#define SIGN 0x80000000u
#define ABS 0x7FFFFFFFu
#define INF 0x7F800000u
#define DEFAULT_NAN 0xFFC00000u
#define QUIET 0x00400000u

static inline u32 bits_of(float f) {
    union { float f; u32 u; } v;
    v.f = f;
    return v.u;
}

static inline float float_of(u32 u) {
    union { float f; u32 u; } v;
    v.u = u;
    return v.f;
}

static inline int is_nan(u32 u) {
    return (u & ABS) > INF;
}

/* Number of leading zeros of a non-zero 32-bit value, with constant shifts only (the SH-2
 * has shifts by 1, 2, 8 and 16 but no variable shift). */
static inline int clz32(u32 x) {
    int n = 0;
    if ((x & 0xFFFF0000u) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000u) == 0) { n += 8; x <<= 8; }
    if ((x & 0xF0000000u) == 0) { n += 4; x <<= 4; }
    if ((x & 0xC0000000u) == 0) { n += 2; x <<= 2; }
    if ((x & 0x80000000u) == 0) { n += 1; }
    return n;
}

/* 2^k for k in 0..31. The SH-2 has no variable shift (gcc calls a helper for one), but it has
 * a 32x32->64 multiply (dmulu.l) and a 32x32->32 one (mul.l): x << n is x * 2^n, and
 * x * 2^(32-n) puts x >> n in the high word and the bits shifted out in the low word. */
static const u32 kPow2[32] = {
    1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6, 1u << 7,
    1u << 8, 1u << 9, 1u << 10, 1u << 11, 1u << 12, 1u << 13, 1u << 14, 1u << 15,
    1u << 16, 1u << 17, 1u << 18, 1u << 19, 1u << 20, 1u << 21, 1u << 22, 1u << 23,
    1u << 24, 1u << 25, 1u << 26, 1u << 27, 1u << 28, 1u << 29, 1u << 30, 1u << 31,
};

/* x << n for 0 <= n < 32. */
static inline u32 shl(u32 x, int n) {
    return x * kPow2[n];
}

/* x >> n for 0 < n < 32. */
static inline u32 shr(u32 x, int n) {
    return (u32) (((u64) x * kPow2[32 - n]) >> 32);
}

/* x >> n for 0 < n < 32, ORing a 1 into bit 0 if any shifted-out bit was set (sticky). */
static inline u32 shift_right_sticky(u32 x, int n) {
    const u64 p = (u64) x * kPow2[32 - n];
    return (u32) (p >> 32) | ((u32) p != 0);
}

/* Round and pack. m holds the significand with its leading 1 at bit 30 (or lower for a
 * subnormal with e == 1) and 7 round bits below bit 7; e is the biased exponent >= 1. */
static inline u32 round_pack(u32 sign, s32 e, u32 m) {
    u32 r, out;
    if (e >= 0xFF) return sign | INF;
    r = m & 0x7F;
    m >>= 7;
    if (r > 0x40 || (r == 0x40 && (m & 1))) m++;
    /* (e-1) << 23 plus a significand that still carries its leading 1 lands the exponent right;
     * a carry out of rounding moves into the exponent, up to and including infinity. */
    out = ((u32) (e - 1) << 23) + m;
    return sign | out;
}

/* Like round_pack, for a result whose exponent may be below the normal range: e < 1 shifts the
 * significand right first (gradual underflow), with sticky. */
static inline u32 round_pack_any(u32 sign, s32 e, u32 m) {
    if (e < 1) {
        int sh = 1 - e;
        m = sh < 31 ? shift_right_sticky(m, sh) : (m != 0);
        e = 1;
    }
    return round_pack(sign, e, m);
}

static u32 nan_pick(u32 a, u32 b) {
    if (is_nan(a)) return a | QUIET;
    return b | QUIET;
}

static u32 add_mag(u32 a, u32 b, u32 sign);
static u32 sub_mag(u32 a, u32 b, u32 sign);

/* |a| >= |b| (as magnitudes), neither NaN nor infinity handled here. */
static u32 add_mag(u32 a, u32 b, u32 sign) {
    s32 ea = (s32) (a >> 23), eb = (s32) (b >> 23);
    u32 ma = a & 0x7FFFFF, mb = b & 0x7FFFFF;
    int d;
    if (ea == 0) ea = 1; else ma |= 0x800000;
    if (eb == 0) eb = 1; else mb |= 0x800000;
    ma <<= 6; /* leading 1 at bit 29: room for the carry */
    mb <<= 6;
    d = ea - eb;
    if (d) mb = d < 31 ? shift_right_sticky(mb, d) : (mb != 0);
    ma += mb;
    /* Bit 29 stands for 2^0 at exponent ea. A carry to bit 30 is 2^0 at exponent ea + 1. */
    if (ma & 0x40000000u) return round_pack(sign, ea + 1, ma);
    /* No carry: bring bit 29 up to bit 30. A subnormal sum (ea == 1, no leading 1) packs as a
     * subnormal, or becomes the smallest normal if it reached 2^23. */
    return round_pack(sign, ea, ma << 1);
}

static u32 sub_mag(u32 a, u32 b, u32 sign) {
    s32 ea = (s32) (a >> 23), eb = (s32) (b >> 23);
    u32 ma = a & 0x7FFFFF, mb = b & 0x7FFFFF;
    int d, z;
    if (ea == 0) ea = 1; else ma |= 0x800000;
    if (eb == 0) eb = 1; else mb |= 0x800000;
    ma <<= 7; /* leading 1 at bit 30 */
    mb <<= 7;
    d = ea - eb;
    if (d) mb = d < 31 ? shift_right_sticky(mb, d) : (mb != 0);
    ma -= mb;
    if (ma == 0) return 0; /* exact zero is +0 in round to nearest */
    z = clz32(ma) - 1;     /* shift to bring the leading 1 to bit 30 */
    if (z > ea - 1) z = ea - 1; /* stop at the subnormal boundary */
    ma = shl(ma, z);
    return round_pack(sign, ea - z, ma);
}

float fzx_fadd(float fa, float fb) {
    u32 a = bits_of(fa), b = bits_of(fb), t;
    u32 aa = a & ABS, ab = b & ABS;
    if (aa >= INF || ab >= INF) {
        if (is_nan(a) || is_nan(b)) return float_of(nan_pick(a, b));
        if (aa == INF && ab == INF && ((a ^ b) & SIGN)) return float_of(DEFAULT_NAN);
        return aa == INF ? fa : fb;
    }
    if (aa < ab) { t = a; a = b; b = t; t = aa; aa = ab; ab = t; }
    if (ab == 0) {
        if (aa == 0) return float_of(a & b); /* +0 unless both are -0 */
        return float_of(a);
    }
    if ((a ^ b) & SIGN) return float_of(sub_mag(aa, ab, a & SIGN));
    return float_of(add_mag(aa, ab, a & SIGN));
}

float fzx_fsub(float fa, float fb) {
    u32 b = bits_of(fb);
    if (is_nan(b)) { /* keep the NaN's own sign, as SSE subss does */
        u32 a = bits_of(fa);
        return float_of(is_nan(a) ? a | QUIET : b | QUIET);
    }
    return fzx_fadd(fa, float_of(b ^ SIGN));
}

/* Normalise a non-zero subnormal significand: leading 1 to bit 23, exponent adjusted. */
static inline void normalise_sub(u32* m, s32* e) {
    int z = clz32(*m) - 8;
    *m = shl(*m, z);
    *e = 1 - z;
}

float fzx_fmul(float fa, float fb) {
    u32 a = bits_of(fa), b = bits_of(fb);
    u32 sign = (a ^ b) & SIGN, aa = a & ABS, ab = b & ABS, ma, mb, hi, lo;
    s32 ea, eb, e;
    u64 p;
    if (aa >= INF || ab >= INF) {
        if (is_nan(a) || is_nan(b)) return float_of(nan_pick(a, b));
        if (aa == 0 || ab == 0) return float_of(DEFAULT_NAN); /* inf * 0 */
        return float_of(sign | INF);
    }
    if (aa == 0 || ab == 0) return float_of(sign);
    ea = (s32) (aa >> 23); ma = aa & 0x7FFFFF;
    eb = (s32) (ab >> 23); mb = ab & 0x7FFFFF;
    if (ea == 0) normalise_sub(&ma, &ea); else ma |= 0x800000;
    if (eb == 0) normalise_sub(&mb, &eb); else mb |= 0x800000;
    e = ea + eb - 127;
    p = (u64) ma * mb;            /* [2^46, 2^48) */
    hi = (u32) (p >> 32);
    lo = (u32) p;
    /* Bring the leading 1 to bit 30 of a 32-bit word with sticky from the rest. */
    if (hi & 0x8000) {            /* product >= 2^47 */
        u32 m = (hi << 15) | (lo >> 17) | ((lo & 0x1FFFF) != 0);
        return float_of(round_pack_any(sign, e + 1, m));
    } else {
        u32 m = (hi << 16) | (lo >> 16) | ((lo & 0xFFFF) != 0);
        return float_of(round_pack_any(sign, e, m));
    }
}

#if defined(FZX_FLOAT_SH2_DIVU)
/* SH-2 on-chip division unit: signed 64/32 -> 32 quotient and remainder (39 cycles). */
static inline u32 divu64(u32 n_hi, u32 n_lo, u32 d, u32* rem) {
    volatile u32* const dvsr = (volatile u32*) 0xFFFFFF00u;
    volatile u32* const dvdnth = (volatile u32*) 0xFFFFFF10u;
    volatile u32* const dvdntl = (volatile u32*) 0xFFFFFF14u;
    *dvsr = d;
    *dvdnth = n_hi;
    *dvdntl = n_lo; /* starts the division */
    {
        u32 q = *dvdntl;
        *rem = *dvdnth;
        return q;
    }
}
#endif

float fzx_fdiv(float fa, float fb) {
    u32 a = bits_of(fa), b = bits_of(fb);
    u32 sign = (a ^ b) & SIGN, aa = a & ABS, ab = b & ABS, ma, mb, q, rem, m;
    s32 ea, eb, e;
    if (aa >= INF || ab >= INF) {
        if (is_nan(a) || is_nan(b)) return float_of(nan_pick(a, b));
        if (aa == INF && ab == INF) return float_of(DEFAULT_NAN);
        return float_of(aa == INF ? sign | INF : sign);
    }
    if (ab == 0) return float_of(aa == 0 ? DEFAULT_NAN : sign | INF);
    if (aa == 0) return float_of(sign);
    ea = (s32) (aa >> 23); ma = aa & 0x7FFFFF;
    eb = (s32) (ab >> 23); mb = ab & 0x7FFFFF;
    if (ea == 0) normalise_sub(&ma, &ea); else ma |= 0x800000;
    if (eb == 0) normalise_sub(&mb, &eb); else mb |= 0x800000;
    e = ea - eb + 127;
    if (ma < mb) { ma <<= 1; e--; }  /* quotient in [1, 2) */
    /* q = (ma << 30) / mb has its leading 1 at bit 30 (ma/mb in [1,2)). */
#if defined(FZX_FLOAT_SH2_DIVU)
    q = divu64(ma >> 2, ma << 30, mb, &rem);
#else
    {
        u64 n = (u64) ma << 30;
        q = (u32) (n / mb);
        rem = (u32) (n % mb);
    }
#endif
    m = q | (rem != 0);
    return float_of(round_pack_any(sign, e, m));
}

/* Comparison key: a signed integer that orders like the float (both zeros equal). */
static inline s32 order_key(u32 u) {
    return (u & SIGN) ? -(s32) (u & ABS) : (s32) u;
}

/* -1, 0, 1 for a < b, a == b, a > b; 2 if unordered. */
static inline int cmp3(float fa, float fb) {
    u32 a = bits_of(fa), b = bits_of(fb);
    s32 ka, kb;
    if (is_nan(a) || is_nan(b)) return 2;
    ka = order_key(a);
    kb = order_key(b);
    return (ka > kb) - (ka < kb);
}

int fzx_fcmp(float fa, float fb) {
    return cmp3(fa, fb);
}

s32 fzx_ftoi(float fa) {
    u32 a = bits_of(fa), m;
    s32 e = (s32) ((a >> 23) & 0xFF) - 127, r;
    if (e < 0) return 0;
    if (e >= 31) { /* only -2^31 fits; everything else is out of range */
        return (s32) 0x80000000u;
    }
    m = (a & 0x7FFFFF) | 0x800000;
    if (e >= 23) r = (s32) shl(m, e - 23);
    else r = (s32) shr(m, 23 - e);
    return (a & SIGN) ? -r : r;
}

static u32 from_u32(u32 v, u32 sign) {
    int z;
    if (v == 0) return sign;
    z = clz32(v);
    if (z >= 8) { /* exact: fits in 24 bits */
        return sign | (((u32) (127 + 31 - z - 1) << 23) + shl(v, z - 8));
    }
    /* leading 1 to bit 30 with sticky, then round */
    {
        u32 m = z == 0 ? (v >> 1) | (v & 1) : shl(v, z - 1);
        return round_pack(sign, 127 + 31 - z, m);
    }
}

float fzx_itof(s32 v) {
    if (v < 0) return float_of(from_u32(0u - (u32) v, SIGN));
    return float_of(from_u32((u32) v, 0));
}

float fzx_utof(u32 v) {
    return float_of(from_u32(v, 0));
}

#if defined(FZX_FLOAT_LIBGCC)
/* libgcc soft-float entry points (SH-2 build): the linker takes these before libgcc.a. The
 * arithmetic ones are aliases, so a call costs one jump, not two. */
float __addsf3(float a, float b) __attribute__((alias("fzx_fadd")));
float __subsf3(float a, float b) __attribute__((alias("fzx_fsub")));
float __mulsf3(float a, float b) __attribute__((alias("fzx_fmul")));
float __divsf3(float a, float b) __attribute__((alias("fzx_fdiv")));
s32 __fixsfsi(float a) __attribute__((alias("fzx_ftoi")));
float __floatsisf(s32 v) __attribute__((alias("fzx_itof")));
float __floatunsisf(u32 v) __attribute__((alias("fzx_utof")));
float __negsf2(float a) { return float_of(bits_of(a) ^ SIGN); }
int __eqsf2(float a, float b) { int c = cmp3(a, b); return c == 2 ? 1 : c; }
int __nesf2(float a, float b) { int c = cmp3(a, b); return c == 2 ? 1 : c; }
int __ltsf2(float a, float b) { int c = cmp3(a, b); return c == 2 ? 1 : c; }
int __lesf2(float a, float b) { int c = cmp3(a, b); return c == 2 ? 1 : c; }
int __gtsf2(float a, float b) { int c = cmp3(a, b); return c == 2 ? -1 : c; }
int __gesf2(float a, float b) { int c = cmp3(a, b); return c == 2 ? -1 : c; }
int __unordsf2(float a, float b) { return cmp3(a, b) == 2; }
#endif
