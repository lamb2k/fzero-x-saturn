#include "fzx_view.h"
#include <limits.h>
#include <string.h>
#ifndef FZX_RBG_FAST
#define FZX_RBG_FAST 0
#endif
#if FZX_RBG_FAST
/* FZX_RBG_FAST=1: the same results with cheaper SH-2 arithmetic. Products whose
 * factors fit 32 bits use 32x32->64 multiplies; divisions whose quotient fits
 * 32 bits use the SH-2 64/32 divider (remainders recomputed exactly). Every
 * other case takes the original 64-bit path. */
static inline int64_t fv_div(int64_t n,int32_t d) {
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
static inline int64_t fv_mul(int64_t k,int32_t z) {
    return k>=INT32_MIN && k<=INT32_MAX ? (int64_t)(int32_t)k*z : k*z;
}
#endif

static bool view_finite(float x) {
    uint32_t bits; memcpy(&bits,&x,4);
    return (bits&0x7f800000u)!=0x7f800000u;
}
static bool fixed(float x, float scale, float bound, int32_t *out) {
    if(!view_finite(x) || x < -bound || x > bound) return false;
    *out=(int32_t)(x*scale);
    return true;
}

bool fzx_view_from_source(fzx_view_t *out, const float v[4][4],
                         const float p[4][4], const float vp[4],
                         const float sc[4], float near_plane, float far_plane) {
    if(!out || !v || !p || !vp || !sc) return false;
    fzx_view_t a={0};
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j)
        if(!view_finite(v[i][j]) || !view_finite(p[i][j])) return false;
    if(v[0][3]!=0 || v[1][3]!=0 || v[2][3]!=0 || v[3][3]!=1 ||
       p[0][1]!=0 || p[0][2]!=0 || p[0][3]!=0 || p[1][0]!=0 ||
       p[1][2]!=0 || p[1][3]!=0 || p[2][3]!=-1 || p[3][0]!=0 ||
       p[3][1]!=0 || p[3][3]!=0 || p[0][0]<=0 || p[1][1]<=0) return false;
    for(unsigned row=0;row<3;++row) {
        const float sign=row==2 ? -1.0f : 1.0f;
        for(unsigned col=0;col<3;++col)
            if(!fixed(sign*v[col][row],1073741824.0f,1.01f,&a.rotation[row][col])) return false;
        if(!fixed(sign*v[3][row],4096.0f,131072.0f,&a.translation[row])) return false;
    }
    for(unsigned i=0;i<4;++i) if(!view_finite(vp[i]) || !view_finite(sc[i])) return false;
    if(vp[0]<=0 || vp[1]<=0 || vp[0]>320 || vp[1]>240) return false;
    if(!fixed(vp[0]*p[0][0],65536.0f,4096.0f,&a.focal[0]) ||
       !fixed(vp[1]*p[1][1],65536.0f,4096.0f,&a.focal[1]) ||
       !fixed(vp[2]-vp[0]*p[2][0],65536.0f,4096.0f,&a.center[0]) ||
       !fixed(vp[3]-8.0f+vp[1]*p[2][1],65536.0f,4096.0f,&a.center[1]) ||
       !fixed(near_plane,4096.0f,131072.0f,&a.near_plane) ||
       !fixed(far_plane,4096.0f,131072.0f,&a.far_plane)) return false;
    if(a.near_plane<=0 || a.far_plane<=a.near_plane) return false;
    const float crop[4]={sc[0],sc[1]-8.0f,sc[2],sc[3]-8.0f};
    for(unsigned i=0;i<4;++i) {
        /* Source scissor coordinates are integral in this race path. Reject
         * fractional transitions until a matching subpixel contract is added. */
        if(crop[i]<-240 || crop[i]>640 || (float)(int)crop[i]!=crop[i]) return false;
        int n=(int)crop[i],limit=(i&1u)?224:320;
        if(n<0)n=0;
        if(n>limit)n=limit;
        a.clip[i]=(int16_t)n;
    }
    if(a.clip[0]>=a.clip[2] || a.clip[1]>=a.clip[3]) return false;
    *out=a; return true;
}

