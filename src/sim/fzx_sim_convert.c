/* binary32 <-> fixed point; see fzx_sim_convert.h. MIT; see ../../LICENSE. */
#include "fzx_sim_convert.h"

/* 2^k, k = 0..31: variable right shifts as a 32x32->64 product (the SH-2 has
 * no barrel shifter): x >> n == (x * 2^(32-n)) >> 32 for n in 1..31. */
static const uint32_t pow2[32]={
    UINT32_C(1)<<0,UINT32_C(1)<<1,UINT32_C(1)<<2,UINT32_C(1)<<3,UINT32_C(1)<<4,UINT32_C(1)<<5,UINT32_C(1)<<6,UINT32_C(1)<<7,
    UINT32_C(1)<<8,UINT32_C(1)<<9,UINT32_C(1)<<10,UINT32_C(1)<<11,UINT32_C(1)<<12,UINT32_C(1)<<13,UINT32_C(1)<<14,UINT32_C(1)<<15,
    UINT32_C(1)<<16,UINT32_C(1)<<17,UINT32_C(1)<<18,UINT32_C(1)<<19,UINT32_C(1)<<20,UINT32_C(1)<<21,UINT32_C(1)<<22,UINT32_C(1)<<23,
    UINT32_C(1)<<24,UINT32_C(1)<<25,UINT32_C(1)<<26,UINT32_C(1)<<27,UINT32_C(1)<<28,UINT32_C(1)<<29,UINT32_C(1)<<30,UINT32_C(1)<<31};

int32_t fzx_f32_to_fix(uint32_t bits,unsigned frac) {
    const uint32_t e=(bits>>23)&0xffu;
    const bool negative=(bits>>31)!=0;
    uint32_t m=bits&UINT32_C(0x7fffff),magnitude;
    if(e==0xffu) return m ? 0 : (negative ? INT32_MIN : INT32_MAX);
    if(e) m|=UINT32_C(0x800000);
    if(!m) return 0;
    /* value = m * 2^(e - 150) (subnormals: e treated as 1), scaled by 2^frac */
    const int shift=(int)(e ? e : 1u)-150+(int)frac;
    if(shift>=8) return negative ? INT32_MIN : INT32_MAX;     /* m >= 2^23 */
    if(shift>=0) {
        magnitude=m<<shift;
        if(magnitude>UINT32_C(0x7fffffff)) return negative ? INT32_MIN : INT32_MAX;
    } else if(shift<=-25) {
        return 0;                                            /* below 1/2 */
    } else {
        /* round half away from zero: (m + 2^(n-1)) >> n, n = -shift in 1..24.
         * Variable shifts as a 32x32->64 product (no barrel shifter on SH-2). */
        const unsigned n=(unsigned)-shift;
        magnitude=(uint32_t)(((uint64_t)(m+(UINT32_C(1)<<(n-1u)))*pow2[32u-n])>>32);
    }
    return negative ? -(int32_t)magnitude : (int32_t)magnitude;
}

uint32_t fzx_fix_to_f32(int32_t v,unsigned frac) {
    if(!v) return 0;
    const uint32_t sign=v<0 ? UINT32_C(0x80000000) : 0u;
    uint32_t a=v<0 ? 0u-(uint32_t)v : (uint32_t)v;
    /* normalise a to [2^31, 2^32): leading-bit position p (0..31) */
    unsigned p=31;
    if(a<UINT32_C(0x10000)) { a<<=16; p-=16; }
    if(a<UINT32_C(0x1000000)) { a<<=8; p-=8; }
    if(a<UINT32_C(0x10000000)) { a<<=4; p-=4; }
    if(a<UINT32_C(0x40000000)) { a<<=2; p-=2; }
    if(a<UINT32_C(0x80000000)) { a<<=1; p-=1; }
    /* 24-bit mantissa from the top of a, round to nearest even on the low 8 bits */
    uint32_t m=a>>8;
    const uint32_t rest=a&0xffu;
    if(rest>0x80u || (rest==0x80u && (m&1u))) ++m;
    int exponent=(int)p-(int)frac+127;
    if(m==UINT32_C(0x1000000)) { m>>=1; ++exponent; }
    if(exponent>=255) return sign|UINT32_C(0x7f800000);
    if(exponent<=0) return sign;                             /* not reached for frac <= 60 and |v| >= 1 */
    return sign|(uint32_t)exponent<<23|(m&UINT32_C(0x7fffff));
}
