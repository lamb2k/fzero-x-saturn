#include "fzx_road_stream_draw.h"
#include "fzx_road_stream_phase.h"
#include <limits.h>
#include <string.h>
#define U16 fzx_road_u16
#define U32 fzx_road_u32

static int32_t s16(const uint8_t *p){return (int16_t)U16(p);}
/* Boot-only native layout (see the header). remap is its scratch: old row to
 * newest copy. A chunk may use copies made from the previous chunk's first
 * referenced index on; anything older (the wrap) is copied again. */
static bool native_prepare(fzx_road_stream_draw_work_t *w,const fzx_road_stream_pack_t *p){
    const uint32_t nv=p->count[FZX_RLD_VERTICES],nq=p->count[FZX_RLD_QUADS],nc=p->count[FZX_RLD_CHUNKS],ns=p->count[FZX_RLD_STRIPS];
    if(ns>256||nq>65535||nc>65535)return false;
    for(uint32_t s=0;s<ns;++s)w->nunit[s]=(uint16_t)(U16(fzx_road_stream_record(p,FZX_RLD_STRIPS,s)+4)*32u);
    memset(w->remap,0xff,nv*sizeof(*w->remap));
    uint32_t used=0,window=0;
    for(uint32_t c=0;c<nc;++c){
        const uint8_t *cr=fzx_road_stream_record(p,FZX_RLD_CHUNKS,c);
        const uint32_t first=U32(cr),number=U32(cr+4);uint32_t low=UINT32_MAX;
        if(first>nq||number>nq-first)return false;
        for(uint32_t i=first;i<first+number;++i){const uint8_t *q=fzx_road_stream_record(p,FZX_RLD_QUADS,i);
            for(unsigned j=0;j<4;++j){const unsigned id=U16(q+j*2);
                if(w->remap[id]==UINT16_MAX||w->remap[id]<window){
                    if(used>=w->npos_capacity||used>=65535u)return false;
                    const uint8_t *v=fzx_road_stream_record(p,FZX_RLD_VERTICES,id);
                    w->npos[used]=(sh2z_pos16_t){(int16_t)U16(v),(int16_t)U16(v+2),(int16_t)U16(v+4)};
                    w->remap[id]=(uint16_t)used++;
                }
                if(w->remap[id]<low)low=w->remap[id];
            }}
        if(!number)low=used;
        w->nchunks[c]=(fzx_road_stream_nchunk_t){(uint16_t)first,(uint16_t)number,(uint16_t)low,(uint16_t)used};
        for(uint32_t i=first;i<first+number;++i){const uint8_t *q=fzx_road_stream_record(p,FZX_RLD_QUADS,i);
            fzx_road_stream_nquad_t *o=w->nquads+i;const unsigned flags=U16(q+10),strip=U16(q+8);
            for(unsigned j=0;j<4;++j){const uint32_t local=w->remap[U16(q+j*2)]-low;if(local>255u)return false;o->v[j]=(uint8_t)local;}
            o->flags=(uint8_t)flags;o->reserved=0;o->order=0;o->strip=0;w->nspans[i]=(fzx_road_stream_nspan_t){0,0,0,0};
            if(flags&2u)continue;
            if(strip>=ns)return false;
            int32_t uv[4][2];
            for(unsigned j=0;j<4;++j){const uint8_t *v=fzx_road_stream_record(p,FZX_RLD_VERTICES,U16(q+j*2));uv[j][0]=s16(v+6);uv[j][1]=s16(v+8);}
            unsigned left=uv[0][0]<uv[2][0]?0u:3u,right=left==0?3u:0u;
            unsigned le=left==0?1u:2u,re=right==0?1u:2u;
            if(uv[left][1]>uv[le][1]){unsigned tmp=left;left=le;le=tmp;}
            if(uv[right][1]>uv[re][1]){unsigned tmp=right;right=re;re=tmp;}
            o->strip=(uint8_t)strip;o->order=(uint8_t)(left|right<<2|re<<4|le<<6);
            w->nspans[i]=(fzx_road_stream_nspan_t){(int16_t)uv[left][1],(int16_t)uv[le][1],(int16_t)uv[right][1],(int16_t)uv[re][1]};
        }
        window=low;
    }
    w->npos_count=used;return true;
}
bool fzx_road_stream_draw_prepare(fzx_road_stream_draw_work_t *w,
    const fzx_road_stream_pack_t *p,const void *textures,uint32_t bytes,uint32_t expected){
    if(!w)return false;
    w->prepared=false;w->fog_valid=false;w->native=false;w->npos_count=0;
    const bool native=w->npos||w->nquads||w->nspans||w->nchunks;
    const uint8_t *t=textures;
    if(!p||!p->data||!t||bytes!=p->texture_bytes||bytes<64||!expected||
       fzx_road_hash(t,bytes)!=expected||memcmp(t,"FZXRTX01",8)||U32(t+8)!=1||
       U32(t+12)!=bytes||!w->images||w->image_capacity<p->count[FZX_RLD_IMAGES]||
       !w->remap||w->remap_capacity<p->count[FZX_RLD_VERTICES]||
       (!native&&!w->positions)||!w->projected||!w->vertex_capacity||w->vertex_capacity>65535)return false;
    if(native&&(!w->npos||!w->nquads||!w->nspans||!w->nchunks||!native_prepare(w,p)))return false;
    for(uint32_t i=0;i<p->count[FZX_RLD_IMAGES];++i){
        const uint8_t *im=fzx_road_stream_record(p,FZX_RLD_IMAGES,i);
        fzx_road_stream_image_t *out=w->images+i;
        const unsigned mode=U16(im),width=U16(im+2),height=U16(im+4);
        for(unsigned v=0;v<3;++v)out->variant[v]=(fzx_road_stream_material_t){
            U32(im+8+v*4),U32(im+20+v*4),(uint16_t)width,(uint16_t)height,(uint16_t)mode,0};
        uint32_t sums[3]={0,0,0};
        for(unsigned pixel=0;pixel<width*height;++pixel){
            unsigned color;
            if(mode){const unsigned b=t[U32(im+8)+pixel/2],index=(pixel&1u)?b&15u:b>>4;
                color=U16(t+U32(im+20)+index*2);}
            else color=U16(t+U32(im+8)+pixel*2);
            for(unsigned c=0;c<3;++c)sums[c]+=(color>>(c*5))&31u;
        }
        for(unsigned c=0;c<3;++c)out->mean[c]=(uint8_t)((sums[c]+width*height/2)/(width*height));
        out->reserved=0;
    }
    w->pack=p;w->native=native;w->prepared=true;return true;
}

