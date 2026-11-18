#include "fzx_planes.h"
#include <limits.h>
#include <string.h>
#if FZX_PLANES_DEGENERATE_UV
uint32_t fzx_plane_degenerate_layers; /* layers skipped for a degenerate UV basis */
#endif

typedef struct {
    const fzx_view_t *view;const uint16_t *fog,*mean;
    int32_t base[3],s0,t0,shade0,shade_middle,shade_s,shade_t;
    int64_t ds[3],dt[3];
    uint32_t capacity,layer;fzx_plane_emit_fn emit;void *context;fzx_plane_stats_t *stats;
} plane_t;
static int32_t floor_div(int32_t a,int32_t b){return a/b-(a%b<0);}
static int64_t divide(int64_t n,int32_t d) {
#if defined(__sh__)
    /* Same signed 64/32 DIVU path used by the native core. The range test
     * prevents quotient overflow; distant guard-band cases retain the exact
     * software expression. Vblank handling does not use the division unit. */
    if(n>=(int64_t)INT32_MIN*d && n<=(int64_t)INT32_MAX*d) {
        volatile uint32_t *const divisor=(volatile uint32_t *)0xffffff00u;
        volatile uint32_t *const high=(volatile uint32_t *)0xffffff10u;
        volatile uint32_t *const low=(volatile uint32_t *)0xffffff14u;
        *divisor=(uint32_t)d;*high=(uint32_t)((uint64_t)n>>32);*low=(uint32_t)n;
        return (int32_t)*low;
    }
#endif
    return n/d;
}
static bool project(const fzx_view_t *view,const int32_t p[3],int32_t xy[2]) {
    if(p[2]<=0)return false;
    const int64_t x=view->center[0]+divide((int64_t)view->focal[0]*p[0],p[2]);
    const int64_t y=view->center[1]-divide((int64_t)view->focal[1]*p[1],p[2]);
    if(x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX)return false;
    xy[0]=(int32_t)x;xy[1]=(int32_t)y;return true;
}
static bool fault(plane_t *p,uint32_t code){p->stats->fault=code;
        return false;}
