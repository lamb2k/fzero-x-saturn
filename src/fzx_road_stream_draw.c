#include "fzx_road_stream_draw.h"
#include "fzx_road_stream_phase.h"
#include <limits.h>
#include <string.h>
#define U16 fzx_road_u16
#define U32 fzx_road_u32

static int32_t s16(const uint8_t *p){return (int16_t)U16(p);}
bool fzx_road_stream_draw_prepare(fzx_road_stream_draw_work_t *w,
    const fzx_road_stream_pack_t *p,const void *textures,uint32_t bytes,uint32_t expected){
    if(!w)return false;
    w->prepared=false;w->fog_valid=false;
    const uint8_t *t=textures;
    if(!p||!p->data||!t||bytes!=p->texture_bytes||bytes<64||!expected||
       fzx_road_hash(t,bytes)!=expected||memcmp(t,"FZXRTX01",8)||U32(t+8)!=1||
       U32(t+12)!=bytes||!w->images||w->image_capacity<p->count[FZX_RLD_IMAGES]||
       !w->remap||w->remap_capacity<p->count[FZX_RLD_VERTICES]||
       !w->positions||!w->projected||!w->vertex_capacity||w->vertex_capacity>65535)return false;
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
    w->pack=p;w->prepared=true;return true;
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
    /* Bucket index, as shade-1's sh2z_shade_fog_from_depth builds it. */
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
        /* Bounded search inside one bucket; identical to the full search for
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
} draw_t;
/* Exact replacements for divisions on the per-corner path (no SH-2 divide
 * instruction; libgcc division dominated this stage). Both are checked
 * exhaustively over their whole reachable domain by the HOST tests. */
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
static unsigned classify(const sh2z_view_vertex_t v[4],bool *safe){
    /* Preserve source order: original quad order for culling, or left-start,
     * right-start, right-end, left-end for textured pieces. Do not rotate the
     * polygon: the first clipped corner anchors the VDP1 texture fan. */
    unsigned all=63;*safe=true;
    for(unsigned i=0;i<4;++i){all&=v[i].outcode;
        if((v[i].outcode&SH2Z_OUT_NEAR)||v[i].sx< -900||v[i].sx>900||v[i].sy< -900||v[i].sy>900)*safe=false;}
    return all;
}
/* Corners as pointers: the unclipped quad itself, or the guard clip output.
 * Only a clip needs the low-RAM clip_in copy. */
static unsigned corners_of(fzx_road_stream_draw_work_t *w,const sh2z_view_vertex_t v[4],bool safe,
    int n,const sh2z_view_vertex_t **out){
    if(safe){for(unsigned i=0;i<4;++i)out[i]=v+i;return 4;}
    for(int i=0;i<n;++i)out[i]=&w->clip_out[i].v;
    return (unsigned)n;
}
static void clip_input(fzx_road_stream_draw_work_t *w,const sh2z_view_vertex_t v[4]){
    for(unsigned i=0;i<4;++i)w->clip_in[i].v=v[i];
}
static bool piece(draw_t *d,const sh2z_view_vertex_t v[4],unsigned quad,unsigned piece_id,
    unsigned base_image,unsigned image,unsigned row,unsigned height,unsigned tier,bool solid){
    fzx_road_stream_draw_work_t *w=d->work;bool safe;
    FZX_RSP(QUAD);FZX_RSP_COUNT(PIECES,1);
    if(classify(v,&safe)){++d->stats->culled;return true;}
    int n=4;
    if(!safe){const int16_t guard[4]={-900,-900,900,900};
        FZX_RSP(CLIP);FZX_RSP_COUNT(PIECE_CLIPS,1);clip_input(w,v);
        n=sh2z_view_clip_guard_src_center_divu(w->clip_out,&d->view,guard,w->clip_in,4,0,10);
        FZX_RSP(QUAD);if(n>0)FZX_RSP_COUNT(CLIP_CORNERS,n);
        if(n<0)return fail(d,FZX_RS_CLIP);
        if(n<3){++d->stats->culled;return true;}
        ++d->stats->clipped;
    }
    const sh2z_view_vertex_t *corners[SH2Z_VIEW_GUARD_MAX_OUT];
    const unsigned count=corners_of(w,v,safe,n,corners);
    if(!area(corners,count)){++d->stats->culled;return true;}
    unsigned fade[16],middle=0;
    FZX_RSP(FOG);
    for(unsigned i=0;i<count;++i){fade[i]=fog_lookup(w,corners[i]->z);middle+=fade[i];}
    FZX_RSP(BUILD);
    middle/=count;unsigned variant=(middle+64)/128;if(variant>2)variant=2;
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
    FZX_RSP(FOG);
    if(solid){
        for(unsigned i=0;i<count;++i){uint16_t packed=0x8000;const uint32_t f=fade[i];
            for(unsigned c=0;c<3;++c){
                const uint32_t color=div65025((d->plan->primitive[c]*(255u-f)+d->plan->fog[c]*f)*31u+32512u);
                packed|=(uint16_t)((color>31u?31u:color)<<(c*5));
            }
            shades[i]=packed;
        }
    } else {
        /* live-mean and selected*(base-mean) are constant over the piece. */
        const uint8_t *mean=w->images[base_image].mean;int32_t gain[3],offset[3];
        for(unsigned c=0;c<3;++c){gain[c]=d->live[c]-mean[c];offset[c]=selected*(d->base[c]-mean[c]);}
        for(unsigned i=0;i<count;++i){uint16_t packed=0x8000;const int32_t f=(int32_t)fade[i];
            for(unsigned c=0;c<3;++c){int32_t color=16+div255(f*gain[c]-offset[c]);
                if(color<0)color=0;
                if(color>31)color=31;
                packed|=(uint16_t)((unsigned)color<<(c*5));
            }
            shades[i]=packed;
        }
    }
    FZX_RSP(BUILD);
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
    fzx_road_stream_draw_work_t *w=d->work;unsigned first=0,end=count;
    FZX_RSP_COUNT(EDGE_CALLS,1);
    while(first<count&&w->boundary[first]<=low)w->edge[slot][first++]=*from;
    while(end>first&&w->boundary[end-1]>=high)w->edge[slot][--end]=*to;
    if(end==first)return true;
    FZX_RSP_COUNT(STEPPED,end-first);
    return sh2z_view_edge_steps_center_divu(w->edge[slot]+first,&d->view,from,to,
        (uint16_t)(w->boundary[first]-low),(uint16_t)step,(uint16_t)(high-low),end-first);
}
static bool quad(draw_t *d,unsigned id){
    fzx_road_stream_draw_work_t *w=d->work;const fzx_road_stream_pack_t *p=w->pack;
    const uint8_t *q=fzx_road_stream_record(p,FZX_RLD_QUADS,id);const unsigned flags=U16(q+10);
    if(!d->plan->outer&&(flags&1u))return true;
    ++d->stats->quads;d->stats->error_quad=id;
    sh2z_view_vertex_t vertices[4];unsigned rows[4];int32_t uv[4][2];bool front=true,safe;
    for(unsigned i=0;i<4;++i){const unsigned row=U16(q+i*2),mapped=w->remap[row];
        if(mapped>=d->stats->positions)return fail(d,FZX_RS_GATHER);
        vertices[i]=w->projected[mapped];if(vertices[i].outcode&SH2Z_OUT_NEAR)front=false;rows[i]=row;}
    if(classify(vertices,&safe)){++d->stats->culled;return true;}
    int n=4;
    if(!safe){
        FZX_RSP(CLIP);FZX_RSP_COUNT(QUAD_CLIPS,1);clip_input(w,vertices);
        n=sh2z_view_clip_guard_src_center_divu(w->clip_out,&d->view,d->view.view.clip,w->clip_in,4,0,10);
        FZX_RSP(QUAD);if(n>0)FZX_RSP_COUNT(CLIP_CORNERS,n);
        if(n<0)return fail(d,FZX_RS_CLIP);
        if(n<3){++d->stats->culled;return true;}
    }
    {const sh2z_view_vertex_t *winding[SH2Z_VIEW_GUARD_MAX_OUT];
     if(area(winding,corners_of(w,vertices,safe,n,winding))>=0){++d->stats->culled;return true;}}
    if(flags&2u){const bool ok=piece(d,vertices,id,0,0,0,0,0,0,true);FZX_RSP(QUAD);return ok;}
    /* Texture coordinates only for quads that survive culling. */
    for(unsigned i=0;i<4;++i){const uint8_t *v=fzx_road_stream_record(p,FZX_RLD_VERTICES,rows[i]);uv[i][0]=s16(v+6);uv[i][1]=s16(v+8);}
    unsigned left=uv[0][0]<uv[2][0]?0u:3u,right=left==0?3u:0u;
    unsigned le=left==0?1u:2u,re=right==0?1u:2u;
    if(uv[left][1]>uv[le][1]){unsigned tmp=left;left=le;le=tmp;}
    if(uv[right][1]>uv[re][1]){unsigned tmp=right;right=re;re=tmp;}
    const int32_t l0=uv[left][1],l1=uv[le][1],r0=uv[right][1],r1=uv[re][1];
    const int32_t low=l0<r0?l0:r0,high=l1>r1?l1:r1,span=high-low;
    const unsigned strip=U16(q+8);const uint8_t *s=fzx_road_stream_record(p,FZX_RLD_STRIPS,strip);
    const unsigned period=U16(s+4);int32_t screen=0;
    if(front)for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j){
        int dx=vertices[i].sx-vertices[j].sx,dy=vertices[i].sy-vertices[j].sy;
        if(dx<0)dx=-dx;
        if(dy<0)dy=-dy;
        if(dx+dy>screen)screen=dx+dy;}
    unsigned tier=0,factor=1;
    if(front&&!(d->flags&FZX_RS_FORCE_FIRST)){
        const int64_t pixels=(int64_t)screen*period*32;
        if(pixels<(int64_t)span*4){tier=3;factor=0;}
        else if(pixels<(int64_t)span*8){tier=2;factor=4;}
        else if(pixels<(int64_t)span*16){tier=1;factor=2;}
    }
    ++d->stats->tiers[tier];
    if(!factor){const sh2z_view_vertex_t whole[4]={vertices[left],vertices[right],vertices[re],vertices[le]};
        const bool ok=piece(d,whole,id,0,strip*5,strip*5+4,0,1,0,false);FZX_RSP(QUAD);return ok;}
    /* Step at original repetition boundaries, then merge complete periods
     * only when the tagged planner bounds pixel displacement to one pixel.
     * Partial leading/trailing periods must keep their source phase. */
    const unsigned unit=period*32;unsigned count=1;w->boundary[0]=low;
    /* unit is 256, 512 or 1024 (pack admission), so the Euclidean phase is a mask. */
    const int32_t phase_mask=(int32_t)unit-1;
    for(int32_t start=low;start<high;){const int32_t phase=start&phase_mask;
        int32_t end=start+(int32_t)unit-phase;if(end>high)end=high;
        if(count>=65)return fail(d,FZX_RS_STEP);
        w->boundary[count++]=end;start=end;}
    FZX_RSP_COUNT(BOUNDARIES,count);FZX_RSP(EDGE);
    if(!edge(d,0,vertices+left,vertices+le,l0,l1,count,unit)||
       !edge(d,1,vertices+right,vertices+re,r0,r1,count,unit))return fail(d,FZX_RS_STEP);
    FZX_RSP(MERGE);FZX_RSP_COUNT(MERGES,1);
    uint32_t merge_rows[65],end_masks[65];uint8_t runs[64];
    for(unsigned j=0;j<count;++j){
        merge_rows[j]=(uint32_t)(w->boundary[j]-low);end_masks[j]=0;
        for(unsigned run=2;run<=4;run*=2)
            if(run<=factor && j>=run && !(w->boundary[j-run]&phase_mask) &&
               w->boundary[j]-w->boundary[j-run]==(int32_t)(unit*run))end_masks[j]|=1u<<run;
        for(unsigned rail=0;rail<2;++rail){sh2z_view_vertex_t *v=&w->edge[rail][j];
            if(v->sx< -900||v->sx>900||v->sy< -900||v->sy>900)v->outcode|=0x80;}
    }
    const uint32_t mask=factor==4?0x16u:factor==2?0x6u:0x2u;
    const int run_count=sh2z_view_merge_runs(runs,w->edge[0],w->edge[1],merge_rows,end_masks,
        count,mask,256u,SH2Z_VIEW_MERGE_PIXELS);
    FZX_RSP(QUAD);
    if(run_count<1)return fail(d,FZX_RS_STEP);
    unsigned i=0;
    for(unsigned run_index=0;run_index<(unsigned)run_count;++run_index){
        const unsigned group=runs[run_index];
        const int32_t phase=w->boundary[i]&phase_mask;
        const unsigned selected_tier=group==4?2u:group==2?1u:0u;
        const sh2z_view_vertex_t part[4]={w->edge[0][i],w->edge[1][i],w->edge[1][i+group],w->edge[0][i+group]};
        /* group is 1, 2 or 4 (mask 0x16 or narrower), so scale=32*group is a shift. */
        const unsigned scale=32*group,shift=group==4?7u:group==2?6u:5u;
        if(scale!=1u<<shift)return fail(d,FZX_RS_STEP);
        const unsigned row=(unsigned)phase>>shift;
        const unsigned height=(((unsigned)phase+(unsigned)(w->boundary[i+group]-w->boundary[i])+scale-1)>>shift)-row;
        if(!height||row+height>period)return fail(d,FZX_RS_STEP);
        if(!piece(d,part,id,i,strip*5,strip*5+selected_tier,row,height,group,false))return false;
        FZX_RSP(QUAD);i+=group;
    }
    return true;
}
bool fzx_road_stream_draw(fzx_road_stream_draw_work_t *w,const fzx_view_t *view,
    const int32_t eye[3],const RefRoadPlan *plan,int32_t pa,int32_t pb,unsigned flags,
    fzx_road_stream_command_t *commands,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *stats){
    if(count)*count=0;
    if(!stats)return false;
    *stats=(fzx_road_stream_draw_stats_t){0};stats->fault=FZX_RS_INVALID;
    if(!count||!w||!w->prepared||!w->pack||!view||!eye||!plan||!commands||!capacity||
       (flags&~(unsigned)(FZX_RS_FORCE_FIRST|FZX_RS_AUDIT_UNALIGNED))||plan->faults||
       plan->count>REF_ROAD_GROUPS||plan->outer>1||plan->fog_start<0||plan->fog_start>996||pb>0||
       view->clip[0]<-900||view->clip[1]<-900||view->clip[2]>901||view->clip[3]>901)return false;
    draw_t d={.work=w,.plan=plan,.commands=commands,.capacity=capacity,.flags=flags,.stats=stats};
    for(unsigned c=0;c<3;++c){d.live[c]=(plan->fog[c]*31+127)/255;d.base[c]=(w->pack->base_fog[c]*31+127)/255;}
    FZX_RSP(EYE);
    if(!fzx_view16_from_source_center(&d.view,view,eye))return false;
    FZX_RSP(GATHER);
    if(!fzx_road_stream_gather(w->pack,plan,w->remap,w->remap_capacity,w->positions,w->vertex_capacity,&stats->positions))return fail(&d,FZX_RS_GATHER);
    FZX_RSP(BATCH);
    if(!sh2z_view_batch_center_divu(w->projected,&d.view,w->positions,sizeof(*w->positions),stats->positions))return fail(&d,FZX_RS_PROJECT);
    FZX_RSP(FOG);
    fog_prepare(w,&d.view.view,plan,pa,pb,stats);
    FZX_RSP(QUAD);
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