static void fog_prepare(fzx_road_stream_draw_work_t *w,const sh2z_view_t *view,
    const RefRoadPlan *plan,int32_t pa,int32_t pb,fzx_road_stream_draw_stats_t *stats){
    if(w->fog_valid&&w->fog_pa==pa&&w->fog_pb==pb&&w->fog_start==plan->fog_start&&
       w->fog_near==view->near_z&&w->fog_far==view->far_z)return;
    const int32_t fm=128000/(1000-plan->fog_start),fo=(500-plan->fog_start)*256/(1000-plan->fog_start);
    const uint64_t numerator=(uint64_t)(-(int64_t)pb)*UINT64_C(1073741824);
    w->fog_depth[0]=0;
    for(unsigned i=1;i<256;++i){
        /* fade >= i iff ratio >= ceil((i-fo)*2^30/fm). For pb<=0,
         * ratio = pa-floor(-pb*2^30/z_q12), hence z_q12>N/(M+1). */
        const int64_t n=((int64_t)i-fo)*INT64_C(1073741824);
        const int64_t required=n/fm+(n%fm>0);
        uint32_t depth=(uint32_t)view->far_z+1u;
        if(required<=-INT64_C(1073741824))depth=0;
        else if(required<=INT64_C(1073741824)){
            const int64_t m=(int64_t)pa-required;
            if(m>=0){const uint64_t q12=numerator/((uint64_t)m+1u)+1u;
                if(q12<=(uint32_t)view->far_z/4u)depth=(uint32_t)q12*4u;}
        }
        w->fog_depth[i]=depth;
    }
    /* STAND-IN pending an SH2ZAM tag: bucket index, as shade-1's
     * sh2z_shade_fog_from_depth builds it, kept inside the existing work. */
    unsigned monotonic=1,shift=0;
    for(unsigned i=1;i<256;++i)if(w->fog_depth[i]<w->fog_depth[i-1])monotonic=0;
    while((w->fog_depth[255]>>shift)>255u)++shift;
    {const uint32_t step=(uint32_t)1u<<shift;uint32_t z=0;unsigned i=0;
     for(unsigned b=0;b<256;++b,z+=step){while(i<255u&&w->fog_depth[i+1u]<=z)++i;w->fog_bucket[b]=(uint8_t)i;}}
    w->fog_bucket[256]=255;w->fog_shift=(uint8_t)(monotonic?shift:255u);
    w->fog_scale=shift?(uint32_t)1u<<(32u-shift):0u;
    w->fog_pa=pa;w->fog_pb=pb;w->fog_start=plan->fog_start;
    w->fog_near=view->near_z;w->fog_far=view->far_z;w->fog_valid=true;
    stats->fog_updates=1;
}
static unsigned fog_lookup(const fzx_road_stream_draw_work_t *w,int32_t z){
    const uint32_t depth=z>0?(uint32_t)z:0;
    if(w->fog_shift!=255u){
        /* STAND-IN for sh2z_shade_fog_lookup (shade-1), inlined per corner.
         * Bounded search inside one bucket; identical to the full search for
         * a non-decreasing table. depth>>shift as a multiply-high (the SH-2
         * has no variable shift). */
        const uint32_t b=w->fog_shift?(uint32_t)(((uint64_t)depth*w->fog_scale)>>32):depth;
        if(b>255u)return 255;
        unsigned lo=w->fog_bucket[b],hi=w->fog_bucket[b+1u];
        while(lo<hi){const unsigned middle=(lo+hi+1u)>>1;
            if(w->fog_depth[middle]<=depth)lo=middle;else hi=middle-1u;}
        return lo;
    }
    unsigned lo=0,hi=256;
    while(lo+1<hi){const unsigned middle=(lo+hi)/2;
        if(depth>=w->fog_depth[middle])lo=middle;else hi=middle;}
    return lo;
}
typedef struct {
    fzx_road_stream_draw_work_t *work;const RefRoadPlan *plan;sh2z_view_center_t view;
    fzx_road_stream_command_t *commands;uint32_t capacity;unsigned flags;
    fzx_road_stream_draw_stats_t *stats;
    int32_t live[3],base[3];   /* per-frame fog colours in RGB5 */
    const fzx_road_stream_emit_t *emit;   /* direct VDP1 target, or NULL for packets */
    fzx_road_stream_hot_t *hot;
} draw_t;
/* STAND-IN pending an SH2ZAM tag (owner direction: numeric work goes through
 * SH2ZAM). Exact replacements for divisions on the per-corner path (no SH-2
 * divide instruction; libgcc division dominated this stage). Both are checked
 * exhaustively over their whole reachable domain by the HOST tests. The
 * shade-1 batch forms (sh2z_shade_fade_offset/_mix) compute the same values. */
