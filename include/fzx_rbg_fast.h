#ifndef FZX_RBG_FAST_H
#define FZX_RBG_FAST_H
/* FZX_RBG_FAST=1 (default 0): cheaper per-frame preparation of the floor and
 * cloud rotation layers. Two replacements, shared by fzx_floor_rbg.c and
 * fzx_clouds_rbg.c:
 *  - projection divides through the SH-2 64/32 divider whenever the quotient
 *    fits 32 bits (the same check the window code already uses), otherwise the
 *    portable 64-bit divide: the projected values are unchanged;
 *  - the line window is built by walking each polygon edge once with a 16.16
 *    step instead of intersecting every edge on every line. The step's
 *    rounding can move an edge pixel only where the exact crossing lies within
 *    about 0.004 px of a pixel boundary.
 * Disabled, nothing here is compiled. */
#include <stdint.h>
#include <stdbool.h>
#include "fzx_view.h"

static inline int64_t fzx_rbg_div(int64_t n,int32_t d){
#if defined(__sh__)
    if(d<0){d=-d;n=-n;}
    if(d>0 && n>=(int64_t)INT32_MIN*d && n<=(int64_t)INT32_MAX*d){
        *(volatile uint32_t *)0xffffff00u=(uint32_t)d;
        *(volatile uint32_t *)0xffffff10u=(uint32_t)((uint64_t)n>>32);
        *(volatile uint32_t *)0xffffff14u=(uint32_t)n;
        return (int32_t)*(volatile uint32_t *)0xffffff14u;
    }
#endif
    return n/d;
}
/* fzx_view_project with the divider: same results. */
static inline bool fzx_rbg_project(const fzx_view_t *v,const int32_t p[3],int32_t out[2]){
    if(p[2]<=0)return false;
    const int64_t x=v->center[0]+fzx_rbg_div((int64_t)v->focal[0]*p[0],p[2]);
    const int64_t y=v->center[1]-fzx_rbg_div((int64_t)v->focal[1]*p[1],p[2]);
    if(x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX)return false;
    out[0]=(int32_t)x;out[1]=(int32_t)y;return true;
}
static inline int32_t fzx_rbg_first_line(int32_t y){
    /* Smallest line l with l*65536+32768 >= y (an arithmetic shift floors). */
    const int64_t n=(int64_t)y-32768;
    return (int32_t)-((-n)>>16);
}
/* Line window of a convex polygon xy[count] (16.16 screen coordinates):
 * window[line] = {2*lo,2*hi} as the per-line original, {640,0} where empty. */
static inline void fzx_rbg_window(const fzx_view_t *v,const int32_t xy[10][2],unsigned count,
    uint16_t window[224][2],uint32_t *visible_lines){
    /* Projected x fits 32 bits (fzx_rbg_project rejects the rest), so does
     * every crossing between two of them. */
    static int32_t left[224],right[224];
    const int top=v->clip[1]<0?0:v->clip[1],bottom=v->clip[3]>224?224:v->clip[3];
    for(int y=0;y<224;++y){window[y][0]=640;window[y][1]=0;left[y]=INT32_MAX;right[y]=INT32_MIN;}
    for(unsigned i=0;i<count;++i){
        const int32_t *a=xy[i],*b=xy[(i+1)%count];
        if(a[1]==b[1])continue;
        if(a[1]>b[1]){const int32_t *t=a;a=b;b=t;}
        int y0=fzx_rbg_first_line(a[1]),y1=fzx_rbg_first_line(b[1]);
        if(y0<top)y0=top;
        if(y1>bottom)y1=bottom;
        if(y0>=y1)continue;
        const int32_t dy=b[1]-a[1];const int64_t dx=(int64_t)b[0]-a[0];
        int64_t x=a[0]+fzx_rbg_div(dx*((int64_t)y0*65536+32768-a[1]),dy);
        const int64_t step=fzx_rbg_div(dx*65536,dy);
        for(int y=y0;y<y1;++y,x+=step){
            const int32_t xi=(int32_t)x;
            if(xi<left[y])left[y]=xi;
            if(xi>right[y])right[y]=xi;
        }
    }
    for(int y=top;y<bottom;++y){
        const int32_t l=left[y],r=right[y];
        if(l>r || r<32768)continue;
        int lo=(int)(((int64_t)l+32767)/65536),hi=(int)(((int64_t)r-32768)/65536);
        if(lo<v->clip[0])lo=v->clip[0];
        if(hi>=v->clip[2])hi=v->clip[2]-1;
        if(lo<=hi){window[y][0]=(uint16_t)(lo*2);window[y][1]=(uint16_t)(hi*2);++*visible_lines;}
    }
}
#endif
