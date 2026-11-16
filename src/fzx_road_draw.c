#include "fzx_road_draw.h"
#include <limits.h>
#include <string.h>

enum { ATTRIBUTES=6, MAX_VERTICES=10 };
/* u/v Q24.8, source shade RGB/fog UNORM16. */
typedef struct { int32_t p[3],a[ATTRIBUTES]; } vertex_t;
typedef struct { int32_t x,y,inv,a[ATTRIBUTES]; } projected_t;
typedef struct {
    const fzx_road_pack_t *pack;
    const fzx_view_t *view;
    const RefRoadPlan *plan;
    uint16_t *atlas;
    uint32_t bytes,capacity;
    int32_t projection_a,projection_b;
    fzx_road_chart_fn emit;
    void *context;
    fzx_road_draw_stats_t *stats;
    uint32_t material_index;
    fzx_road_material_t material;
} draw_t;

static int32_t signed16(const uint8_t *p)
{
    const uint32_t n=fzx_road_u16(p);
    return n<32768u?(int32_t)n:(int32_t)n-65536;
}
static uint64_t fraction(int64_t a,int64_t b)
{
    uint64_t n=a<0?0u-(uint64_t)a:(uint64_t)a;
    const uint64_t other=b<0?0u-(uint64_t)b:(uint64_t)b;
    if(!other)return UINT64_C(4294967296);
    const uint64_t d=n+other;
    uint64_t q=0;
    for(unsigned i=0;i<32;++i){
        q<<=1;
        if(n>=d-n){n-=d-n;q|=1u;}else n+=n;
    }
    return q;
}
static int32_t lerp(int32_t a,int32_t b,uint64_t t)
{
    const int64_t delta=(int64_t)b-a;
    const uint64_t magnitude=(uint64_t)(delta<0?-delta:delta);
    const int64_t step=(int64_t)((magnitude*t)>>32);
    return (int32_t)((int64_t)a+(delta<0?-step:step));
}
static int clip(const fzx_view_t *view,const vertex_t input[3],vertex_t output[MAX_VERTICES])
{
    vertex_t buffers[2][MAX_VERTICES];
    memcpy(buffers[0],input,3*sizeof(vertex_t));
    unsigned source=0,count=3;
    for(unsigned plane=0;plane<6 && count>=3;++plane){
        unsigned written=0;
        const vertex_t *previous=&buffers[source][count-1];
        int64_t dp=fzx_view_plane(view,previous->p,plane);
        for(unsigned i=0;i<count;++i){
            const vertex_t *current=&buffers[source][i];
            const int64_t dc=fzx_view_plane(view,current->p,plane);
            if((dp>=0)!=(dc>=0)){
                if(written==MAX_VERTICES)return -1;
                vertex_t *q=&buffers[1-source][written++];
                const uint64_t t=fraction(dp,dc);
                for(unsigned k=0;k<3;++k)q->p[k]=lerp(previous->p[k],current->p[k],t);
                for(unsigned k=0;k<ATTRIBUTES;++k)q->a[k]=lerp(previous->a[k],current->a[k],t);
                /* Near/far snapping prevents a one-unit roundoff from making
                 * projection undefined. Final raster bounds enforce scissor. */
                if(q->p[2]<view->near_plane)q->p[2]=view->near_plane;
                if(q->p[2]>view->far_plane)q->p[2]=view->far_plane;
            }
            if(dc>=0){if(written==MAX_VERTICES)return -1;buffers[1-source][written++]=*current;}
            previous=current;dp=dc;
        }
        count=written;source=1-source;
    }
    if(count<3)return 0;
    memcpy(output,buffers[source],count*sizeof(vertex_t));
    return (int)count;
}

