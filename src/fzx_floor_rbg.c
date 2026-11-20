#include "fzx_floor_rbg.h"
#include <limits.h>
#include <string.h>
#include "fzx_scene_phase.h"
#include "fzx_band_corners.h"
#ifndef FZX_RBG_DIV
#define FZX_RBG_DIV 0
#endif
#ifndef FZX_RBG_LEAN
#define FZX_RBG_LEAN 0
#endif
#ifndef FZX_RBG_NEAR0
#define FZX_RBG_NEAR0 0
#endif
#if FZX_RBG_FAST
#include "fzx_rbg_fast.h"
#endif

_Static_assert(sizeof(fzx_floor_rp_t)==96,"rotation parameter ABI");
static int64_t magnitude(int64_t n){return n<0?-n:n;}
static int64_t rounded(int64_t n,int64_t d){return n<0?-((-n+d/2)/d):(n+d/2)/d;}
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
static int64_t q24(int64_t n,int64_t d) {
    return (n/d)*16777216+((n%d)*16777216)/d;
}
static bool fail(fzx_floor_rbg_t *out,uint32_t fault){out->fault=fault;return false;}
uint32_t fzx_floor_rbg_coefficient(unsigned index) {
    if(index>=32768)return 0x80000000u;
    const int32_t twice=(int32_t)index*2-32768+1;
    /* Sample each reciprocal bin at its centre. Signed8.16 lives in bits23..0. */
    if(twice>=-32 && twice<=32)return 0x80000000u;
    const int32_t value=(int32_t)rounded(INT64_C(268435456),magnitude(twice));
    return (uint32_t)(twice<0?-value:value)&0x00ffffffu;
}
static void cross(const int32_t a[3],const int32_t b[3],int64_t out[3]) {
    for(unsigned i=0;i<3;++i)out[i]=(int64_t)a[(i+1)%3]*b[(i+2)%3]-(int64_t)a[(i+2)%3]*b[(i+1)%3];
}
static bool mapping(const fzx_view_t *view,const RefBackdropVertex *source,
                    const int32_t positions[8][3],fzx_floor_rbg_t *out) {
    int32_t coarse[3][3];int64_t cofactor[3][3],largest=0;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)coarse[i][j]=positions[i][j]/256;
    for(unsigned i=0;i<3;++i) {
        cross(coarse[(i+1)%3],coarse[(i+2)%3],cofactor[i]);
        for(unsigned j=0;j<3;++j)if(magnitude(cofactor[i][j])>largest)largest=magnitude(cofactor[i][j]);
    }
    if(!largest)return fail(out,2);
    int64_t divisor=1;
    while(largest/divisor>4194304)divisor*=2;
    int64_t rows[3][3]={{0}},raw[3][3],h[3][3];
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        const int64_t c=cofactor[i][j]/divisor;
        rows[0][j]+=(int64_t)source[i].uv[0]*c;
        rows[1][j]+=(int64_t)source[i].uv[1]*c;
        rows[2][j]+=c;
    }
    const int64_t rx=INT64_C(68719476736)/view->focal[0],ry=-INT64_C(68719476736)/view->focal[1];
    const int64_t x0=-(int64_t)view->center[0]*1048576/view->focal[0];
    const int64_t y0=(int64_t)view->center[1]*1048576/view->focal[1];
    if(magnitude(rx)>65536 || magnitude(ry)>65536 || magnitude(x0)>4194304 || magnitude(y0)>4194304)return fail(out,2);
    for(unsigned i=0;i<3;++i) {
        raw[i][0]=rows[i][0]*rx;raw[i][1]=rows[i][1]*ry;
        raw[i][2]=rows[i][0]*x0+rows[i][1]*y0+rows[i][2]*1048576;
        if(i<2)for(unsigned j=0;j<3;++j)raw[i][j]/=32;
    }
    int64_t extent=0;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        const int64_t d=magnitude(raw[2][0]*(x?319:0)+raw[2][1]*(y?223:0)+raw[2][2]);
        if(d>extent)extent=d;
    }
    if(!extent)return fail(out,2);
    divisor=1;
    while(extent/divisor>INT64_C(137438953472))divisor*=2;
    extent/=divisor;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        const int64_t n=raw[i][j]/divisor;
        if(magnitude(n/extent)>16777216)return fail(out,2);
        h[i][j]=q24(n,extent);
    }
    for(unsigned axis=0;axis<2;++axis) {
        int64_t numerator=0,denominator=0;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
            const int64_t d=(h[2][0]*(x?319:0)+h[2][1]*(y?223:0)+h[2][2])/256;
            const int64_t n=(h[axis][0]*(x?319:0)+h[axis][1]*(y?223:0)+h[axis][2])/256;
            if(magnitude(n)>INT64_C(268435456))return fail(out,2);
            numerator+=n*d;denominator+=d*d;
        }
        if(!denominator)return fail(out,2);
        const int32_t period=axis?32:64;
        const int64_t offset=rounded(numerator,denominator*period)*period;
        for(unsigned j=0;j<3;++j)h[axis][j]-=offset*h[2][j];
        const int64_t values[3]={h[axis][0],h[axis][1],
            (h[axis][2]+160*h[axis][0]+112*h[axis][1])/1024};
        for(unsigned j=0;j<3;++j) {
            const int64_t word=rounded(values[j],4096)*64;
            if(word<-524288 || word>=524288)return fail(out,3);
            out->rp.matrix[axis*3+j]=(int32_t)word;
        }
        /* Offsets are whole texture periods, so zero is their exact residue. */
        out->rp.move[axis]=0;
    }
    out->rp.start[0]=-160*65536;out->rp.start[1]=-112*65536;out->rp.start[2]=1024*65536;
    out->rp.line[1]=out->rp.dot[0]=65536;out->rp.scale[0]=out->rp.scale[1]=65536;
    const int64_t ka[3]={rounded(h[2][2],2)*64+INT64_C(1073741824),rounded(h[2][1],2)*64,rounded(h[2][0],2)*64};
    if(ka[0]<0 || ka[0]>UINT32_MAX || magnitude(ka[1])>=33554432 || magnitude(ka[2])>=33554432)return fail(out,3);
    out->rp.coefficient_start=(uint32_t)ka[0];out->rp.coefficient_line=(int32_t)ka[1];out->rp.coefficient_dot=(int32_t)ka[2];
    out->index_min=32767;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        const int64_t k=ka[0]+ka[1]*(y?223:0)+ka[2]*(x?319:0);
        if(k<0 || k>=INT64_C(2147483648))return fail(out,3);
        const uint32_t index=(uint32_t)(k/65536);
        if(index<out->index_min)out->index_min=index;
        if(index>out->index_max)out->index_max=index;
    }
    return true;
}
#if FZX_RBG_DIV
/* FZX_RBG_DIV (default 0): the same mapping with power-of-two divisions as
 * truncating shifts, 64/32 quotients on the SH-2 divider (divide()), a range
 * check as a product and one division per q24: the same results. */
