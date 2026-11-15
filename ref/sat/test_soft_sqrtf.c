/* Exhaustive check of fzx_soft_sqrtf against SSE sqrtss over all 2^32 inputs.
 * NaN outputs compare as NaN (payload not checked). Build on the host:
 *   gcc -O2 -msse2 -o t ref/sat/test_soft_sqrtf.c ref/sat/soft_sqrtf.c -DTEST_NO_SQRTF_ALIAS */
#include <emmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

float fzx_soft_sqrtf(float x);

int main(void) {
    uint64_t bad = 0;
    uint32_t u = 0;
    do {
        float x, a, b;
        uint32_t ua, ub;
        memcpy(&x, &u, 4);
        a = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
        b = fzx_soft_sqrtf(x);
        memcpy(&ua, &a, 4);
        memcpy(&ub, &b, 4);
        if (ua != ub && !(a != a && b != b)) {
            if (bad < 10) printf("mismatch x=%08x sse=%08x soft=%08x\n", u, ua, ub);
            bad++;
        }
        u++;
    } while (u != 0);
    printf("soft_sqrtf: %llu mismatches over 2^32 inputs\n", (unsigned long long) bad);
    return bad != 0;
}