static int32_t fog_alpha(const draw_t *d,int32_t depth)
{
    const int32_t denominator=1000-d->plan->fog_start;
    const int32_t fm=128000/denominator;
    const int32_t fo=(500-d->plan->fog_start)*256/denominator;
    int64_t ratio=-(INT64_C(1)<<30);
    if(depth>0){
        ratio=d->projection_a+(int64_t)d->projection_b*(INT64_C(1)<<30)/depth;
        if(ratio<-(INT64_C(1)<<30))ratio=-(INT64_C(1)<<30);
        if(ratio>(INT64_C(1)<<30))ratio=INT64_C(1)<<30;
    }
    const int64_t alpha=ratio*fm+(int64_t)fo*(INT64_C(1)<<30);
    if(alpha<=0)return 0;
    if(alpha>=255*(INT64_C(1)<<30))return 65535;
    return (int32_t)(alpha/(INT64_C(1)<<30))*257;
}
static int64_t edge(const projected_t *a,const projected_t *b,int32_t x,int32_t y)
{
    return ((int64_t)b->x-a->x)*(y-a->y)-((int64_t)b->y-a->y)*(x-a->x);
}
static bool top_left(const projected_t *a,const projected_t *b)
{
    return b->y<a->y || (b->y==a->y && b->x>a->x);
}
static bool project(draw_t *d,const vertex_t *in,projected_t *out,int32_t minimum_depth)
{
    int32_t xy[2];
    if(!fzx_view_project(d->view,in->p,xy))return false;
    out->x=xy[0]/4096;out->y=xy[1]/4096; /* Q12.4 pixel coverage */
    if(out->x < -16 || out->x > 5136 || out->y < -16 || out->y > 3600)return false;
    /* Per-polygon reciprocal normalization retains precision even on distant
     * road. Products/planes below deliberately remain signed 64-bit. */
    out->inv=(int32_t)((int64_t)minimum_depth*65536/in->p[2]);
    if(out->inv<1)return false;
    for(unsigned i=0;i<ATTRIBUTES;++i)out->a[i]=in->a[i];
    return true;
}
static bool plane_step(const projected_t p[3],unsigned attribute,
                       const int64_t weights[3],int64_t area,int64_t *out)
{
    int64_t value=0;
    for(unsigned i=0;i<3;++i){
        const int64_t a=attribute==0?p[i].inv:(int64_t)p[i].inv*p[i].a[attribute-1];
        value+=a*weights[i];
    }
    /* Q8 limits drift, but skinny triangles may extrapolate far outside the
     * attribute range over their bounding box. Reject unrepresentable planes. */
    const int64_t whole=value/area;
    if(whole>(INT64_MAX-255)/256 || whole<(INT64_MIN+255)/256)return false;
    *out=whole*256+(value%area)*256/area;
    return true;
}
static bool plane_span_valid(int64_t base,int64_t dx,int64_t dy,uint32_t width,uint32_t height)
{
    const uint64_t a=base<0?0u-(uint64_t)base:(uint64_t)base;
    const uint64_t x=dx<0?0u-(uint64_t)dx:(uint64_t)dx;
    const uint64_t y=dy<0?0u-(uint64_t)dy:(uint64_t)dy;
    if(a>(uint64_t)INT64_MAX)return false;
    uint64_t remaining=(uint64_t)INT64_MAX-a;
    if(x>remaining/width)return false;
    remaining-=x*width;
    return y<=remaining/height;
}

