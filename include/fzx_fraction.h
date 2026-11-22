/* floor(a * 2^32 / d) for the clip fractions (q = bit-serial restoring division of a by d over 32 bits, the original
 * definition kept as fzx_fraction_loop), on the SH-2 64/32 divider. Exact: every result equals the loop's.
 *  - d < 2^31 (and a < d): two bits of quotient and the remainder's bit from one divide of a * 2^31 by d;
 *  - d < 2^60 (and a < d): the divide of the operands shifted down to 31 bits is within a few units of the answer; the
 *    exact remainder a * 2^32 - q * d, taken modulo 2^64 (its true size is below 2^63), steers the last steps;
 *  - anything else: the loop. */
#ifndef FZX_FRACTION_H
#define FZX_FRACTION_H
#include <stdint.h>

static inline uint64_t fzx_fraction_loop(uint64_t a,uint64_t d) {
    uint64_t q=0;
    for(unsigned i=0;i<32;++i) {
        q<<=1;
        if(a>=d-a){a-=d-a;q|=1u;}else a+=a;
    }
    return q;
}
/* floor(n * 2^32 / d), 0 <= n < d < 2^31. */
static inline uint32_t fzx_q32_ratio(uint32_t n,uint32_t d) {
#if defined(__sh__)
    *(volatile uint32_t *)0xffffff00u=d;
    *(volatile uint32_t *)0xffffff10u=n>>1;
    *(volatile uint32_t *)0xffffff14u=n<<31;
    const uint32_t q=*(volatile uint32_t *)0xffffff14u;
    const uint32_t r=(n<<31)-q*d; /* the remainder, below d < 2^31: exact in wrapped arithmetic */
    return (q<<1)|((r<<1)>=d?1u:0u);
#else
    return (uint32_t)(((uint64_t)n<<32)/d);
#endif
}
static inline uint64_t fzx_fraction(uint64_t a,uint64_t d) {
    if(!(a<d) || (d>>60))return fzx_fraction_loop(a,d);
    if(!(d>>31))return fzx_q32_ratio((uint32_t)a,(uint32_t)d);
    unsigned bits=0;
    for(uint64_t t=d;t;t>>=8)bits+=8u;
    while(!((d>>(bits-1u))&1u))--bits;
    const unsigned s=bits-31u;
    const uint32_t a1=(uint32_t)(a>>s),d1=(uint32_t)(d>>s);
    uint64_t q=a1<d1?fzx_q32_ratio(a1,d1):0xffffffffu;
    /* q * d modulo 2^64 from 32x32 products. */
    const uint64_t qd=(uint64_t)(uint32_t)d*(uint32_t)q+((uint64_t)((uint32_t)(d>>32)*(uint32_t)q)<<32);
    int64_t r=(int64_t)((a<<32)-qd);
    while(r<0){--q;r+=(int64_t)d;}
    while((uint64_t)r>=d){++q;r-=(int64_t)d;}
    return q;
}
#endif