static int32_t div255(int32_t n){      /* trunc(n/255), |n| <= 65534 */
    const uint32_t a=(uint32_t)(n<0?-n:n),q=(a+(a>>8)+1u)>>8;
    return n<0?-(int32_t)q:(int32_t)q;
}
static uint32_t div65025(uint32_t n){  /* n/65025, n <= 2048287 */
    return (uint32_t)(((uint64_t)n*UINT32_C(2113633))>>37);
}
static bool fail(draw_t *d,unsigned fault){d->stats->fault=fault;return false;}
/* Inputs to area are clipped to +/-900, bounding the 16-corner sum in int32. */
static int32_t area(const sh2z_view_vertex_t *const *v,unsigned count){
    int32_t sum=0;for(unsigned i=0;i<count;++i){unsigned j=i+1<count?i+1:0;
        sum+=(int32_t)v[i]->sx*v[j]->sy-(int32_t)v[i]->sy*v[j]->sx;}
    return sum;
}
typedef const sh2z_view_vertex_t *vref_t;
static unsigned classify(const vref_t v[4],bool *safe){
    /* Preserve source order: original quad order for culling, or left-start,
     * right-start, right-end, left-end for textured pieces. Do not rotate the
     * polygon: the first clipped corner anchors the VDP1 texture fan. */
    unsigned all=63;*safe=true;
    for(unsigned i=0;i<4;++i){all&=v[i]->outcode;
        if((v[i]->outcode&SH2Z_OUT_NEAR)||v[i]->sx< -900||v[i]->sx>900||v[i]->sy< -900||v[i]->sy>900)*safe=false;}
    return all;
}
/* Corners as pointers: the unclipped quad itself, or the guard clip output.
 * Only a clip needs the low-RAM clip_in copy. */