static bool chart(draw_t *d,const vertex_t poly[MAX_VERTICES],unsigned count,
                   uint16_t triangle,uint16_t material)
{
    projected_t q[MAX_VERTICES];
    int32_t left=320*16,top=224*16,right=0,bottom=0;
    int32_t minimum_depth=poly[0].p[2];
    for(unsigned i=1;i<count;++i)if(poly[i].p[2]<minimum_depth)minimum_depth=poly[i].p[2];
    for(unsigned i=0;i<count;++i){
        if(!project(d,&poly[i],&q[i],minimum_depth))return false;
        if(q[i].x<left)left=q[i].x;
        if(q[i].x>right)right=q[i].x;
        if(q[i].y<top)top=q[i].y;
        if(q[i].y>bottom)bottom=q[i].y;
    }
    /* Keep the source winding test, before reversing coverage edges. */
    int64_t signed_area=0;
    unsigned pivot=0;
    for(unsigned i=1;i+1<count;++i){
        const int64_t a=edge(&q[0],&q[i],q[i+1].x,q[i+1].y);
        if(a<signed_area || a>0){pivot=i;signed_area=a;}
    }
    if(!pivot || signed_area>=0){++d->stats->culled;return true;}
    projected_t p[3]={q[0],q[pivot+1],q[pivot]};
    const int64_t area=-signed_area;
    left=(left+7)/16;top=(top+7)/16;right=(right-8)/16;bottom=(bottom-8)/16;
    if(left<d->view->clip[0])left=d->view->clip[0];
    if(top<d->view->clip[1])top=d->view->clip[1];
    if(right>=d->view->clip[2])right=d->view->clip[2]-1;
    if(bottom>=d->view->clip[3])bottom=d->view->clip[3]-1;
    if(right<left || bottom<top)return true;
    const uint32_t width=((uint32_t)(right-left+1)+7u)&~7u,height=(uint32_t)(bottom-top+1);
    const uint32_t bytes=width*height*2u;
    if(width>504 || height>255 || d->stats->charts==d->capacity ||
       bytes>d->bytes-d->stats->bytes){d->stats->fault=2;return false;}
    uint16_t *dest=d->atlas+d->stats->bytes/2;
    /* Clockwise source polygons become positive inside half-spaces by
     * traversing their boundary in reverse, preserving the source diagonal. */
    int64_t coverage[MAX_VERTICES],cx[MAX_VERTICES],cy[MAX_VERTICES];
    bool inclusive[MAX_VERTICES];
    for(unsigned i=0;i<count;++i){
        const projected_t *a=&q[(i+1)%count],*b=&q[i];
        coverage[i]=edge(a,b,left*16+8,top*16+8);
        cx[i]=-((int64_t)b->y-a->y)*16;cy[i]=((int64_t)b->x-a->x)*16;
        /* A clip-plane endpoint or Q4 rounding can duplicate a vertex.
         * Its zero edge imposes no coverage constraint. */
        inclusive[i]=(a->x==b->x && a->y==b->y) || top_left(a,b);
    }
    int64_t w[3],dx[3],dy[3],row[ATTRIBUTES+1],sx[ATTRIBUTES+1],sy[ATTRIBUTES+1];
    for(unsigned i=0;i<3;++i){
        const projected_t *a=&p[(i+1)%3],*b=&p[(i+2)%3];
        w[i]=edge(a,b,left*16+8,top*16+8);
        dx[i]=-((int64_t)b->y-a->y)*16;dy[i]=((int64_t)b->x-a->x)*16;
    }
    for(unsigned i=0;i<=ATTRIBUTES;++i){
        if(!plane_step(p,i,w,area,&row[i]) || !plane_step(p,i,dx,area,&sx[i]) ||
           !plane_step(p,i,dy,area,&sy[i]) ||
           !plane_span_valid(row[i],sx[i],sy[i],width,height))return false;
    }
    for(uint32_t y=0;y<height;++y){
        int64_t at[MAX_VERTICES],values[ATTRIBUTES+1];
        memcpy(at,coverage,count*sizeof(int64_t));memcpy(values,row,sizeof(values));
        for(uint32_t x=0;x<width;++x){
            bool inside=x<=(uint32_t)(right-left);
            for(unsigned i=0;i<count && inside;++i)inside=at[i]>0 || (at[i]==0 && inclusive[i]);
            uint16_t pixel=0;
            ++d->stats->tested_pixels;
            if(inside){
                if(values[0]<=0)return false;
                int32_t a[ATTRIBUTES];
                for(unsigned i=0;i<ATTRIBUTES;++i){
                    const int64_t value=values[i+1]/values[0];
                    if(value<INT32_MIN || value>INT32_MAX)return false;
                    a[i]=(int32_t)value;
                }
                for(unsigned i=2;i<ATTRIBUTES;++i){if(a[i]<0)a[i]=0;if(a[i]>65535)a[i]=65535;}
                const fzx_material_color_t shade={(uint16_t)a[2],(uint16_t)a[3],(uint16_t)a[4],(uint16_t)a[5]};
                pixel=fzx_road_material_pixel(&d->material,a[0],a[1],shade,d->plan->primitive,d->plan->fog);
                ++d->stats->shaded_pixels;
            }
            dest[y*width+x]=pixel;
            for(unsigned i=0;i<count;++i)at[i]+=cx[i];
            for(unsigned i=0;i<=ATTRIBUTES;++i)values[i]+=sx[i];
        }
        for(unsigned i=0;i<count;++i)coverage[i]+=cy[i];
        for(unsigned i=0;i<=ATTRIBUTES;++i)row[i]+=sy[i];
    }
    const fzx_road_chart_t record={(int16_t)left,(int16_t)top,(uint16_t)width,(uint16_t)height,
                                  triangle,material,d->stats->bytes};
    if(!d->emit(d->context,&record)){d->stats->fault=3;return false;}
    d->stats->bytes+=bytes;++d->stats->charts;
    return true;
}