static int64_t div_p2(int64_t n,unsigned k){return n<0?-(int64_t)((uint64_t)-n>>k):(int64_t)((uint64_t)n>>k);}
static int64_t round_p2(int64_t n,unsigned k){const int64_t h=(int64_t)1<<k>>1;return n<0?-(int64_t)((uint64_t)(-n+h)>>k):(int64_t)((uint64_t)(n+h)>>k);}
static int64_t q24_one(int64_t n,int64_t d){const int64_t q=n/d;return q*16777216+((n-q*d)*16777216)/d;}
static bool mapping_div(const fzx_view_t *view,const RefBackdropVertex *source,
                    const int32_t positions[8][3],fzx_floor_rbg_t *out) {
    int32_t coarse[3][3];int64_t cofactor[3][3],largest=0;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)coarse[i][j]=positions[i][j]/256;
    for(unsigned i=0;i<3;++i) {
        cross(coarse[(i+1)%3],coarse[(i+2)%3],cofactor[i]);
        for(unsigned j=0;j<3;++j)if(magnitude(cofactor[i][j])>largest)largest=magnitude(cofactor[i][j]);
    }
    if(!largest)return fail(out,2);
    unsigned shift=0;
    while((largest>>shift)>4194304)++shift;
    int64_t rows[3][3]={{0}},raw[3][3],h[3][3];
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        const int64_t c=div_p2(cofactor[i][j],shift);
        rows[0][j]+=(int64_t)source[i].uv[0]*c;
        rows[1][j]+=(int64_t)source[i].uv[1]*c;
        rows[2][j]+=c;
    }
    const int64_t rx=divide(INT64_C(68719476736),view->focal[0]),ry=-divide(INT64_C(68719476736),view->focal[1]);
    const int64_t x0=divide(-(int64_t)view->center[0]*1048576,view->focal[0]);
    const int64_t y0=divide((int64_t)view->center[1]*1048576,view->focal[1]);
    if(magnitude(rx)>65536 || magnitude(ry)>65536 || magnitude(x0)>4194304 || magnitude(y0)>4194304)return fail(out,2);
    for(unsigned i=0;i<3;++i) {
        raw[i][0]=rows[i][0]*rx;raw[i][1]=rows[i][1]*ry;
        raw[i][2]=rows[i][0]*x0+rows[i][1]*y0+rows[i][2]*1048576;
        if(i<2)for(unsigned j=0;j<3;++j)raw[i][j]=div_p2(raw[i][j],5);
    }
    int64_t extent=0;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        const int64_t d=magnitude(raw[2][0]*(x?319:0)+raw[2][1]*(y?223:0)+raw[2][2]);
        if(d>extent)extent=d;
    }
    if(!extent)return fail(out,2);
    shift=0;
    while((extent>>shift)>INT64_C(137438953472))++shift;
    extent>>=shift;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        const int64_t n=div_p2(raw[i][j],shift);
        if(magnitude(n)>=INT64_C(16777217)*extent)return fail(out,2);
        h[i][j]=q24_one(n,extent);
    }
    for(unsigned axis=0;axis<2;++axis) {
        int64_t numerator=0,denominator=0;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
            const int64_t d=div_p2(h[2][0]*(x?319:0)+h[2][1]*(y?223:0)+h[2][2],8);
            const int64_t n=div_p2(h[axis][0]*(x?319:0)+h[axis][1]*(y?223:0)+h[axis][2],8);
            if(magnitude(n)>INT64_C(268435456))return fail(out,2);
            numerator+=n*d;denominator+=d*d;
        }
        if(!denominator)return fail(out,2);
        const int32_t period=axis?32:64;
        const int64_t offset=rounded(numerator,denominator*period)*period;
        for(unsigned j=0;j<3;++j)h[axis][j]-=offset*h[2][j];
        const int64_t values[3]={h[axis][0],h[axis][1],
            div_p2(h[axis][2]+160*h[axis][0]+112*h[axis][1],10)};
        for(unsigned j=0;j<3;++j) {
            const int64_t word=round_p2(values[j],12)*64;
            if(word<-524288 || word>=524288)return fail(out,3);
            out->rp.matrix[axis*3+j]=(int32_t)word;
        }
        /* Offsets are whole texture periods, so zero is their exact residue. */
        out->rp.move[axis]=0;
    }
    out->rp.start[0]=-160*65536;out->rp.start[1]=-112*65536;out->rp.start[2]=1024*65536;
    out->rp.line[1]=out->rp.dot[0]=65536;out->rp.scale[0]=out->rp.scale[1]=65536;
    const int64_t ka[3]={round_p2(h[2][2],1)*64+INT64_C(1073741824),round_p2(h[2][1],1)*64,round_p2(h[2][0],1)*64};
    if(ka[0]<0 || ka[0]>UINT32_MAX || magnitude(ka[1])>=33554432 || magnitude(ka[2])>=33554432)return fail(out,3);
    out->rp.coefficient_start=(uint32_t)ka[0];out->rp.coefficient_line=(int32_t)ka[1];out->rp.coefficient_dot=(int32_t)ka[2];
    out->index_min=32767;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        const int64_t k=ka[0]+ka[1]*(y?223:0)+ka[2]*(x?319:0);
        if(k<0 || k>=INT64_C(2147483648))return fail(out,3);
        const uint32_t index=(uint32_t)(k>>16);
        if(index<out->index_min)out->index_min=index;
        if(index>out->index_max)out->index_max=index;
    }
    return true;
}
#endif
static bool projected(const fzx_view_t *v,const int32_t in[4][3],int32_t xy[10][2],unsigned *count) {
    int32_t clipped[10][3];const int n=fzx_view_clip(v,in,4,clipped);
    if(n<0)return false;
    *count=(unsigned)n;
    for(int i=0;i<n;++i)if(!fzx_view_project(v,clipped[i],xy[i]))return false;
    return true;
}
#if FZX_RBG_LEAN
/* Band polygons: FZX_RBG_LEAN (default 0) clips only near/far in 3D and the
 * sides in 2D at the view rectangle (as the 3D side planes did); on any
 * failure it falls back to the exact path above. */