static unsigned corners_of(fzx_road_stream_hot_t *h,const vref_t v[4],bool safe,
    int n,const sh2z_view_vertex_t **out){
    if(safe){for(unsigned i=0;i<4;++i)out[i]=v[i];return 4;}
    for(int i=0;i<n;++i)out[i]=&h->clip_out[i].v;
    return (unsigned)n;
}
static void clip_input(fzx_road_stream_hot_t *h,const vref_t v[4]){
    for(unsigned i=0;i<4;++i)h->clip_in[i].v=*v[i];
}
static bool piece(draw_t *d,const vref_t v[4],unsigned quad,unsigned piece_id,
    unsigned base_image,unsigned image,unsigned row,unsigned height,unsigned tier,bool solid){
    fzx_road_stream_draw_work_t *w=d->work;bool safe;
    FZX_RSP(QPIECE);FZX_RSP_COUNT(PIECES,1);
    if(classify(v,&safe)){++d->stats->culled;return true;}
    int n=4;
    if(!safe){const int16_t guard[4]={-900,-900,900,900};
        FZX_RSP(CLIP);FZX_RSP_COUNT(PIECE_CLIPS,1);clip_input(d->hot,v);
        n=sh2z_view_clip_guard_src_center_divu(d->hot->clip_out,&d->view,guard,d->hot->clip_in,4,0,10);
        FZX_RSP(QPIECE);if(n>0)FZX_RSP_COUNT(CLIP_CORNERS,n);
        if(n<0)return fail(d,FZX_RS_CLIP);
        if(n<3){++d->stats->culled;return true;}
        ++d->stats->clipped;
    }
    const sh2z_view_vertex_t *corners[SH2Z_VIEW_GUARD_MAX_OUT];
    const unsigned count=corners_of(d->hot,v,safe,n,corners);
    if(!area(corners,count)){++d->stats->culled;return true;}
    unsigned fade[16],middle=0;
    FZX_RSP(FOG);
    for(unsigned i=0;i<count;++i){fade[i]=fog_lookup(w,corners[i]->z);middle+=fade[i];}
    FZX_RSP(BUILD);
    /* variant = min(2,(floor(middle/count)+64)/128), compared instead of divided:
     * floor(m/c) >= 128v-64 <=> m >= (128v-64)*c. No quotient is formed. */
    const unsigned variant=middle>=192u*count?2u:middle>=64u*count?1u:0u;
    const int selected=variant==2?255:(int)variant*128;
    const fzx_road_stream_material_t neutral={0,0,0,0,0,0};
    fzx_road_stream_material_t material=solid?neutral:w->images[image].variant[variant];
    if(!solid){
        const uint32_t stride=material.mode?material.width/2u:material.width*2u;
        material.pixels+=row*stride;material.height=(uint16_t)height;
        if(material.pixels&7u){++d->stats->unaligned;
            if(!(d->flags&FZX_RS_AUDIT_UNALIGNED))return fail(d,FZX_RS_TEXTURE_ALIGNMENT);}
    }
    uint16_t shades[16];
    /* RGB1555 channel places as constants: the SH-2 has no variable shift
     * (a shift by c*5 is a libgcc call per channel). */
    static const uint16_t place[3]={1u,1u<<5,1u<<10};
    FZX_RSP(FOG);
    /* Channel-outer: per-piece channel terms are formed once, then each
     * corner costs one multiply and one stand-in division. */
    for(unsigned i=0;i<count;++i)shades[i]=0x8000;
    if(solid){
        for(unsigned c=0;c<3;++c){const uint32_t prim=d->plan->primitive[c],fog=d->plan->fog[c];
            for(unsigned i=0;i<count;++i){const uint32_t f=fade[i];
                const uint32_t color=div65025((prim*(255u-f)+fog*f)*31u+32512u);
                shades[i]|=(uint16_t)((color>31u?31u:color)*place[c]);
            }
        }
    } else {
        /* Per-frame live/base fog colours; one image mean per piece. */
        const uint8_t *means=w->images[base_image].mean;
        for(unsigned c=0;c<3;++c){const int32_t mean=means[c],live=d->live[c],base=d->base[c];
            const int32_t gain=live-mean,bias=selected*(base-mean);
            for(unsigned i=0;i<count;++i){int32_t color;
                color=16+div255((int32_t)fade[i]*gain-bias);
                if(color<0)color=0;
                if(color>31)color=31;
                shades[i]|=(uint16_t)((unsigned)color*place[c]);
            }
        }
    }
    FZX_RSP(BUILD);
    if(d->emit){
        /* Direct VDP1 emit: one template copy, then patch the texture fields
         * (once per piece), corners and Gouraud address per command. */
        const fzx_road_stream_emit_t *e=d->emit;
        const fzx_road_stream_vdp1_t *tmpl=e->templates+(solid?2u:material.mode);
        const uint16_t srca=(uint16_t)((e->texture_offset+material.pixels)>>3);
        const uint16_t size=(uint16_t)((((unsigned)material.width>>3)<<8|material.height)&0x3fffu);
        const uint16_t colr=material.mode&&!solid?(uint16_t)(((e->texture_offset+material.palette)&0x7ffffu)>>3):tmpl->colr;
        for(unsigned first=1;first+1<count;first+=2){
            const uint32_t at=d->stats->commands;
            if(at>=d->capacity)return fail(d,FZX_RS_CAPACITY);
            fzx_road_stream_vdp1_t *c=e->commands+at;uint16_t *g=e->shades+at*4u;
            const unsigned last=first+2<count?first+2:count-1;
            /* Field-wise template copy (a struct copy is a memcpy call). */
            c->ctrl=tmpl->ctrl;c->link=tmpl->link;c->pmod=tmpl->pmod;c->reserved=tmpl->reserved;
            if(solid){c->colr=tmpl->colr;c->srca=tmpl->srca;c->size=tmpl->size;}
            else {c->srca=srca;c->size=size;c->colr=colr;}
            c->xa=corners[0]->sx;c->ya=corners[0]->sy;c->xb=corners[first]->sx;c->yb=corners[first]->sy;
            c->xc=corners[first+1]->sx;c->yc=corners[first+1]->sy;c->xd=corners[last]->sx;c->yd=corners[last]->sy;
            g[0]=shades[0];g[1]=shades[first];g[2]=shades[first+1];g[3]=shades[last];
            c->grda=(uint16_t)(((e->shade_offset+at*8u)&0x7ffffu)>>3);
            d->stats->commands=at+1;
        }
        return true;
    }
    for(unsigned first=1;first+1<count;first+=2){
        if(d->stats->commands>=d->capacity)return fail(d,FZX_RS_CAPACITY);
        fzx_road_stream_command_t *out=d->commands+d->stats->commands;
        out->material=material;out->quad=(uint16_t)quad;out->piece=(uint16_t)piece_id;out->tier=(uint16_t)tier;out->solid=(uint16_t)solid;
        const unsigned order[4]={0,first,first+1,first+2<count?first+2:count-1};
        for(unsigned i=0;i<4;++i){out->x[i]=corners[order[i]]->sx;out->y[i]=corners[order[i]]->sy;out->shade[i]=shades[order[i]];}
        ++d->stats->commands;
    }
    return true;
}
static bool edge(draw_t *d,unsigned slot,const sh2z_view_vertex_t *from,const sh2z_view_vertex_t *to,
    int32_t low,int32_t high,unsigned count,unsigned step){
    fzx_road_stream_hot_t *h=d->hot;unsigned first=0,end=count;
    FZX_RSP_COUNT(EDGE_CALLS,1);
    while(first<count&&h->boundary[first]<=low)h->edge[slot][first++]=*from;
    while(end>first&&h->boundary[end-1]>=high)h->edge[slot][--end]=*to;
    if(end==first)return true;
    FZX_RSP_COUNT(STEPPED,end-first);
    return sh2z_view_edge_steps_center_divu(h->edge[slot]+first,&d->view,from,to,
        (uint16_t)(h->boundary[first]-low),(uint16_t)step,(uint16_t)(high-low),end-first);
}
/* Texture corner order and longitudinal spans of one quad (view-independent). */
typedef struct {unsigned left,right,re,le,strip,unit;int32_t l0,l1,r0,r1;} quad_tex_t;
static void legacy_tex(const fzx_road_stream_pack_t *p,const uint8_t *q,const unsigned rows[4],quad_tex_t *x){
    int32_t uv[4][2];
    for(unsigned i=0;i<4;++i){const uint8_t *v=fzx_road_stream_record(p,FZX_RLD_VERTICES,rows[i]);uv[i][0]=s16(v+6);uv[i][1]=s16(v+8);}
    unsigned left=uv[0][0]<uv[2][0]?0u:3u,right=left==0?3u:0u;
    unsigned le=left==0?1u:2u,re=right==0?1u:2u;
    if(uv[left][1]>uv[le][1]){unsigned tmp=left;left=le;le=tmp;}
    if(uv[right][1]>uv[re][1]){unsigned tmp=right;right=re;re=tmp;}
    x->left=left;x->right=right;x->re=re;x->le=le;
    x->l0=uv[left][1];x->l1=uv[le][1];x->r0=uv[right][1];x->r1=uv[re][1];
    x->strip=U16(q+8);
    const unsigned period=U16(fzx_road_stream_record(p,FZX_RLD_STRIPS,x->strip)+4),unit=period*32;x->unit=unit;
}
/* One out-of-line body for both front ends: inlined into each, its large
 * stepping/merge scratch would be duplicated in the caller's stack frame. */