static bool chunk(draw_t *d,uint32_t index)
{
    const uint8_t *record=fzx_road_record(d->pack,FZX_ROAD_CHUNK,index);
    if(!record)return false;
    ++d->stats->chunks;
    const uint32_t first=fzx_road_u32(record+8),end=first+fzx_road_u32(record+12);
    for(uint32_t t=first;t<end;++t){
        const uint8_t *tri=fzx_road_record(d->pack,FZX_ROAD_TRIANGLE,t);
        if(!tri)return false;
        if(!d->plan->outer && (fzx_road_u16(tri+10)&FZX_ROAD_OUTER))continue;
        ++d->stats->triangles;
        const uint16_t material=fzx_road_u16(tri+6);
        if(material!=d->material_index){
            if(!fzx_road_material_open(&d->material,d->pack,material))return false;
            d->material_index=material;
        }
        vertex_t input[3],poly[MAX_VERTICES];
        for(unsigned i=0;i<3;++i){
            const uint8_t *v=fzx_road_record(d->pack,FZX_ROAD_VERTEX,fzx_road_u16(tri+i*2));
            if(!v)return false;
            const int32_t world[3]={signed16(v)*4096,signed16(v+2)*4096,signed16(v+4)*4096};
            if(!fzx_view_transform(d->view,world,input[i].p))return false;
            input[i].a[0]=signed16(v+6)*8;input[i].a[1]=signed16(v+8)*8;
            for(unsigned k=0;k<3;++k)input[i].a[k+2]=v[k+10]*257;
            input[i].a[5]=fog_alpha(d,input[i].p[2]);
        }
        const int count=clip(d->view,input,poly);
        if(count<0)return false;
        if(count==0){++d->stats->culled;continue;}
        if(!chart(d,poly,(unsigned)count,(uint16_t)t,material))return false;
    }
    return true;
}

bool fzx_road_draw(const fzx_road_pack_t *pack,const fzx_view_t *view,const RefRoadPlan *plan,
    int32_t projection_a,int32_t projection_b,uint16_t *atlas,uint32_t atlas_bytes,
    uint32_t chart_capacity,fzx_road_chart_fn emit,void *context,fzx_road_draw_stats_t *stats)
{
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));stats->fault=1;
    if(!pack || !pack->data || !view || !plan || !atlas || !emit || (atlas_bytes&7u) ||
       !chart_capacity || plan->faults || plan->count>REF_ROAD_GROUPS || plan->outer>1 ||
       plan->fog_start<0 || plan->fog_start>996 || view->near_plane<=0 ||
       view->far_plane<=view->near_plane || view->far_plane/view->near_plane>65535 ||
       view->clip[0]<0 || view->clip[1]<0 || view->clip[2]>320 || view->clip[3]>224 ||
       view->clip[0]>=view->clip[2] || view->clip[1]>=view->clip[3])return false;
    draw_t d={.pack=pack,.view=view,.plan=plan,.atlas=atlas,.bytes=atlas_bytes,
              .capacity=chart_capacity,.projection_a=projection_a,.projection_b=projection_b,
              .emit=emit,.context=context,.stats=stats,.material_index=UINT32_MAX};
    const uint32_t chunks=pack->count[FZX_ROAD_CHUNK];
    for(unsigned g=0;g<plan->count;++g){
        const RefRoadGroup *group=&plan->groups[g];
        if(group->start>=chunks || group->end>=chunks ||
           (group->direction!=1 && group->direction!=-1))return false;
        uint32_t index=group->direction==1?(group->end+chunks-1)%chunks:group->start,visited=0;
        do {
            if(visited++==chunks || !chunk(&d,index))return false;
            if(group->direction==1){
                if(index==group->start)break;
                index=(index+chunks-1)%chunks;
            }else{
                index=(index+1)%chunks;
                if(index==group->end)break;
            }
        }while(true);
    }
    if(stats->chunks!=plan->visible_chunks)return false;
    stats->fault=0;return true;
}