static bool projected_band(const fzx_view_t *v,const int32_t in[4][3],int32_t xy[10][2],unsigned *count) {
    const int n=fzx_view_project_guarded(v,in,0,xy);
    if(n>=0){*count=(unsigned)n;return true;}
    return projected(v,in,xy,count);
}
#define BAND_PROJECTED projected_band
#else
#define BAND_PROJECTED projected
#endif
#if FZX_RBG_LEAN && FZX_RBG_SHARE
/* FZX_RBG_SHARE (exact): adjacent fog bands share their boundary lines (band b's
 * high fraction is band b+1's low one). Each boundary's two points are built with
 * the same formula as the per-band corners, tested against the near and far planes
 * as fzx_view_project_guarded tests them, and projected once. A band whose four
 * corners are all inside goes straight to the 2D clip; any other band takes the
 * unchanged per-band path, so the output is identical. */
typedef struct { int32_t fraction; uint8_t half, ok; int32_t xy[2][2]; } band_edge_t;
static const band_edge_t *band_edge(band_edge_t *cache, unsigned *n, const fzx_view_t *view,
                                    const int32_t positions[8][3], unsigned half, int32_t fraction, unsigned inner_base) {
    for(unsigned k=0;k<*n;++k)if(cache[k].half==half && cache[k].fraction==fraction)return &cache[k];
    band_edge_t *e=&cache[(*n)++];e->half=(uint8_t)half;e->fraction=fraction;e->ok=1;
    for(unsigned side=0;side<2;++side) {
        const unsigned in_vertex=half?4+side:inner_base+side,edge=half?6+side:side;
        int32_t p[3];
#if ENG_DSP_BANDS
        fzx_band_point(0,p,positions,half,side,fraction);(void)edge;(void)in_vertex;
#else
        for(unsigned j=0;j<3;++j)p[j]=positions[in_vertex][j]+(int32_t)(((int64_t)positions[edge][j]-positions[in_vertex][j])*fraction/65536);
#endif
        if(fzx_view_plane(view,p,0)<0 || fzx_view_plane(view,p,1)<0 || !fzx_view_project(view,p,e->xy[side]))e->ok=0;
    }
    return e;
}
static bool band_shared(const fzx_view_t *view, const band_edge_t *lo, const band_edge_t *hi, int32_t xy[10][2], unsigned *count) {
    if(!lo->ok || !hi->ok)return false;
    const int32_t quad[4][2]={{lo->xy[0][0],lo->xy[0][1]},{lo->xy[1][0],lo->xy[1][1]},{hi->xy[1][0],hi->xy[1][1]},{hi->xy[0][0],hi->xy[0][1]}};
    const int n=fzx_view_guard_projected(view,quad,0,xy);
    if(n<0)return false;
    *count=(unsigned)n;return true;
}
#endif
static void window(const fzx_view_t *v,const int32_t xy[10][2],unsigned count,fzx_floor_rbg_t *out) {
    for(int y=0;y<224;++y) {
        out->window[y][0]=640;out->window[y][1]=0;
        if(y<v->clip[1] || y>=v->clip[3])continue;
        int64_t left=INT64_MAX,right=INT64_MIN;const int32_t line=y*65536+32768;
        for(unsigned i=0;i<count;++i) {
            const int32_t *a=xy[i],*b=xy[(i+1)%count];
            if((a[1]<=line && b[1]>line)||(b[1]<=line && a[1]>line)) {
                const int64_t x=a[0]+divide(((int64_t)b[0]-a[0])*(line-a[1]),b[1]-a[1]);
                if(x<left)left=x;
                if(x>right)right=x;
            }
        }
        if(left>right || right<32768)continue;
        int lo=(int)((left+32767)/65536),hi=(int)((right-32768)/65536);
        if(lo<v->clip[0])lo=v->clip[0];
        if(hi>=v->clip[2])hi=v->clip[2]-1;
        if(lo<=hi){out->window[y][0]=(uint16_t)(lo*2);out->window[y][1]=(uint16_t)(hi*2);++out->visible_lines;}
    }
}
static bool polygon(const int32_t xy[10][2],unsigned count,unsigned band,
                    fzx_floor_rbg_t *out,fzx_floor_emit_fn emit,void *context) {
    for(unsigned first=1;first+1<count;first+=2) {
        const unsigned ids[4]={0,first,first+1,first+2<count?first+2:count-1};
        fzx_floor_polygon_t q={.band=band};int64_t area=0;
        for(unsigned j=0;j<4;++j){q.x[j]=(int16_t)(xy[ids[j]][0]/65536);q.y[j]=(int16_t)(xy[ids[j]][1]/65536);}
        for(unsigned j=0;j<4;++j)area+=(int32_t)q.x[j]*q.y[(j+1)&3]-(int32_t)q.y[j]*q.x[(j+1)&3];
        if(!area)continue;
        if(out->commands>=64)return fail(out,4);
        if(!emit(context,&q))return fail(out,5);
        ++out->commands;
#if !FZX_RBG_LEAN
        for(unsigned j=0;j<4;++j){out->hash=(out->hash^(uint32_t)(int32_t)q.x[j])*16777619u;out->hash=(out->hash^(uint32_t)(int32_t)q.y[j])*16777619u;}
        out->hash=(out->hash^band)*16777619u;
#endif
    }
    return true;
}
bool fzx_floor_rbg_prepare(const fzx_view_t *view,const RefBackdrop *source,
                           fzx_floor_rbg_t *out,fzx_floor_emit_fn emit,void *context) {
    if(!out)return false;
    FZX_SCP_BEGIN(FZX_SCP_FLOOR);
    memset(out,0,sizeof(*out));out->hash=2166136261u;
    if(!view || !source || !emit || source->faults || source->count!=20 ||
       view->near_plane<=0 || view->far_plane<=view->near_plane ||
       view->focal[0]<65536 || view->focal[1]<65536 ||
       view->center[0]<-4096*65536 || view->center[0]>4096*65536 ||
       view->center[1]<-4096*65536 || view->center[1]>4096*65536 ||
       view->clip[0]<0 || view->clip[1]<0 || view->clip[2]>320 || view->clip[3]>224 ||
       view->clip[0]>=view->clip[2] || view->clip[1]>=view->clip[3])return fail(out,1);
    int32_t positions[8][3],outer[4][3],xy[10][2];unsigned count;
    FZX_SCP(TRANSFORM);
    for(unsigned i=0;i<8;++i) {
        const RefBackdropVertex *s=&source->vertices[i];int32_t p[3];
        if(s->position[1]!=source->vertices[0].position[1] || s->color[0]!=(i<2 || i>=6?255:0))return fail(out,1);
        for(unsigned j=0;j<3;++j)p[j]=(int32_t)s->position[j]*4096;
        if(!fzx_view_transform(view,p,positions[i]))return fail(out,2);
    }
#if ENG_DSP_BANDS
    {fzx_band_fractions_t bf;fzx_band_fractions_floor(&bf);fzx_band_begin(0,(const int32_t (*)[3])positions,&bf);}
#endif
    static const unsigned corners[4]={0,1,7,6};
    for(unsigned i=0;i<4;++i)memcpy(outer[i],positions[corners[i]],12);
    FZX_SCP(OUTER);
    if(!projected(view,(const int32_t (*)[3])outer,xy,&count))return fail(out,2);
    FZX_SCP(WINDOW);
#if FZX_RBG_FAST
    fzx_rbg_window(view,(const int32_t (*)[2])xy,count,out->window,&out->visible_lines);
#else
    window(view,(const int32_t (*)[2])xy,count,out);
#endif
    FZX_SCP(MAPPING);
#if FZX_RBG_DIV
    if(!mapping_div(view,source->vertices,(const int32_t (*)[3])positions,out))return false;
#else
    if(!mapping(view,source->vertices,(const int32_t (*)[3])positions,out))return false;
#endif
    FZX_SCP(BANDS);
#if FZX_RBG_LEAN && FZX_RBG_SHARE
    band_edge_t edges[16];unsigned edge_count=0;
#endif
#if FZX_RBG_NEAR0 && FZX_RBG_LEAN && FZX_RBG_SHARE
    /* FZX_RBG_NEAR0 (engine, exact): bands 1-7 of both halves overdraw the whole
     * quad from fraction 1/14 outwards, so band 0 is drawn only over the near
     * strip between the two halves' 1/14 lines (band 1's low edges, which the
     * band loop below reuses from the cache). */
    {
        const int32_t near=65536/14;
        const band_edge_t *a=band_edge(edges,&edge_count,view,(const int32_t (*)[3])positions,0,near,2);
        const band_edge_t *b=band_edge(edges,&edge_count,view,(const int32_t (*)[3])positions,1,near,2);
        if(!band_shared(view,a,b,xy,&count)) {
            static const uint8_t rail[4][2]={{0,0},{0,1},{1,1},{1,0}}; /* half, side: the outer quad's order */
            for(unsigned i=0;i<4;++i) {
                const unsigned half=rail[i][0],side=rail[i][1];
#if ENG_DSP_BANDS
                fzx_band_point(0,outer[i],(const int32_t (*)[3])positions,half,side,near);
#else
                const unsigned inner=half?4+side:2+side,edge=half?6+side:side;
                for(unsigned j=0;j<3;++j)outer[i][j]=positions[inner][j]+(int32_t)(((int64_t)positions[edge][j]-positions[inner][j])*near/65536);
#endif
            }
            if(!BAND_PROJECTED(view,(const int32_t (*)[3])outer,xy,&count))return fail(out,2);
        }
    }
#endif
    if(!polygon((const int32_t (*)[2])xy,count,0,out,emit,context))return false;
    for(unsigned half=0;half<2;++half)for(unsigned band=1;band<8;++band) {
        /* Thresholds are halfway between the same eight source fog levels as
         * the native tile atlas. Geometry stays on the authored two halves. */
        const int32_t low=(int32_t)(2*band-1)*65536/14,high=band==7?65536:(int32_t)(2*band+1)*65536/14;
        const int32_t fractions[4]={low,low,high,high};
        const unsigned sides[4]={0,1,1,0};
#if ENG_DSP_BANDS
        (void)sides;
#if !(FZX_RBG_LEAN && FZX_RBG_SHARE)
        fzx_band_corners(0,outer,(const int32_t (*)[3])positions,half,band,fractions);
#endif
#else
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<3;++j) {
            const unsigned inner=half?4+sides[i]:2+sides[i],edge=half?6+sides[i]:sides[i];
            outer[i][j]=positions[inner][j]+(int32_t)(((int64_t)positions[edge][j]-positions[inner][j])*fractions[i]/65536);
        }
#endif
#if FZX_RBG_LEAN && FZX_RBG_SHARE
        {
            const band_edge_t *lo=band_edge(edges,&edge_count,view,(const int32_t (*)[3])positions,half,low,2);
            const band_edge_t *hi=band_edge(edges,&edge_count,view,(const int32_t (*)[3])positions,half,high,2);
#if ENG_DSP_BANDS
            if(!band_shared(view,lo,hi,xy,&count)) {
                fzx_band_corners(0,outer,(const int32_t (*)[3])positions,half,band,fractions);
                if(!BAND_PROJECTED(view,(const int32_t (*)[3])outer,xy,&count))return fail(out,2);
            }
#else
            if(!band_shared(view,lo,hi,xy,&count) && !BAND_PROJECTED(view,(const int32_t (*)[3])outer,xy,&count))return fail(out,2);
#endif
        }
#else
        if(!BAND_PROJECTED(view,(const int32_t (*)[3])outer,xy,&count))return fail(out,2);
#endif
        if(!polygon((const int32_t (*)[2])xy,count,band,out,emit,context))return false;
    }
    FZX_SCP(HASH);
#if !FZX_RBG_LEAN
    for(unsigned i=0;i<sizeof(out->rp)/4;++i) {
        uint32_t word;memcpy(&word,(const unsigned char *)&out->rp+i*4,4);
        out->hash=(out->hash^word)*16777619u;
    }
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<2;++x)out->hash=(out->hash^out->window[y][x])*16777619u;
#endif
    FZX_SCP_END();
    return true;
}
