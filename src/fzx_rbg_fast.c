#include "fzx_rbg_fast.h"
/* Line window of a convex polygon xy[count] (16.16 screen coordinates); see
 * include/fzx_rbg_fast.h. Equivalent per line to the original:
 *   lo=(left+32767)/65536, hi=(right-32768)/65536, empty if right<32768,
 * because both roundings are monotonic in x: the minimum lo comes from the
 * leftmost crossing and the maximum hi from the rightmost. Lo and hi use
 * floor; they differ from the original's truncation only below zero, where
 * both are then clamped to clip[0] >= 0, and a negative hi means right<32768. */
static int64_t divide(int64_t n,int32_t d) {
#if defined(__sh__)
    if(d<0){d=-d;n=-n;}
    if(d>0 && n>=(int64_t)INT32_MIN*d && n<=(int64_t)INT32_MAX*d) {
        *(volatile uint32_t *)0xffffff00u=(uint32_t)d;
        *(volatile uint32_t *)0xffffff10u=(uint32_t)((uint64_t)n>>32);
        *(volatile uint32_t *)0xffffff14u=(uint32_t)n;
        return (int32_t)*(volatile uint32_t *)0xffffff14u;
    }
#endif
    return n/d;
}
static int32_t first_line(int32_t y) {
    /* Smallest line l with l*65536+32768 >= y (an arithmetic shift floors). */
    const int64_t n=(int64_t)y-32768;
    return (int32_t)-((-n)>>16);
}
void fzx_rbg_window(const fzx_view_t *v,const int32_t xy[10][2],unsigned count,
    uint16_t window[224][2],uint32_t *visible_lines) {
    const int top=v->clip[1]<0?0:v->clip[1],bottom=v->clip[3]>224?224:v->clip[3];
    /* Running minimum lo and maximum hi, as int16 bit patterns, in place. */
    for(int y=0;y<224;++y){window[y][0]=(uint16_t)INT16_MAX;window[y][1]=(uint16_t)INT16_MIN;}
    for(unsigned i=0;i<count;++i) {
        const int32_t *a=xy[i],*b=xy[(i+1)%count];
        if(a[1]==b[1])continue;
        if(a[1]>b[1]){const int32_t *t=a;a=b;b=t;}
        int y0=first_line(a[1]),y1=first_line(b[1]);
        if(y0<top)y0=top;
        if(y1>bottom)y1=bottom;
        if(y0>=y1)continue;
        const int32_t dy=b[1]-a[1];const int64_t dx=(int64_t)b[0]-a[0];
        int64_t x=a[0]+divide(dx*((int64_t)y0*65536+32768-a[1]),dy);
        const int64_t step=divide(dx*65536,dy);
        for(int y=y0;y<y1;++y,x+=step) {
            /* Projected x fits 32 bits (fzx_view_project rejects the rest), and so
             * does every crossing between two of them; each rounding fits 16. */
            const int32_t xi=(int32_t)x;
            const int16_t lo=(int16_t)(((int64_t)xi+32767)>>16),hi=(int16_t)(((int64_t)xi-32768)>>16);
            if(lo<(int16_t)window[y][0])window[y][0]=(uint16_t)lo;
            if(hi>(int16_t)window[y][1])window[y][1]=(uint16_t)hi;
        }
    }
    for(int y=0;y<224;++y) {
        int lo=(int16_t)window[y][0],hi=(int16_t)window[y][1];
        window[y][0]=640;window[y][1]=0;
        if(y<top || y>=bottom || hi<0)continue;
        if(lo<v->clip[0])lo=v->clip[0];
        if(hi>=v->clip[2])hi=v->clip[2]-1;
        if(lo<=hi){window[y][0]=(uint16_t)(lo*2);window[y][1]=(uint16_t)(hi*2);++*visible_lines;}
    }
}