static bool quad_body(draw_t *d,unsigned id,const vref_t vertices[4],unsigned flags,
    const fzx_road_stream_nquad_t *nq,const uint8_t *q,const unsigned rows[4]) __attribute__((noinline));
static bool quad(draw_t *d,unsigned id){
    fzx_road_stream_draw_work_t *w=d->work;const fzx_road_stream_pack_t *p=w->pack;
    FZX_RSP(QFETCH);
    const uint8_t *q=fzx_road_stream_record(p,FZX_RLD_QUADS,id);const unsigned flags=U16(q+10);
    if(!d->plan->outer&&(flags&1u))return true;
    ++d->stats->quads;d->stats->error_quad=id;
    vref_t vertices[4];unsigned rows[4];
    for(unsigned i=0;i<4;++i){const unsigned row=U16(q+i*2),mapped=w->remap[row];
        if(mapped>=d->stats->positions)return fail(d,FZX_RS_GATHER);
        vertices[i]=w->projected+mapped;rows[i]=row;}
    return quad_body(d,id,vertices,flags,NULL,q,rows);
}
/* Native front end: 8-byte record, chunk-local corners into the projected run. */
static bool native_quad(draw_t *d,unsigned id,const sh2z_view_vertex_t *base){
    const fzx_road_stream_nquad_t *nq=d->work->nquads+id;const unsigned flags=nq->flags;
    FZX_RSP(QFETCH);
    if(!d->plan->outer&&(flags&1u))return true;
    ++d->stats->quads;d->stats->error_quad=id;
    const vref_t vertices[4]={base+nq->v[0],base+nq->v[1],base+nq->v[2],base+nq->v[3]};
    return quad_body(d,id,vertices,flags,nq,NULL,NULL);
}
static bool quad_body(draw_t *d,unsigned id,const vref_t vertices[4],unsigned flags,
    const fzx_road_stream_nquad_t *nq,const uint8_t *q,const unsigned rows[4]){
    fzx_road_stream_draw_work_t *w=d->work;bool front=true,safe;
    for(unsigned i=0;i<4;++i)if(vertices[i]->outcode&SH2Z_OUT_NEAR)front=false;
    if(classify(vertices,&safe)){++d->stats->culled;return true;}
    FZX_RSP(QCULL);
    int n=4;
    if(!safe){
        FZX_RSP(CLIP);FZX_RSP_COUNT(QUAD_CLIPS,1);clip_input(d->hot,vertices);
        n=sh2z_view_clip_guard_src_center_divu(d->hot->clip_out,&d->view,d->view.view.clip,d->hot->clip_in,4,0,10);
        FZX_RSP(QCULL);if(n>0)FZX_RSP_COUNT(CLIP_CORNERS,n);
        if(n<0)return fail(d,FZX_RS_CLIP);
        if(n<3){++d->stats->culled;return true;}
    }
    {const sh2z_view_vertex_t *winding[SH2Z_VIEW_GUARD_MAX_OUT];
     if(area(winding,corners_of(d->hot,vertices,safe,n,winding))>=0){++d->stats->culled;return true;}}
    FZX_RSP(QUAD);
    if(flags&2u){const bool ok=piece(d,vertices,id,0,0,0,0,0,0,true);FZX_RSP(QPIECE);return ok;}
    /* Texture order and spans only for quads that survive culling. */
    quad_tex_t x;
    if(nq){const unsigned o=nq->order;const fzx_road_stream_nspan_t *s=w->nspans+id;
        x.left=o&3u;x.right=(o>>2)&3u;x.re=(o>>4)&3u;x.le=o>>6;
        x.l0=s->l0;x.l1=s->l1;x.r0=s->r0;x.r1=s->r1;x.strip=nq->strip;x.unit=w->nunit[nq->strip];}
    else legacy_tex(w->pack,q,rows,&x);
    const unsigned left=x.left,right=x.right,re=x.re,le=x.le,strip=x.strip,unit=x.unit,period=unit>>5;
    const int32_t l0=x.l0,l1=x.l1,r0=x.r0,r1=x.r1;
    const int32_t low=l0<r0?l0:r0,high=l1>r1?l1:r1,span=high-low;
    int32_t screen=0;
    if(front)for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j){
        int dx=vertices[i]->sx-vertices[j]->sx,dy=vertices[i]->sy-vertices[j]->sy;
        if(dx<0)dx=-dx;
        if(dy<0)dy=-dy;
        if(dx+dy>screen)screen=dx+dy;}
    unsigned tier=0,factor=1;
    if(front&&!(d->flags&FZX_RS_FORCE_FIRST)){
        const int64_t pixels=(int64_t)screen*unit;
        if(pixels<(int64_t)span*4){tier=3;factor=0;}
        else if(pixels<(int64_t)span*8){tier=2;factor=4;}
        else if(pixels<(int64_t)span*16){tier=1;factor=2;}
    }
    ++d->stats->tiers[tier];
    if(!factor){const vref_t whole[4]={vertices[left],vertices[right],vertices[re],vertices[le]};
        const bool ok=piece(d,whole,id,0,strip*5,strip*5+4,0,1,0,false);FZX_RSP(QPIECE);return ok;}
    /* Step at original repetition boundaries, then merge complete periods
     * only when the tagged planner bounds pixel displacement to one pixel.
     * Partial leading/trailing periods must keep their source phase. */
    unsigned count=1;d->hot->boundary[0]=low;
    /* unit is 256, 512 or 1024 (pack admission), so the Euclidean phase is a mask. */
    const int32_t phase_mask=(int32_t)unit-1;
    for(int32_t start=low;start<high;){const int32_t phase=start&phase_mask;
        int32_t end=start+(int32_t)unit-phase;if(end>high)end=high;
        if(count>=65)return fail(d,FZX_RS_STEP);
        d->hot->boundary[count++]=end;start=end;}
    FZX_RSP_COUNT(BOUNDARIES,count);FZX_RSP(EDGE);
    if(!edge(d,0,vertices[left],vertices[le],l0,l1,count,unit)||
       !edge(d,1,vertices[right],vertices[re],r0,r1,count,unit))return fail(d,FZX_RS_STEP);
    FZX_RSP(MERGE);FZX_RSP_COUNT(MERGES,1);
    /* Merge scratch lives with the stepping scratch, off the stack. */
    uint32_t *const end_masks=d->hot->end_masks;uint8_t *const runs=d->hot->runs;int run_count;
    if(factor==1){
        /* Run mask 0x2 admits only single repetitions: the plan is all ones. */
        for(unsigned j=0;j+1<count;++j)runs[j]=1;
        run_count=(int)count-1;
    } else {
        for(unsigned j=0;j<count;++j){
            end_masks[j]=0;
            for(unsigned run=2;run<=4;run*=2)
                if(run<=factor && j>=run && !(d->hot->boundary[j-run]&phase_mask) &&
                   d->hot->boundary[j]-d->hot->boundary[j-run]==(int32_t)(unit*run))end_masks[j]|=run==2?4u:16u;   /* 1<<run, run 2 or 4 */
            for(unsigned rail=0;rail<2;++rail){sh2z_view_vertex_t *v=&d->hot->edge[rail][j];
                if(v->sx< -900||v->sx>900||v->sy< -900||v->sy>900)v->outcode|=0x80;}
        }
        /* view-merge-2: the tier planner; equal-height whole periods only
         * (end_masks), so its plan is the pixel-mode plan of merge_runs. */
        const uint32_t mask=factor==4?0x16u:0x6u;
        run_count=sh2z_view_merge_tier(runs,d->hot->edge[0],d->hot->edge[1],end_masks,count,mask,256u);
    }
    FZX_RSP(QPIECE);
    if(run_count<1)return fail(d,FZX_RS_STEP);
    unsigned i=0;
    for(unsigned run_index=0;run_index<(unsigned)run_count;++run_index){
        const unsigned group=runs[run_index];
        const int32_t phase=d->hot->boundary[i]&phase_mask;
        const unsigned selected_tier=group==4?2u:group==2?1u:0u;
        const vref_t part[4]={&d->hot->edge[0][i],&d->hot->edge[1][i],&d->hot->edge[1][i+group],&d->hot->edge[0][i+group]};
        /* group is 1, 2 or 4 (mask 0x16 or narrower), so scale=32*group is a shift. */
        const unsigned scale=32*group;
        if(group!=1&&group!=2&&group!=4)return fail(d,FZX_RS_STEP);
        /* Constant shifts per group (the SH-2 has no variable shift). */
        #define SHR_GROUP(v) (group==4?(v)>>7:group==2?(v)>>6:(v)>>5)
        const unsigned row=SHR_GROUP((unsigned)phase);
        const unsigned height=SHR_GROUP((unsigned)phase+(unsigned)(d->hot->boundary[i+group]-d->hot->boundary[i])+scale-1)-row;
        #undef SHR_GROUP
        if(!height||row+height>period)return fail(d,FZX_RS_STEP);
        if(!piece(d,part,id,i,strip*5,strip*5+selected_tier,row,height,group,false))return false;
        FZX_RSP(QPIECE);i+=group;
    }
    return true;
}
/* Native frame: each group's chunks form at most two contiguous index runs
 * (the course wraps once); each run is one batch over its position range.
 * Chunks are then visited in the source order, as the pack path visits them. */
