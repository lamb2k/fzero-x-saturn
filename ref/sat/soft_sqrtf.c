/* Correctly rounded binary32 square root in integer arithmetic, for the reference-on-SH-2
 * probe (Yaul has no libm). MIT; see ../../LICENSE.
 * IEEE 754 round to nearest even; sqrt(+-0) = +-0, sqrt(x<0) = NaN, sqrt(+inf) = +inf, a NaN
 * input returns it quieted. ref/sat/test_soft_sqrtf.c checks every one of the 2^32 inputs
 * against SSE sqrtss. */
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

float fzx_soft_sqrtf(float x) {
    const u32_t ix = to_bits(x);
    const u32_t biased = (ix >> 23) & 0xFF;
    u32_t man = ix & 0x7FFFFF, q, round, sticky;
    u64_t m, root = 0, rem = 0;
    int e, i;

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
    /* M = man * 2^25 lies in [2^48, 2^50), so floor(sqrt(M)) has exactly 25 bits, and
     * sqrt(x) = sqrt(M) * 2^((e-48)/2). The top 24 bits are the result, bit 0 rounds. */
    m = (u64_t) man << 25;
    for (i = 48; i >= 0; i -= 2) {
        u64_t trial;
        rem = (rem << 2) | ((m >> i) & 3);
        trial = (root << 2) | 1;
        root <<= 1;
        if (rem >= trial) {
            rem -= trial;
            root |= 1;
        }
    }
    q = (u32_t) (root >> 1);      /* [2^23, 2^24) */
    round = (u32_t) (root & 1);
    sticky = rem != 0;
    e /= 2;                        /* result = q * 2^(e - 23) */
    if (round && (sticky || (q & 1))) {
        q++;
        if (q == (1u << 24)) {
            q >>= 1;
            e++;
        }
    }
    return from_bits(((u32_t) (e + 127) << 23) | (q & 0x7FFFFF));
}

float sqrtf(float x) {
    return fzx_soft_sqrtf(x);
}
