/* Check src/sh2float/fzx_float.c bit for bit against SSE scalar binary32 (the reference's
 * float). MIT; see ../LICENSE.
 *   cc -O2 -msse2 -mfpmath=sse -fno-fast-math -ffp-contract=off -I src/sh2float \
 *      tests/test_fzx_float.c src/sh2float/fzx_float.c -o test_fzx_float && ./test_fzx_float [scale [stride]]
 * Binary ops: every exponent pair (256 x 256, both signs) with random significands biased to
 * the edges, plus special values; scale (default 40) is significand pairs per exponent pair.
 * Conversions: every stride-th 32-bit input (default 1: all 2^32). Exit 0 when nothing differs.
 * `make test` runs a quick sweep; CI also runs the exhaustive one. */
#include <emmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fzx_float.h"

static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static uint64_t s = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t) (s >> 16); }

/* Significands that hit rounding edges: all ones, all zeros, single bits, random. */
static uint32_t mant(void) {
    switch (rnd() % 8) {
    case 0: return 0;
    case 1: return 0x7FFFFF;
    case 2: return 1u << (rnd() % 23);
    case 3: return 0x7FFFFF ^ (1u << (rnd() % 23));
    case 4: return rnd() & 0x7F;           /* low bits only */
    case 5: return 0x400000 | (rnd() & 1); /* near a tie */
    default: return rnd() & 0x7FFFFF;
    }
}

static float sse_add(float a, float b) { return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static float sse_sub(float a, float b) { return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static float sse_mul(float a, float b) { return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static float sse_div(float a, float b) { return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static int sse_cmp(float a, float b) {
    if (_mm_ucomineq_ss(_mm_set_ss(a), _mm_set_ss(a)) || _mm_ucomineq_ss(_mm_set_ss(b), _mm_set_ss(b))) {
        if (a != a || b != b) return 2;
    }
    return (a > b) - (a < b);
}

static uint64_t bad;
static void check(const char* op, uint32_t a, uint32_t b, uint32_t want, uint32_t got) {
    if (want != got) {
        if (bad < 20) printf("%s %08x %08x: sse %08x fzx %08x\n", op, a, b, want, got);
        bad++;
    }
}

static void pair(uint32_t a, uint32_t b) {
    float x = bf(a), y = bf(b);
    check("add", a, b, fb(sse_add(x, y)), fb(fzx_fadd(x, y)));
    check("sub", a, b, fb(sse_sub(x, y)), fb(fzx_fsub(x, y)));
    check("mul", a, b, fb(sse_mul(x, y)), fb(fzx_fmul(x, y)));
    check("div", a, b, fb(sse_div(x, y)), fb(fzx_fdiv(x, y)));
    check("cmp", a, b, (uint32_t) sse_cmp(x, y), (uint32_t) fzx_fcmp(x, y));
}

int main(int argc, char** argv) {
    int scale = argc > 1 ? atoi(argv[1]) : 40;
    uint32_t stride = argc > 2 ? (uint32_t) strtoul(argv[2], NULL, 0) : 1;
    static const uint32_t special[] = { 0, 0x80000000u, 1, 0x80000001u, 0x7FFFFF, 0x800000, 0x7F7FFFFF, 0x7F800000u,
                                        0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0x7FBFFFFFu,
                                        0x3F800000u, 0xBF800000u, 0x3F7FFFFFu, 0x4B000000u, 0x00FFFFFFu };
    const int ns = (int) (sizeof(special) / sizeof(special[0]));
    uint64_t pairs = 0;
    uint32_t i;
    int ea, eb, k, j;

    for (k = 0; k < ns; k++)
        for (j = 0; j < ns; j++) { pair(special[k], special[j]); pairs++; }
    for (ea = 0; ea < 256; ea++) {
        for (eb = 0; eb < 256; eb++) {
            for (k = 0; k < scale; k++) {
                uint32_t a = ((rnd() & 1u) << 31) | ((uint32_t) ea << 23) | mant();
                uint32_t b = ((rnd() & 1u) << 31) | ((uint32_t) eb << 23) | mant();
                pair(a, b);
                pairs++;
            }
        }
    }
    printf("binary ops: %llu operand pairs x 5 ops, %llu differ\n", (unsigned long long) pairs,
           (unsigned long long) bad);
    {
        uint64_t before = bad;
        i = 0;
        do {
            float f = bf(i);
            int32_t want = _mm_cvttss_si32(_mm_set_ss(f));
            check("ftoi", i, 0, (uint32_t) want, (uint32_t) fzx_ftoi(f));
            check("itof", i, 0, fb(_mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), (int32_t) i))), fb(fzx_itof((int32_t) i)));
            check("utof", i, 0, fb((float) i), fb(fzx_utof(i)));
            i += stride;
        } while (i >= stride);
        printf("conversions: every %u-th of 2^32 inputs x 3, %llu differ\n", stride,
               (unsigned long long) (bad - before));
    }
    printf("fzx_float: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