typedef struct {uint32_t first,last;int32_t offset;} run_t;
static bool project_run(draw_t *d,run_t *r,unsigned first,unsigned last){
    fzx_road_stream_draw_work_t *w=d->work;
    const uint32_t from=w->nchunks[first].vertex_first,to=w->nchunks[last].vertex_end,used=d->stats->positions;
    r->first=first;r->last=last;r->offset=(int32_t)used-(int32_t)from;
    if(to<from||to-from>w->vertex_capacity-used)return fail(d,FZX_RS_GATHER);
    if(to>from&&!sh2z_view_batch_center_divu(w->projected+used,&d->view,w->npos+from,sizeof(*w->npos),to-from))
        return fail(d,FZX_RS_PROJECT);
    d->stats->positions=used+(to-from);return true;
}
static bool native_frame(draw_t *d){
    fzx_road_stream_draw_work_t *w=d->work;const RefRoadPlan *plan=d->plan;
    const unsigned chunks=w->pack->count[FZX_RLD_CHUNKS];uint32_t visited_total=0;
    for(unsigned g=0;g<plan->count;++g){const RefRoadGroup *group=plan->groups+g;
        const unsigned start=(unsigned)group->start,end=(unsigned)group->end;
        if(start>=chunks||end>=chunks||(group->direction!=1&&group->direction!=-1))
            return fail(d,FZX_RS_GATHER);
        FZX_RSP(BATCH);
        run_t runs[2]={{0,0,0},{0,0,0}};
        if(start<end){if(!project_run(d,runs,start,end-1))return false;}
        else {if(!project_run(d,runs,start,chunks-1))return false;
            if(end&&!project_run(d,runs+1,0,end-1))return false;}
        FZX_RSP(QFETCH);
        unsigned index=group->direction==1?(end?end-1:chunks-1):start,visited=0;
        do {if(visited++==chunks)return fail(d,FZX_RS_INVALID);++d->stats->chunks;++visited_total;
            const fzx_road_stream_nchunk_t *c=w->nchunks+index;
            const run_t *r=index>=runs[0].first&&index<=runs[0].last?runs:runs+1;
            const sh2z_view_vertex_t *base=w->projected+(r->offset+(int32_t)c->vertex_first);
            for(unsigned i=c->quad_first,e=i+c->quad_count;i<e;++i)if(!native_quad(d,i,base))return false;
            if(group->direction==1){if(index==start)break;index=index?index-1:chunks-1;}
            else {index=index+1<chunks?index+1:0;if(index==end)break;}
        }while(true);
    }
    if(visited_total!=plan->visible_chunks)return fail(d,FZX_RS_GATHER);
    return true;
}
static bool draw_any(fzx_road_stream_draw_work_t *w,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,unsigned flags,
    fzx_road_stream_command_t *commands,const fzx_road_stream_emit_t *emit,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *stats){
    if(count)*count=0;
    if(!stats)return false;
    *stats=(fzx_road_stream_draw_stats_t){0};stats->fault=FZX_RS_INVALID;
    if(!count||!w||!w->prepared||!w->pack||!view||!eye||!plan||(!commands&&!emit)||!capacity||
       (flags&~(unsigned)(FZX_RS_FORCE_FIRST|FZX_RS_AUDIT_UNALIGNED))||plan->faults||
       plan->count>REF_ROAD_GROUPS||plan->outer>1||plan->fog_start<0||plan->fog_start>996||pb>0||
       view->clip[0]<-900||view->clip[1]<-900||view->clip[2]>901||view->clip[3]>901)return false;
    draw_t d={.work=w,.plan=plan,.commands=commands,.capacity=capacity,.flags=flags,.stats=stats,.emit=emit,
        .hot=w->hot?w->hot:&w->local};
    for(unsigned c=0;c<3;++c){d.live[c]=(plan->fog[c]*31+127)/255;d.base[c]=(w->pack->base_fog[c]*31+127)/255;}
    FZX_RSP(EYE);
    if(!fzx_view16_from_source_center(&d.view,view,eye))return false;
    if(w->native){
        FZX_RSP(FOG);
        fog_prepare(w,&d.view.view,plan,pa,pb,stats);
        if(!native_frame(&d))return false;
        FZX_RSP(REPORT);
        stats->fault=0;stats->error_quad=0;*count=stats->commands;return true;
    }
    FZX_RSP(GATHER);
    if(!fzx_road_stream_gather(w->pack,plan,w->remap,w->remap_capacity,w->positions,w->vertex_capacity,&stats->positions))return fail(&d,FZX_RS_GATHER);
    FZX_RSP(BATCH);
    if(!sh2z_view_batch_center_divu(w->projected,&d.view,w->positions,sizeof(*w->positions),stats->positions))return fail(&d,FZX_RS_PROJECT);
    FZX_RSP(FOG);
    fog_prepare(w,&d.view.view,plan,pa,pb,stats);
    FZX_RSP(QFETCH);
    const unsigned chunks=w->pack->count[FZX_RLD_CHUNKS];
    for(unsigned g=0;g<plan->count;++g){const RefRoadGroup *group=plan->groups+g;
        unsigned index=group->direction==1?(group->end+chunks-1)%chunks:group->start,visited=0;
        do {if(visited++==chunks)return fail(&d,FZX_RS_INVALID);++stats->chunks;
            const uint8_t *c=fzx_road_stream_record(w->pack,FZX_RLD_CHUNKS,index);
            const unsigned first=U32(c),number=U32(c+4);
            for(unsigned i=first;i<first+number;++i)if(!quad(&d,i))return false;
            if(group->direction==1){if(index==group->start)break;index=(index+chunks-1)%chunks;}
            else {index=(index+1)%chunks;if(index==group->end)break;}
        }while(true);
    }
    FZX_RSP(REPORT);
    stats->fault=0;stats->error_quad=0;*count=stats->commands;return true;
}
bool fzx_road_stream_draw(fzx_road_stream_draw_work_t *w,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,unsigned flags,
    fzx_road_stream_command_t *commands,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *stats){
    if(!commands){if(count)*count=0;return false;}
    return draw_any(w,view,eye,plan,pa,pb,flags,commands,NULL,capacity,count,stats);
}
bool fzx_road_stream_draw_vdp1(fzx_road_stream_draw_work_t *w,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,unsigned flags,
    const fzx_road_stream_emit_t *emit,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *stats){
    if(count)*count=0;
    if(!emit||!emit->commands||!emit->shades||(flags&FZX_RS_AUDIT_UNALIGNED)||
       emit->texture_offset>0x7ffffu||(emit->texture_offset&7u)||(emit->shade_offset&7u)||
       capacity>(0x80000u-emit->shade_offset)/8u)return false;
    return draw_any(w,view,eye,plan,pa,pb,flags,NULL,emit,capacity,count,stats);
}
