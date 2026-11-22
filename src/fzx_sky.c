#include "fzx_sky.h"
#include <limits.h>
#include <string.h>
#if defined(FZX_ENGINE) && FZX_ENGINE
#define FZX_SKY_FAST 1
#include "fzx_fraction.h"
#else
#define FZX_SKY_FAST 0
#endif

enum { MAX_VERTICES=12 };
typedef struct { int32_t p[3],s; } vertex_t;

static uint64_t fraction(int64_t a,int64_t b) {
    uint64_t n=a<0?0u-(uint64_t)a:(uint64_t)a;
    const uint64_t other=b<0?0u-(uint64_t)b:(uint64_t)b;
    if(!other)return UINT64_C(4294967296);
#if FZX_SKY_FAST
    return fzx_fraction(n,n+other);
#else
    const uint64_t d=n+other;
    uint64_t q=0;
    for(unsigned i=0;i<32;++i) {
        q<<=1;
        if(n>=d-n){n-=d-n;q|=1u;}else n+=n;
    }
    return q;
#endif
}
/* The same product, with a 32x32->64 multiply when both factors fit 32 bits (m < 2^32 always; t < 2^32 except the
 * whole-edge fraction 2^32 of fraction()). */
static int32_t lerp(int32_t a,int32_t b,uint64_t t) {
    const int64_t d=(int64_t)b-a;
    const uint64_t m=(uint64_t)(d<0?-d:d);
#if FZX_SKY_FAST
    const uint64_t product=((m|t)>>32)?m*t:(uint64_t)(uint32_t)m*(uint32_t)t;
#else
    const uint64_t product=m*t;
#endif
    const int64_t step=(int64_t)(product>>32);
    return (int32_t)((int64_t)a+(d<0?-step:step));
}
static int64_t distance(const fzx_view_t *view,const vertex_t *v,
                        unsigned plane,int32_t bound) {
    if(plane<6)return fzx_view_plane(view,v->p,plane);
    return plane==6?(int64_t)v->s-bound:(int64_t)bound-v->s;
}
static bool ordered(const vertex_t *a,const vertex_t *b) {
    for(unsigned i=0;i<3;++i)if(a->p[i]!=b->p[i])return a->p[i]<b->p[i];
    return a->s<=b->s;
}
static int clip(const fzx_view_t *view,const vertex_t *in,unsigned count,
                vertex_t *out,unsigned plane,int32_t bound) {
    unsigned n=0;
    const vertex_t *p=&in[count-1];
    int64_t dp=distance(view,p,plane,bound);
    for(unsigned i=0;i<count;++i) {
        const vertex_t *c=&in[i];const int64_t dc=distance(view,c,plane,bound);
        if((dp<0)!=(dc<0)) {
            if(n==MAX_VERTICES)return -1;
            vertex_t *q=&out[n++];
            const vertex_t *a=p,*b=c;int64_t da=dp,db=dc;
            if(!ordered(a,b)){a=c;b=p;da=dc;db=dp;}
            const uint64_t t=fraction(da,db);
            for(unsigned j=0;j<3;++j)q->p[j]=lerp(a->p[j],b->p[j],t);
            q->s=lerp(a->s,b->s,t);
            if(plane>=6)q->s=bound;
            if(q->p[2]<view->near_plane)q->p[2]=view->near_plane;
            if(q->p[2]>view->far_plane)q->p[2]=view->far_plane;
        }
        if(dc>=0) {if(n==MAX_VERTICES)return -1;out[n++]=*c;}
        p=c;dp=dc;
    }
    return (int)n;
}
static vertex_t at_s(const vertex_t *a,const vertex_t *b,int32_t s) {
    /* Use one orientation and the original edge for both adjacent bands.
     * Sequentially clipping a rounded lower edge before the upper edge can
     * put the same shared endpoint on opposite sides of a pixel midpoint. */
    if(a->s>b->s){const vertex_t *swap=a;a=b;b=swap;}
    /* Authored S is s16 * 2048 and clipping stays inside that range, so
     * this denominator is at most 134,215,680. Keep the exact Q0.32 ratio
     * but avoid doing each division step in two-word arithmetic on SH-2. */
    uint32_t n=(uint32_t)((int64_t)s-a->s);
    const uint32_t d=(uint32_t)((int64_t)b->s-a->s);
#if FZX_SKY_FAST
    const uint64_t t=n<d&&d<=0x7fffffffu?fzx_q32_ratio(n,d):fzx_fraction_loop(n,d);
#else
    uint32_t q32=0;
    for(unsigned i=0;i<32;++i) {
        q32<<=1;
        if(n>=d-n){n-=d-n;q32|=1u;}else n+=n;
    }
    const uint64_t t=q32;
#endif
    vertex_t q;
    for(unsigned j=0;j<3;++j)q.p[j]=lerp(a->p[j],b->p[j],t);
    q.s=s;return q;
}
static int band_clip(const vertex_t *in,unsigned count,vertex_t *out,int32_t low,int32_t high) {
    unsigned n=0;
    const vertex_t *p=&in[count-1];
    for(unsigned i=0;i<count;++i) {
        const vertex_t *c=&in[i];
        const int32_t bounds[2]={p->s<c->s?low:high,p->s<c->s?high:low};
        for(unsigned j=0;j<2;++j) {
            const int32_t s=bounds[j];
            if((p->s<s && c->s>s)||(p->s>s && c->s<s)) {
                if(n==MAX_VERTICES)return -1;
                out[n++]=at_s(p,c,s);
            }
        }
        if(c->s>=low && c->s<=high){if(n==MAX_VERTICES)return -1;out[n++]=*c;}
        p=c;
    }
    return (int)n;
}
static uint16_t sample(const uint16_t colors[64],int32_t s) {
    if(s<=0)return colors[0];
    if(s>=63*65536)return colors[63];
    const unsigned at=(unsigned)s>>16,part=(unsigned)s&65535u;
    uint16_t result=0x8000;
    for(unsigned shift=0;shift<15;shift+=5) {
        const unsigned a=(colors[at]>>shift)&31,b=(colors[at+1]>>shift)&31;
        result|=(uint16_t)(((a*(65536u-part)+b*part+32768u)>>16)<<shift);
    }
    return result;
}
static int16_t pixel(int32_t value,int16_t lower,int16_t upper) {
    int64_t n=((int64_t)value+32768)/65536;
    if(n<lower)n=lower;
    if(n>=upper)n=upper-1;
    return (int16_t)n;
}
static bool emit_polygon(const fzx_view_t *view,const vertex_t *v,unsigned count,
                         const uint16_t colors[64],uint32_t capacity,
                         fzx_sky_emit_fn emit,void *context,fzx_sky_stats_t *stats) {
    int16_t x[MAX_VERTICES],y[MAX_VERTICES];uint16_t shade[MAX_VERTICES];
    for(unsigned i=0;i<count;++i) {
        int32_t xy[2];
        if(!fzx_view_project(view,v[i].p,xy)){stats->fault=3;return false;}
        x[i]=pixel(xy[0],view->clip[0],view->clip[2]);
        y[i]=pixel(xy[1],view->clip[1],view->clip[3]);
        shade[i]=sample(colors,v[i].s);
    }
    /* Convex clipped strips become native quads; a final triangle repeats its
     * last corner. The original source diagonal remains a boundary. */
    for(unsigned i=1;i+1<count;i+=2) {
        const unsigned ids[4]={0,i,i+1,i+2<count?i+2:i+1};
        int64_t area=0;
        fzx_sky_polygon_t p;
        for(unsigned j=0;j<4;++j) {
            const unsigned a=ids[j],b=ids[(j+1)&3];
            p.x[j]=x[a];p.y[j]=y[a];p.color[j]=shade[a];
            area+=(int64_t)x[a]*y[b]-(int64_t)y[a]*x[b];
        }
        if(!area)continue;
        if(stats->polygons>=capacity){stats->fault=4;return false;}
        if(!emit(context,&p)){stats->fault=5;return false;}
        ++stats->polygons;
    }
    return true;
}
bool fzx_sky_draw_bands(const fzx_view_t *view,const RefBackdropVertex vertices[4],
                       const uint16_t colors[64],const uint8_t *breaks,unsigned break_count,
                       uint32_t capacity,fzx_sky_emit_fn emit,void *context,fzx_sky_stats_t *stats) {
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));
    if(!view || !vertices || !colors || !emit || !capacity ||
       view->near_plane<=0 || view->far_plane<=view->near_plane ||
       view->focal[0]<=0 || view->focal[1]<=0 ||
       view->clip[0]<0 || view->clip[1]<0 || view->clip[2]>320 || view->clip[3]>224 ||
       view->clip[0]>=view->clip[2] || view->clip[1]>=view->clip[3]) {
        stats->fault=1;return false;
    }
    for(unsigned i=0;i<64;++i)if(!(colors[i]&0x8000)){stats->fault=1;return false;}
    if(breaks) {
        if(break_count<2 || break_count>64 || breaks[0]!=0 || breaks[break_count-1]!=63) {
            stats->fault=1;return false;
        }
        for(unsigned i=1;i<break_count;++i)if(breaks[i]<=breaks[i-1]){stats->fault=1;return false;}
    } else if(break_count){stats->fault=1;return false;}
    vertex_t source[4];
    for(unsigned i=0;i<4;++i) {
        int32_t world[3];
        for(unsigned j=0;j<3;++j)world[j]=(int32_t)vertices[i].position[j]*4096;
        if(!fzx_view_transform(view,world,source[i].p)){stats->fault=2;return false;}
        source[i].s=(int32_t)vertices[i].uv[0]*2048;
    }
    static const unsigned indices[2][3]={{0,3,1},{0,2,3}};
    for(unsigned triangle=0;triangle<2;++triangle) {
        vertex_t buffers[2][MAX_VERTICES],visible[MAX_VERTICES];
        unsigned side=0;int count=3;
        for(unsigned j=0;j<3;++j)buffers[0][j]=source[indices[triangle][j]];
        for(unsigned plane=0;plane<6 && count>=3;++plane) {
            count=clip(view,buffers[side],(unsigned)count,buffers[1-side],plane,0);side=1-side;
        }
        if(count<0){stats->fault=6;return false;}
        if(count<3){++stats->clipped;continue;}
        const unsigned visible_count=(unsigned)count;
        memcpy(visible,buffers[side],visible_count*sizeof(vertex_t));
        int32_t minimum=visible[0].s,maximum=visible[0].s;
        for(unsigned i=1;i<visible_count;++i) {
            if(visible[i].s<minimum)minimum=visible[i].s;
            if(visible[i].s>maximum)maximum=visible[i].s;
        }
        const unsigned boundary_count=breaks?break_count:64;
        /* Include the constant-color clamp regions on both ends. */
        for(unsigned band=0;band<=boundary_count;++band) {
            const int32_t low=band?(int32_t)(breaks?breaks[band-1]:band-1)*65536:INT32_MIN;
            const int32_t high=band==boundary_count?INT32_MAX:(int32_t)(breaks?breaks[band]:band)*65536;
            if(maximum<low || minimum>high)continue;
            count=band_clip(visible,visible_count,buffers[0],low,high);
            if(count<0){stats->fault=6;return false;}
            if(count<3)continue;
            ++stats->bands;
            if(!emit_polygon(view,buffers[0],(unsigned)count,colors,capacity,emit,context,stats))return false;
        }
    }
    return true;
}
bool fzx_sky_draw(const fzx_view_t *view,const RefBackdropVertex vertices[4],
                  const uint16_t colors[64],uint32_t capacity,
                  fzx_sky_emit_fn emit,void *context,fzx_sky_stats_t *stats) {
    return fzx_sky_draw_bands(view,vertices,colors,NULL,0,capacity,emit,context,stats);
}
