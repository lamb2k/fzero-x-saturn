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
/* fzx_view_clip, same results: the plane tests use 32x32->64 products (every
 * factor fits 32 bits for a validated view: |center| <= 4096 px, clip within
 * the screen), the inward snap divides on the SH-2 divider with the remainder
 * recomputed exactly, and the edge step multiplies 32x32 bits. */
static inline int64_t fzx_rbg_plane(const fzx_view_t *v,const int32_t p[3],unsigned plane){
    switch(plane){
    case 0:return (int64_t)p[2]-v->near_plane;
    case 1:return (int64_t)v->far_plane-p[2];
    case 2:return (int64_t)v->focal[0]*p[0]+(int64_t)(v->center[0]-v->clip[0]*65536)*p[2];
    case 3:return (int64_t)(v->clip[2]*65536-v->center[0])*p[2]-(int64_t)v->focal[0]*p[0];
    case 4:return (int64_t)(v->center[1]-v->clip[1]*65536)*p[2]-(int64_t)v->focal[1]*p[1];
    default:return (int64_t)v->focal[1]*p[1]+(int64_t)(v->clip[3]*65536-v->center[1])*p[2];
    }
}
static inline uint64_t fzx_rbg_fraction32(int64_t dp,int64_t dc){
    uint64_t a=dp<0?0u-(uint64_t)dp:(uint64_t)dp;
    const uint64_t b=dc<0?0u-(uint64_t)dc:(uint64_t)dc;
    if(b==0)return UINT64_C(4294967296);
    const uint64_t d=a+b;uint64_t q=0;
    for(unsigned i=0;i<32;++i){q<<=1;if(a>=d-a){a-=d-a;q|=1u;}else a+=a;}
    return q;
}
/* Floor/ceiling of n/d for d>0, via the divider when the quotient fits. */
static inline int64_t fzx_rbg_floor_ceil(int64_t n,int32_t d,bool ceiling){
    const int64_t q=fzx_rbg_div(n,d),r=n-q*(int64_t)d;
    return ceiling?q+(r>0):q-(r<0);
}
static inline bool fzx_rbg_inward(const fzx_view_t *v,int32_t p[3],unsigned plane){
    if(p[2]<v->near_plane)p[2]=v->near_plane;
    if(plane>=1 && p[2]>v->far_plane)p[2]=v->far_plane;
    for(unsigned k=2;k<=plane;++k){
        const unsigned axis=k<4?0u:1u;const bool lower=k==2 || k==5;
        const unsigned ci=k==2?0u:k==3?2u:k==4?1u:3u;
        const int64_t bound=(int64_t)(axis==0?v->clip[ci]*65536-v->center[0]:v->center[1]-v->clip[ci]*65536)*p[2];
        const int64_t n=fzx_rbg_floor_ceil(bound,v->focal[axis],lower);
        if(n<INT32_MIN || n>INT32_MAX)return false;
        if((lower && p[axis]<n) || (!lower && p[axis]>n))p[axis]=(int32_t)n;
    }
    for(unsigned k=0;k<=plane;++k)if(fzx_rbg_plane(v,p,k)<0)return false;
    return true;
}
static inline int fzx_rbg_clip(const fzx_view_t *v,const int32_t input[][3],unsigned count,int32_t output[10][3]){
    if(!v || !input || !output || count<3 || count>4 || v->near_plane<=0 ||
       v->far_plane<=v->near_plane || v->focal[0]<=0 || v->focal[1]<=0)return -1;
    /* The 32-bit plane factors need a view centre within 4096 px. */
    if(v->center[0]<-4096*65536 || v->center[0]>4096*65536 || v->center[1]<-4096*65536 || v->center[1]>4096*65536)
        return fzx_view_clip(v,input,count,output);
    int32_t buffers[2][10][3];
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<3;++j)buffers[0][i][j]=input[i][j];
    unsigned source=0;
    for(unsigned plane=0;plane<6 && count>=3;++plane){
        unsigned written=0;
        const int32_t *previous=buffers[source][count-1];
        int64_t dp=fzx_rbg_plane(v,previous,plane);
        for(unsigned i=0;i<count;++i){
            const int32_t *current=buffers[source][i];
            const int64_t dc=fzx_rbg_plane(v,current,plane);
            if((dp<0)!=(dc<0)){
                if(written>=10)return -1;
                int32_t *q=buffers[source^1u][written++];
                const uint64_t t=fzx_rbg_fraction32(dp,dc);
                for(unsigned j=0;j<3;++j){
                    const int64_t delta=(int64_t)current[j]-previous[j];
                    const uint64_t magnitude=(uint64_t)(delta<0?-delta:delta);
                    /* magnitude < 2^32; t <= 2^32. */
                    const int64_t step=t==UINT64_C(4294967296)?(int64_t)magnitude:
                        (int64_t)(((uint64_t)(uint32_t)magnitude*(uint32_t)t)>>32);
                    q[j]=(int32_t)(previous[j]+(delta<0?-step:step));
                }
                if(!fzx_rbg_inward(v,q,plane))return -1;
            }
            if(dc>=0){
                if(written>=10)return -1;
                for(unsigned j=0;j<3;++j)buffers[source^1u][written][j]=current[j];
                ++written;
            }
            previous=current;dp=dc;
        }
        count=written;source^=1u;
    }
    if(count<3)return 0;
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<3;++j)output[i][j]=buffers[source][i][j];
    return (int)count;
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