static void point(const plane_t *p,int32_t s,int32_t t,int32_t out[3]) {
    for(unsigned j=0;j<3;++j)out[j]=p->base[j]+(int32_t)(((int64_t)p->ds[j]*(s-p->s0)+(int64_t)p->dt[j]*(t-p->t0))/65536);
}
static int32_t fade(const plane_t *p,int32_t s,int32_t t) {
    int64_t n=(int64_t)p->shade0+((int64_t)p->shade_s*(s-p->s0)+(int64_t)p->shade_t*(t-p->t0))/65536;
    /* The two source halves meet at zero and rise toward either outer edge. */
    if(n<p->shade_middle)n=2*p->shade_middle-n;
    if(n>255)n=255;
    return (int32_t)n;
}
static bool submit(plane_t *p,const fzx_plane_quad_t *quad) {
    const fzx_plane_quad_t q=*quad;
    int64_t area=0;
    for(unsigned i=0;i<4;++i)area+=(int32_t)q.x[i]*q.y[(i+1)&3]-(int32_t)q.y[i]*q.x[(i+1)&3];
    if(!area){++p->stats->clipped;
        return true;}
    if(p->stats->draws>=p->capacity)return fault(p,2);
    if(!p->emit(p->context,&q))return fault(p,3);
    uint32_t h=p->stats->hash;
    for(unsigned i=0;i<4;++i) {
        h=(h^(uint32_t)(int32_t)q.x[i])*16777619u;
        h=(h^(uint32_t)(int32_t)q.y[i])*16777619u;
        h=(h^q.shade[i])*16777619u;
    }
    h=(h^q.offset)*16777619u;h=(h^q.width)*16777619u;h=(h^q.height)*16777619u;
    p->stats->hash=h;
    ++p->stats->draws;
        return true;
}
static bool tile(plane_t *p,int32_t s,int32_t t,unsigned level,unsigned part_x,unsigned part_y) {
    const unsigned width=64u>>level,height=32u>>level;
    const int32_t ss[4]={s,s+(int32_t)width*32,s+(int32_t)width*32,s};
    const int32_t tt[4]={t,t,t+(int32_t)height*32,t+(int32_t)height*32};
    int32_t v[4][3],xy[4][2],shades[4];unsigned all_out=63;bool safe=true;
    int32_t minx=INT32_MAX,maxx=INT32_MIN,miny=INT32_MAX,maxy=INT32_MIN;
    int32_t minimum=INT32_MAX,maximum=0,shade_min=255,shade_max=0;
    for(unsigned i=0;i<4;++i) {
        point(p,ss[i],tt[i],v[i]);shades[i]=fade(p,ss[i],tt[i]);
        if(shades[i]<shade_min)shade_min=shades[i];
        if(shades[i]>shade_max)shade_max=shades[i];
        unsigned out=0;for(unsigned j=0;j<6;++j)if(fzx_view_plane(p->view,v[i],j)<0)out|=1u<<j;
        all_out&=out;
        if(v[i][2]<minimum)minimum=v[i][2];
        if(v[i][2]>maximum)maximum=v[i][2];
    }
    if(all_out || (p->layer && shade_min==255)){++p->stats->clipped;return true;}
    for(unsigned i=0;i<4;++i) {
        if(!project(p->view,v[i],xy[i])){safe=false;continue;}
        const int32_t x=xy[i][0]/65536,y=xy[i][1]/65536;
        if(x<minx)minx=x;
        if(x>maxx)maxx=x;
        if(y<miny)miny=y;
        if(y>maxy)maxy=y;
        if(x<-900 || x>900 || y<-900 || y>900 || v[i][2]<p->view->near_plane)safe=false;
    }
    const int32_t variation=p->layer?32:80;
    if(level<3 && (!safe || (int64_t)maximum>2*(int64_t)minimum ||
       maxx-minx>112 || maxy-miny>112 || shade_max-shade_min>variation)) {
        ++p->stats->split;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            if(!tile(p,s+(int32_t)x*width*16,t+(int32_t)y*height*16,level+1,part_x*2+x,part_y*2+y))return false;
        return true;
    }
    fzx_plane_quad_t q={0};
    const int32_t middle=fade(p,s+(int32_t)width*16,t+(int32_t)height*16);
    const unsigned variant=(unsigned)(middle*7+127)/255;
    q.layer=(uint8_t)p->layer;q.level=(uint8_t)level;q.variant=(uint8_t)variant;
    q.width=(uint16_t)width;q.height=(uint16_t)height;
    q.offset=p->layer*FZX_PLANE_LAYER_BYTES+variant*16384u+level*4096u+
             (part_y*(1u<<level)+part_x)*width*height*2u;
    for(unsigned i=0;i<4;++i) {
        q.shade[i]=0x8000;
        for(unsigned c=0;c<3;++c) {
            int n=16;
            if(!p->layer)n+=(shades[i]-(int32_t)(variant*255u/7u))*((int)p->fog[c]-(int)p->mean[c])/255;
            if(n<0)n=0;
        if(n>31)n=31;
            q.shade[i]|=(uint16_t)(n<<(c*5));
        }
    }
    if(safe) {
        for(unsigned i=0;i<4;++i){q.x[i]=(int16_t)(xy[i][0]/65536);q.y[i]=(int16_t)(xy[i][1]/65536);}
        return submit(p,&q);
    }
    /* Only minimum 8x4 texel cells use guard-band clipping. A convex clipped
     * cell may have up to ten vertices. Pair fan triangles into quads instead
     * of rejecting or dropping the extra corners. Each piece reuses this small
     * source subtile: bounded affine texture remapping is an explicit native
     * adaptation at the near/coordinate guard, not missing plane coverage. */
    fzx_view_t guard=*p->view;guard.clip[0]=guard.clip[1]=-900;guard.clip[2]=guard.clip[3]=900;
    int32_t clipped[10][3],projected[10][2];
    int n=fzx_view_clip(&guard,(const int32_t (*)[3])v,4,clipped);
    if(n<0)return fault(p,4);
    if(n<3){++p->stats->clipped;return true;}
    for(int i=0;i<n;++i)if(!project(&guard,clipped[i],projected[i]))return fault(p,4);
    ++p->stats->near_adapted;
    const fzx_plane_quad_t original=q;
    for(unsigned first=1;first+1<(unsigned)n;first+=2) {
        const unsigned last=first+2<(unsigned)n?first+2:(unsigned)n-1;
        const unsigned indices[4]={0,first,first+1,last};
        q=original;
        for(unsigned i=0;i<4;++i) {
            q.x[i]=(int16_t)(projected[indices[i]][0]/65536);
            q.y[i]=(int16_t)(projected[indices[i]][1]/65536);
        }
        if(!submit(p,&q))return false;
    }
    return true;
}