bool fzx_view_transform(const fzx_view_t *v,const int32_t p[3],int32_t out[3]) {
    for(unsigned i=0;i<3;++i) if(p[i]<-536870912 || p[i]>536870912) return false;
    for(unsigned i=0;i<3;++i) {
        int64_t n=0;
        for(unsigned j=0;j<3;++j)n+=(int64_t)v->rotation[i][j]*p[j];
        n=n/1073741824+v->translation[i];
        if(n<INT32_MIN || n>INT32_MAX)return false;
        out[i]=(int32_t)n;
    }
    return true;
}
int64_t fzx_view_plane(const fzx_view_t *v,const int32_t p[3],unsigned plane) {
#if FZX_RBG_FAST
    switch(plane) {
    case 0:return (int64_t)p[2]-v->near_plane;
    case 1:return (int64_t)v->far_plane-p[2];
    case 2:return (int64_t)v->focal[0]*p[0]+fv_mul(v->center[0]-(int64_t)v->clip[0]*65536,p[2]);
    case 3:return fv_mul((int64_t)v->clip[2]*65536-v->center[0],p[2])-(int64_t)v->focal[0]*p[0];
    case 4:return fv_mul(v->center[1]-(int64_t)v->clip[1]*65536,p[2])-(int64_t)v->focal[1]*p[1];
    default:return (int64_t)v->focal[1]*p[1]+fv_mul((int64_t)v->clip[3]*65536-v->center[1],p[2]);
    }
#endif
    const int64_t x=p[0],y=p[1],z=p[2];
    switch(plane) {
    case 0:return z-v->near_plane;
    case 1:return v->far_plane-z;
    case 2:return v->focal[0]*x+(v->center[0]-(int64_t)v->clip[0]*65536)*z;
    case 3:return ((int64_t)v->clip[2]*65536-v->center[0])*z-v->focal[0]*x;
    case 4:return (v->center[1]-(int64_t)v->clip[1]*65536)*z-v->focal[1]*y;
    default:return v->focal[1]*y+((int64_t)v->clip[3]*65536-v->center[1])*z;
    }
}
bool fzx_view_project(const fzx_view_t *v,const int32_t p[3],int32_t out[2]) {
    if(p[2]<=0)return false;
#if FZX_RBG_FAST
    const int64_t x=v->center[0]+fv_div((int64_t)v->focal[0]*p[0],p[2]);
    const int64_t y=v->center[1]-fv_div((int64_t)v->focal[1]*p[1],p[2]);
#else
    const int64_t x=v->center[0]+(int64_t)v->focal[0]*p[0]/p[2];
    const int64_t y=v->center[1]-(int64_t)v->focal[1]*p[1]/p[2];
#endif
    if(x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX)return false;
    out[0]=(int32_t)x;out[1]=(int32_t)y;return true;
}
int32_t fzx_view_fraction(int64_t dp,int64_t dc) {
    /* Unsigned magnitudes also handle INT64_MIN. Opposite signs imply an
     * unsigned sum <= UINT64_MAX unless both are INT64_MIN (not a crossing). */
    uint64_t a=dp<0 ? 0u-(uint64_t)dp : (uint64_t)dp;
    const uint64_t b=dc<0 ? 0u-(uint64_t)dc : (uint64_t)dc;
    if(b==0)return 65536;
    if(a==0)return 0;
    const uint64_t d=a+b;
    uint32_t q=0;
    for(unsigned i=0;i<16;++i) {
        q<<=1;
        /* Compare before doubling, so even a denominator near 2^64 is safe. */
        if(a>=d-a) { a-=d-a; q|=1u; } else a+=a;
    }
    return (int32_t)q;
}

static uint64_t fraction32(int64_t dp,int64_t dc) {
    uint64_t a=dp<0 ? 0u-(uint64_t)dp : (uint64_t)dp;
    const uint64_t b=dc<0 ? 0u-(uint64_t)dc : (uint64_t)dc;
    if(b==0)return UINT64_C(4294967296);
    const uint64_t d=a+b;
    uint64_t q=0;
    for(unsigned i=0;i<32;++i) {
        q<<=1;
        if(a>=d-a) { a-=d-a; q|=1u; } else a+=a;
    }
    return q;
}
#if FZX_RBG_FAST
/* d is a positive focal length (int32). */
static int64_t floor_div(int64_t n,int32_t d) {
    const int64_t q=fv_div(n,d);
    return q-(n-fv_mul(q,d)<0);
}
static int64_t ceil_div(int64_t n,int32_t d) {
    const int64_t q=fv_div(n,d);
    return q+(n-fv_mul(q,d)>0);
}
#else
static int64_t floor_div(int64_t n,int64_t d) {
    return n/d-(n%d<0);
}
static int64_t ceil_div(int64_t n,int64_t d) {
    return n/d+(n%d>0);
}
#endif
/* Reapply processed planes to rounded intersections. Each side constrains only
 * one coordinate at positive Z, so this cannot undo an earlier plane. */
