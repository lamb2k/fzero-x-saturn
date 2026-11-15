/* Correctly rounded binary32 square root in integer arithmetic, for the SH-2 builds (Yaul has
 * no libm). MIT; see ../../LICENSE.
 * IEEE 754 round to nearest even; sqrt(+-0) = +-0, sqrt(x<0) = NaN, sqrt(+inf) = +inf, a NaN
 * input returns it quieted. ref/sat/test_soft_sqrtf.c checks every one of the 2^32 inputs
 * against SSE sqrtss.
 *
 * Method: the exact integer square root r = floor(sqrt(M)) of a 50-bit M, from a 64-entry
 * table estimate, two Newton steps (a 64/32 divide each: the SH-2 on-chip divider when
 * FZX_SQRT_SH2_DIVU is defined) and a final +-1 correction with exact 64-bit squares. r has
 * 25 bits: 24 for the result and one round bit; M - r^2 != 0 is the sticky bit. */
typedef unsigned int u32_t;
typedef unsigned long long u64_t;

static float from_bits(u32_t u) {
    union {
        u32_t u;
        float f;
    } v;
    v.u = u;
    return v.f;
}

static u32_t to_bits(float f) {
    union {
        u32_t u;
        float f;
    } v;
    v.f = f;
    return v.u;
}

/* Seeds for sqrt(M), M in [2^48, 2^50): entry i (M >> 44 == i, 16 <= i < 64) is
 * round(sqrt((i + 0.5) * 2^44)). Generated; the exhaustive test covers every entry. */
static const u32_t kSqrtSeed[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    17037343, 17546032, 18040384, 18521545, 18990519, 19448188, 19895331, 20332643,
    20760746, 21180197, 21591501, 21995116, 22391456, 22780902, 23163801, 23540473,
    23911212, 24276290, 24635958, 24990450, 25339984, 25684762, 26024972, 26360792,
    26692387, 27019913, 27343517, 27663335, 27979497, 28292127, 28601340, 28907245,
    29209947, 29509544, 29806130, 30099794, 30390620, 30678689, 30964078, 31246861,
    31527108, 31804885, 32080257, 32353285, 32624029, 32892544, 33158885, 33423103,
};

#if defined(FZX_SQRT_SH2_DIVU)
static inline u32_t div64(u64_t n, u32_t d) {
    volatile u32_t* const dvsr = (volatile u32_t*) 0xFFFFFF00u;
    volatile u32_t* const dvdnth = (volatile u32_t*) 0xFFFFFF10u;
    volatile u32_t* const dvdntl = (volatile u32_t*) 0xFFFFFF14u;
    *dvsr = d;
    *dvdnth = (u32_t) (n >> 32);
    *dvdntl = (u32_t) n;
    return *dvdntl;
}
#else
static inline u32_t div64(u64_t n, u32_t d) {
    return (u32_t) (n / d);
}
#endif

float fzx_soft_sqrtf(float x) {
    const u32_t ix = to_bits(x);
    const u32_t biased = (ix >> 23) & 0xFF;
    u32_t man = ix & 0x7FFFFF, q, r;
    u64_t m, sq;
    int e;

    if (biased == 0xFF) {
        if (man != 0) return from_bits(ix | 0x400000);           /* NaN: quiet it */
        return (ix >> 31) ? from_bits(0xFFC00000u) : x;          /* -inf: NaN; +inf */
    }
    if ((ix & 0x7FFFFFFF) == 0) return x;                        /* +-0 */
    if (ix >> 31) return from_bits(0xFFC00000u);                 /* negative: default NaN */
    if (biased == 0) {                                           /* subnormal: normalise */
        e = -126;
        while ((man & 0x800000) == 0) {
            man <<= 1;
            e--;
        }
    } else {
        man |= 0x800000;
        e = (int) biased - 127;
    }
    /* x = man * 2^(e-23), man in [2^23, 2^24). Make e even. */
    if (e & 1) {
        man <<= 1;
        e--;
    }
    /* M = man * 2^25 lies in [2^48, 2^50): floor(sqrt(M)) has exactly 25 bits. */
    m = (u64_t) man << 25;
    r = kSqrtSeed[(u32_t) (m >> 44)];       /* about 8 good bits */
    r = (r + div64(m, r)) >> 1;               /* about 16 */
    r = (r + div64(m, r)) >> 1;               /* about 32: within 1 of the root */
    sq = (u64_t) r * r;
    while (sq > m) {                          /* at most a step or two */
        r--;
        sq = (u64_t) r * r;
    }
    while (sq + 2u * (u64_t) r + 1u <= m) {   /* (r+1)^2 <= m */
        r++;
        sq = (u64_t) r * r;
    }
    q = r >> 1;                               /* [2^23, 2^24) */
    e /= 2;                                   /* result = q * 2^(e - 23) */
    if ((r & 1) && ((sq != m) || (q & 1))) {  /* round bit, then sticky or odd */
        q++;
        if (q == (1u << 24)) {
            q >>= 1;
            e++;
        }
    }
    return from_bits(((u32_t) (e + 127) << 23) + (q & 0x7FFFFF));
}

float sqrtf(float x) {
    return fzx_soft_sqrtf(x);
}