static bool layer(plane_t *p,const RefBackdropVertex vertices[8]) {
    int32_t positions[3][3];
    for(unsigned i=0;i<3;++i) {
        int32_t world[3];for(unsigned j=0;j<3;++j)world[j]=(int32_t)vertices[i].position[j]*4096;
        if(!fzx_view_transform(p->view,world,positions[i]))return fault(p,1);
    }
    p->s0=vertices[0].uv[0];p->t0=vertices[0].uv[1];
    const int32_t ds1=vertices[1].uv[0]-p->s0,dt1=vertices[1].uv[1]-p->t0;
    const int32_t ds2=vertices[2].uv[0]-p->s0,dt2=vertices[2].uv[1]-p->t0;
    const int64_t det=(int64_t)ds1*dt2-(int64_t)ds2*dt1;
#if FZX_PLANES_DEGENERATE_UV
    /* The source scales cloud UVs by (cloud plane - eye.y) / (y - eye.y)
     * (Background_UpdateCloudVtx): as the eye crosses the cloud plane every UV
     * collapses to one point, so the layer spans under one texel and there is
     * no texture to map. Draw nothing for it rather than fault (approximation:
     * the original shows that single texel across the layer). */
    if(!det){++fzx_plane_degenerate_layers;return true;}
#endif
    if(!det)return fault(p,1);
    for(unsigned j=0;j<3;++j) {
        p->base[j]=positions[0][j];
        const int64_t a=(int64_t)positions[1][j]-p->base[j],b=(int64_t)positions[2][j]-p->base[j];
        const int64_t s=(a*dt2-b*dt1)*65536/det,t=(b*ds1-a*ds2)*65536/det;
        /* Distant cloud UVs can span less than one texel per world unit.
         * Keep the Q16 view-space derivatives wide until point evaluation. */
        p->ds[j]=s;p->dt[j]=t;
    }
    const unsigned channel=p->layer?3:0;
    p->shade0=vertices[0].color[channel];
    p->shade_middle=vertices[2].color[channel];
    const int64_t a=(int)vertices[1].color[channel]-p->shade0,b=(int)vertices[2].color[channel]-p->shade0;
    p->shade_s=(int32_t)((a*dt2-b*dt1)*65536/det);
    p->shade_t=(int32_t)((b*ds1-a*ds2)*65536/det);
    int32_t lo[2]={INT32_MAX,INT32_MAX},hi[2]={INT32_MIN,INT32_MIN};
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<2;++j) {
        const int32_t n=vertices[i].uv[j]+(i>=4?vertices[2].uv[j]-vertices[4].uv[j]:0);
        if(n<lo[j])lo[j]=n;
        if(n>hi[j])hi[j]=n;
    }
    /* Bound the tile walk by the visible source plane, once per layer.
     * Invert the largest-area view-axis pair at source-unit precision; a
     * two-texel margin conservatively covers that subtexel rounding. */
    int32_t boundary[4][3],visible[10][3];
    static const unsigned outer[4]={0,1,7,6};
    for(unsigned i=0;i<4;++i) {
        int32_t world[3];for(unsigned j=0;j<3;++j)world[j]=(int32_t)vertices[outer[i]].position[j]*4096;
        if(!fzx_view_transform(p->view,world,boundary[i]))return fault(p,1);
    }
    int visible_count=fzx_view_clip(p->view,(const int32_t (*)[3])boundary,4,visible);
    if(visible_count<0)return fault(p,4);
    if(!visible_count)return true;
    unsigned axis_a=0,axis_b=1;int64_t best=0;
    int32_t coarse[3][3];
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)coarse[i][j]=positions[i][j]/4096;
    for(unsigned a=0;a<3;++a)for(unsigned b=a+1;b<3;++b) {
        int64_t d=(int64_t)(coarse[1][a]-coarse[0][a])*(coarse[2][b]-coarse[0][b])-
                  (int64_t)(coarse[2][a]-coarse[0][a])*(coarse[1][b]-coarse[0][b]);
        if((d<0?-d:d)>(best<0?-best:best)){best=d;axis_a=a;axis_b=b;}
    }
    if(!best)return fault(p,1);
    int32_t clipped_lo[2]={INT32_MAX,INT32_MAX},clipped_hi[2]={INT32_MIN,INT32_MIN};
    for(int i=0;i<visible_count;++i) {
        const int32_t x=visible[i][axis_a]/4096-coarse[0][axis_a],y=visible[i][axis_b]/4096-coarse[0][axis_b];
        const int64_t a=(int64_t)x*(coarse[2][axis_b]-coarse[0][axis_b])-(int64_t)y*(coarse[2][axis_a]-coarse[0][axis_a]);
        const int64_t b=(int64_t)y*(coarse[1][axis_a]-coarse[0][axis_a])-(int64_t)x*(coarse[1][axis_b]-coarse[0][axis_b]);
        const int32_t uv[2]={p->s0+(int32_t)((a*ds1+b*ds2)/best),p->t0+(int32_t)((a*dt1+b*dt2)/best)};
        for(unsigned j=0;j<2;++j){if(uv[j]<clipped_lo[j])clipped_lo[j]=uv[j];if(uv[j]>clipped_hi[j])clipped_hi[j]=uv[j];}
    }
    for(unsigned j=0;j<2;++j){if(lo[j]<clipped_lo[j]-64)lo[j]=clipped_lo[j]-64;if(hi[j]>clipped_hi[j]+64)hi[j]=clipped_hi[j]+64;}
    const int32_t left=floor_div(lo[0],2048),right=floor_div(hi[0],2048);
    const int32_t top=floor_div(lo[1],1024),bottom=floor_div(hi[1],1024);
    if(right-left>64 || bottom-top>64)return fault(p,1);
    for(int32_t y=top;y<=bottom;++y)for(int32_t x=left;x<=right;++x) {
        ++p->stats->tiles;
        if(!tile(p,x*2048,y*1024,0,0,0))return false;
    }
    return true;
}
bool fzx_planes_draw(const fzx_view_t *view,const RefBackdrop *source,
                     const uint16_t fog[3],const uint16_t mean[3],uint32_t capacity,
                     fzx_plane_emit_fn emit,void *context,fzx_plane_stats_t *stats) {
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));
    stats->hash=2166136261u;
    if(!view || !source || !fog || !mean || !emit || !capacity || source->faults || source->count!=20 ||
       view->near_plane<=0 || view->far_plane<=view->near_plane || view->focal[0]<=0 || view->focal[1]<=0) {
        stats->fault=1;
        return false;
    }
    plane_t p={.view=view,.fog=fog,.mean=mean,.capacity=capacity,.emit=emit,.context=context,.stats=stats};

#if !FZX_SOURCE_FLOOR_RBG
    if(!layer(&p,source->vertices))return false;
#endif
    p.layer=1;
    return layer(&p,source->vertices+12);
}