static bool inward(const fzx_view_t *v,int32_t p[3],unsigned plane) {
    if(p[2]<v->near_plane)p[2]=v->near_plane;
    if(plane>=1 && p[2]>v->far_plane)p[2]=v->far_plane;
    for(unsigned k=2;k<=plane;++k) {
        const unsigned axis=k<4 ? 0u : 1u;
        const bool lower=k==2 || k==5;
        /* Plane order is left, right, top, bottom; clip order is L,T,R,B. */
        const unsigned ci=k==2?0u:k==3?2u:k==4?1u:3u;
#if FZX_RBG_FAST
        const int64_t bound=fv_mul(axis==0 ? (int64_t)v->clip[ci]*65536-v->center[0]
                                           : v->center[1]-(int64_t)v->clip[ci]*65536,p[2]);
#else
        const int64_t bound=(axis==0 ? (int64_t)v->clip[ci]*65536-v->center[0]
                                    : v->center[1]-(int64_t)v->clip[ci]*65536)*p[2];
#endif
        const int64_t n=lower?ceil_div(bound,v->focal[axis]):floor_div(bound,v->focal[axis]);
        if(n<INT32_MIN || n>INT32_MAX)return false;
        if((lower && p[axis]<n) || (!lower && p[axis]>n))p[axis]=(int32_t)n;
    }
    for(unsigned k=0;k<=plane;++k)if(fzx_view_plane(v,p,k)<0)return false;
    return true;
}
int fzx_view_clip(const fzx_view_t *v,const int32_t input[][3],unsigned count,
                  int32_t output[10][3]) {
    if(!v || !input || !output || count<3 || count>4 || v->near_plane<=0 ||
       v->far_plane<=v->near_plane || v->focal[0]<=0 || v->focal[1]<=0)return -1;
    int32_t buffers[2][10][3];
    memcpy(buffers[0],input,count*sizeof(buffers[0][0]));
    unsigned source=0;
    for(unsigned plane=0;plane<6 && count>=3;++plane) {
        unsigned written=0;
        const int32_t *previous=buffers[source][count-1];
        int64_t dp=fzx_view_plane(v,previous,plane);
        for(unsigned i=0;i<count;++i) {
            const int32_t *current=buffers[source][i];
            const int64_t dc=fzx_view_plane(v,current,plane);
            if((dp<0)!=(dc<0)) {
                if(written>=10)return -1;
                int32_t *q=buffers[source^1u][written++];
                const uint64_t t=fraction32(dp,dc);
                for(unsigned j=0;j<3;++j) {
                    const int64_t delta=(int64_t)current[j]-previous[j];
                    const uint64_t magnitude=(uint64_t)(delta<0?-delta:delta);
#if FZX_RBG_FAST
                    /* magnitude < 2^32 and t <= 2^32. */
                    const int64_t step=t==UINT64_C(4294967296) ? (int64_t)magnitude :
                        (int64_t)(((uint64_t)(uint32_t)magnitude*(uint32_t)t)>>32);
#else
                    const int64_t step=(int64_t)((magnitude*t)>>32);
#endif
                    q[j]=(int32_t)(previous[j]+(delta<0?-step:step));
                }
                if(!inward(v,q,plane))return -1;
            }
            if(dc>=0) {
                if(written>=10)return -1;
                memcpy(buffers[source^1u][written++],current,3*sizeof(int32_t));
            }
            previous=current;dp=dc;
        }
        count=written;source^=1u;
    }
    if(count<3)return 0;
    memcpy(output,buffers[source],count*sizeof(output[0]));
    return (int)count;
}
#if FZX_RBG_LEAN
/* 64/32 division of an edge step; the SH-2 divider when the quotient fits. */
static int64_t lean_step(int64_t n,int64_t d64) {
    const int32_t d=(int32_t)d64;
#if FZX_RBG_FAST
    return fv_div(n,d);
#else
    return n/d;
#endif
}
/* One side of the guard rectangle: axis 0 = x, 1 = y; keep values >= edge when
 * lower, <= edge otherwise. */
static unsigned lean_side(const int32_t (*in)[2],unsigned count,int32_t (*out)[2],
                          unsigned axis,int32_t edge,bool lower) {
    unsigned written=0;
    if(!count)return 0;
    const int32_t *a=in[count-1];
    bool a_in=lower?a[axis]>=edge:a[axis]<=edge;
    for(unsigned i=0;i<count;++i) {
        const int32_t *b=in[i];const bool b_in=lower?b[axis]>=edge:b[axis]<=edge;
        if(a_in!=b_in) {
            const unsigned other=axis^1u;int32_t *q=out[written++];
            q[axis]=edge;
            q[other]=(int32_t)(a[other]+lean_step(((int64_t)b[other]-a[other])*((int64_t)edge-a[axis]),b[axis]-a[axis]));
        }
        if(b_in){out[written][0]=b[0];out[written][1]=b[1];++written;}
        a=b;a_in=b_in;
    }
    return written;
}
int fzx_view_project_guarded(const fzx_view_t *v,const int32_t input[4][3],
                             int32_t guard,int32_t output[10][2]) {
    if(!v || !input || !output || guard<0 || guard>1024 || v->near_plane<=0 ||
       v->far_plane<=v->near_plane || v->focal[0]<=0 || v->focal[1]<=0)return -1;
    int32_t buffers[2][10][3];
    memcpy(buffers[0],input,4*sizeof(buffers[0][0]));
    unsigned count=4,source=0;
    for(unsigned plane=0;plane<2 && count>=3;++plane) {
        unsigned written=0;
        const int32_t *previous=buffers[source][count-1];
        int64_t dp=fzx_view_plane(v,previous,plane);
        for(unsigned i=0;i<count;++i) {
            const int32_t *current=buffers[source][i];
            const int64_t dc=fzx_view_plane(v,current,plane);
            if((dp<0)!=(dc<0)) {
                if(written>=10)return -1;
                int32_t *q=buffers[source^1u][written++];
                const uint64_t t=fraction32(dp,dc);
                for(unsigned j=0;j<3;++j) {
                    const int64_t delta=(int64_t)current[j]-previous[j];
                    const uint64_t magnitude=(uint64_t)(delta<0?-delta:delta);
                    const int64_t step=t==UINT64_C(4294967296) ? (int64_t)magnitude :
                        (int64_t)(((uint64_t)(uint32_t)magnitude*(uint32_t)t)>>32);
                    q[j]=(int32_t)(previous[j]+(delta<0?-step:step));
                }
                if(q[2]<v->near_plane)q[2]=v->near_plane;
                if(plane==1 && q[2]>v->far_plane)q[2]=v->far_plane;
            }
            if(dc>=0) {
                if(written>=10)return -1;
                memcpy(buffers[source^1u][written++],current,3*sizeof(int32_t));
            }
            previous=current;dp=dc;
        }
        count=written;source^=1u;
    }
    if(count<3)return 0;
    int32_t xy[2][10][2];
    for(unsigned i=0;i<count;++i)if(!fzx_view_project(v,buffers[source][i],xy[0][i]))return -1;
    const int32_t g=guard*65536;
    count=lean_side((const int32_t (*)[2])xy[0],count,xy[1],0,(int32_t)v->clip[0]*65536-g,true);
    count=lean_side((const int32_t (*)[2])xy[1],count,xy[0],0,(int32_t)v->clip[2]*65536+g,false);
    count=lean_side((const int32_t (*)[2])xy[0],count,xy[1],1,(int32_t)v->clip[1]*65536-g,true);
    count=lean_side((const int32_t (*)[2])xy[1],count,xy[0],1,(int32_t)v->clip[3]*65536+g,false);
    if(count<3)return 0;
    if(count>10)return -1;
    memcpy(output,xy[0],count*sizeof(output[0]));
    return (int)count;
}
#endif
