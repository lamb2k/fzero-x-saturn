#include "fzx_road_tiles.h"
#include "fzx_road_profile.h"
#include <limits.h>
#include <string.h>
#ifndef FZX_ROAD_TILE_PROJECT_CULL
#define FZX_ROAD_TILE_PROJECT_CULL 0
#endif
#ifndef FZX_ROAD_TILE_EDGE_REUSE
#define FZX_ROAD_TILE_EDGE_REUSE 0
#endif
#define U16 fzx_road_u16
#define U32 fzx_road_u32
static int32_t s16(const uint8_t *p){return (int16_t)U16(p);}
bool fzx_road_tiles_open(fzx_road_tiles_t *out,const fzx_road_pack_t *road,const void *data,
                         uint32_t bytes,uint32_t textures,uint32_t hash) {
    if(!out)return false;
    memset(out,0,sizeof(*out));const uint8_t *p=data;
    if(!road || !road->data || !p || bytes<64 || memcmp(p,"FZXRTL01",8) ||
       U32(p+8)!=bytes || U32(p+12)!=road->hash || U32(p+20)!=textures ||
       fzx_road_hash(p,bytes)!=hash)return false;
    fzx_road_tiles_t r={road,p,bytes,textures,U32(p+24),U32(p+28),U32(p+32),U32(p+36),U32(p+40),U32(p+44)};
    if(r.chunks!=road->count[FZX_ROAD_CHUNK] || r.quads*2!=road->count[FZX_ROAD_TRIANGLE] ||
       !r.strips || r.strips>256 || r.chunk_at!=64 || r.quad_at!=64+r.chunks*8 ||
       r.strip_at!=r.quad_at+r.quads*16 || bytes!=r.strip_at+r.strips*32)return false;
    for(unsigned i=48;i<64;++i)if(p[i])return false;
    uint32_t at=0;
    for(unsigned c=0;c<r.chunks;++c) {
        const uint8_t *chunk=p+r.chunk_at+c*8;
        const uint8_t *original=fzx_road_record(road,FZX_ROAD_CHUNK,c);
        if(U32(chunk)!=at || U32(chunk+4)*2!=U32(original+12) || at*2!=U32(original+8))return false;
        at+=U32(chunk+4);if(at>r.quads)return false;
    }
    if(at!=r.quads)return false;
    for(unsigned i=0;i<r.quads;++i) {
        const uint8_t *q=p+r.quad_at+i*16;const uint8_t *a=fzx_road_record(road,FZX_ROAD_TRIANGLE,i*2);
        const uint8_t *b=fzx_road_record(road,FZX_ROAD_TRIANGLE,i*2+1);
        if(U16(q+12)!=i*2 || U16(q+14)!=U16(a+8) || U16(a+8)!=U16(b+8) ||
           U16(a+6)!=U16(b+6) || (U16(q+10)&~3u) || (U16(q+10)&1u)!=U16(a+10) || U16(a+10)!=U16(b+10))return false;
        for(unsigned j=0;j<4;++j) {
            unsigned id=U16(q+j*2);bool found=false;
            if(id>=road->count[FZX_ROAD_VERTEX])return false;
            for(unsigned k=0;k<3;++k)if(id==U16(a+k*2) || id==U16(b+k*2))found=true;
            if(!found)return false;
            for(unsigned k=0;k<j;++k)if(id==U16(q+k*2))return false;
        }
        if(U16(q+10)&2u){if(U16(q+8)!=65535)return false;}
        else if(U16(q+8)>=r.strips || U16(p+r.strip_at+U16(q+8)*32+26)!=U16(a+6))return false;
    }
    at=0;
    for(unsigned i=0;i<r.strips;++i) {
        const uint8_t *s=p+r.strip_at+i*32;unsigned w=U16(s+16),h=U16(s+18);
        if(!w || (w&7) || w>504 || !h || h>64 || U32(s)!=at || U32(s+4)!=w*(h+1)*2 ||
           U32(s+8)!=at+w*h*2 || U32(s+12) || U32(s+28) || U32(s+4)*3>textures-at)return false;
        for(unsigned j=0;j<3;++j)if(U16(s+20+j*2)>31)return false;
        at+=U32(s+4)*3;
    }
    if(at!=textures)return false;
    *out=r;return true;
}
static int64_t divide(int64_t n,int32_t d) {
#if defined(__sh__)
    if(n>=(int64_t)INT32_MIN*d && n<=(int64_t)INT32_MAX*d) {
        volatile uint32_t *divisor=(volatile uint32_t *)0xffffff00u,*hi=(volatile uint32_t *)0xffffff10u,*lo=(volatile uint32_t *)0xffffff14u;
        *divisor=(uint32_t)d;*hi=(uint32_t)((uint64_t)n>>32);*lo=(uint32_t)n;return (int32_t)*lo;
    }
#endif
    return n/d;
}
static bool project(const fzx_view_t *v,const int32_t p[3],int32_t xy[2]) {
    if(p[2]<=0)return false;
    int64_t x=v->center[0]+divide((int64_t)v->focal[0]*p[0],p[2]);
    int64_t y=v->center[1]-divide((int64_t)v->focal[1]*p[1],p[2]);
    if(x<INT32_MIN || x>INT32_MAX || y<INT32_MIN || y>INT32_MAX)return false;
    xy[0]=(int32_t)x;xy[1]=(int32_t)y;return true;
}
#if FZX_ROAD_TILE_PROJECT_CULL
/* Projection already divided by positive Z. A one-unit Q16.16 guard keeps
 * truncation from rejecting a vertex on an exact view-plane boundary. If
 * projection cannot be represented, use the original side-plane tests. */
static unsigned projected_outcode(const fzx_view_t *v,const int32_t p[3],const int32_t xy[2],bool valid) {
    unsigned code=(p[2]<v->near_plane?1u:0u)|(p[2]>v->far_plane?2u:0u);
    if(valid) {
        if((int64_t)xy[0]<(int64_t)v->clip[0]*65536-1)code|=4u;
        if((int64_t)xy[0]>(int64_t)v->clip[2]*65536+1)code|=8u;
        if((int64_t)xy[1]<(int64_t)v->clip[1]*65536-1)code|=16u;
        if((int64_t)xy[1]>(int64_t)v->clip[3]*65536+1)code|=32u;
    } else {
        for(unsigned k=2;k<6;++k)if(RP_CALL(FZX_RP_CULL,fzx_view_plane(v,p,k))<0)code|=1u<<k;
    }
    return code;
}
#endif
typedef struct {
    const fzx_road_tiles_t *tiles;const fzx_view_t *view;const RefRoadPlan *plan;
    int32_t pa,pb,fm,fo;uint32_t capacity;fzx_road_tile_emit_fn emit;void *context;fzx_road_tile_stats_t *stats;
} draw_t;
static bool fault(draw_t *d,unsigned code){d->stats->fault=code;return false;}
static int32_t fog(draw_t *d,int32_t z) {
    int64_t ratio=-(INT64_C(1)<<30);
    if(z>0)ratio=d->pa+divide((int64_t)d->pb*(INT64_C(1)<<30),z);
    if(ratio<-(INT64_C(1)<<30))ratio=-(INT64_C(1)<<30);
    if(ratio>(INT64_C(1)<<30))ratio=INT64_C(1)<<30;
    int64_t a=ratio*d->fm+(int64_t)d->fo*(INT64_C(1)<<30);
    return a<=0?0:a>=255*(INT64_C(1)<<30)?255:(int32_t)(a/(INT64_C(1)<<30));
}
static int64_t area(const int32_t xy[][2],unsigned count) {
    int64_t a=0;
    for(unsigned i=0;i<count;++i) {
        unsigned j=(i+1)%count;
        a+=(int64_t)(xy[i][0]/65536)*(xy[j][1]/65536)-(int64_t)(xy[i][1]/65536)*(xy[j][0]/65536);
    }
    return a;
}
static bool submit(draw_t *d,fzx_road_tile_quad_t *q,const int32_t xy[][2],const uint16_t *shades,unsigned n) {
    if(!RP_CALL(FZX_RP_CULL,area(xy,n))){++d->stats->culled;return true;}
    for(unsigned first=1;first+1<n;first+=2) {
        unsigned indices[4]={0,first,first+1,first+2<n?first+2:n-1};
        for(unsigned i=0;i<4;++i){q->x[i]=(int16_t)(xy[indices[i]][0]/65536);q->y[i]=(int16_t)(xy[indices[i]][1]/65536);q->shade[i]=shades[indices[i]];}
        if(d->stats->draws>=d->capacity)return fault(d,2);
        if(!RP_CALL(FZX_RP_EMIT,d->emit(d->context,q)))return fault(d,3);
        RP_BEGIN(FZX_RP_HASH,hash_phase);
        uint32_t h=d->stats->hash;
        for(unsigned i=0;i<4;++i){h=(h^(uint32_t)(int32_t)q->x[i])*16777619u;h=(h^(uint32_t)(int32_t)q->y[i])*16777619u;h=(h^q->shade[i])*16777619u;}
        h=(h^q->offset)*16777619u;h=(h^q->width)*16777619u;h=(h^q->height)*16777619u;h=(h^q->solid)*16777619u;
        d->stats->hash=h;RP_END(hash_phase);++d->stats->draws;
    }
    return true;
}
static bool piece(draw_t *d,const int32_t v[][3],const uint8_t *strip,bool primitive,unsigned row,unsigned height,bool mean) {
    int32_t xy[10][2],clipped[10][3];const int32_t (*vertices)[3]=v;bool safe=true;unsigned out=63;
    for(unsigned i=0;i<4;++i) {
#if FZX_ROAD_TILE_PROJECT_CULL
        bool projected=RP_CALL(FZX_RP_PROJECT,project(d->view,v[i],xy[i]));out&=RP_CALL(FZX_RP_CULL,projected_outcode(d->view,v[i],xy[i],projected));
        if(!projected ||
#else
        unsigned o=0;for(unsigned k=0;k<6;++k)if(RP_CALL(FZX_RP_CULL,fzx_view_plane(d->view,v[i],k))<0)o|=1u<<k;out&=o;
        if(!RP_CALL(FZX_RP_PROJECT,project(d->view,v[i],xy[i])) ||
#endif
           v[i][2]<d->view->near_plane || xy[i][0]/65536 < -900 || xy[i][0]/65536>900 || xy[i][1]/65536 < -900 || xy[i][1]/65536>900)safe=false;
    }
    if(out){++d->stats->culled;return true;}
    unsigned n=4;
    if(!safe) {
        fzx_view_t guard=*d->view;guard.clip[0]=guard.clip[1]=-900;guard.clip[2]=guard.clip[3]=900;
        int count=RP_CALL(FZX_RP_CLIP,fzx_view_clip(&guard,v,4,clipped));
        if(count<0)return fault(d,4);
        if(count<3){++d->stats->culled;return true;}
        n=(unsigned)count;vertices=(const int32_t (*)[3])clipped;
        for(unsigned i=0;i<n;++i)if(!RP_CALL(FZX_RP_PROJECT,project(&guard,clipped[i],xy[i])))return fault(d,4);
        ++d->stats->clipped;
    }
    RP_BEGIN(FZX_RP_MATERIAL,material_phase);
    int fades[10],middle=0;for(unsigned i=0;i<n;++i){fades[i]=fog(d,vertices[i][2]);middle+=fades[i];}middle/=(int)n;
    unsigned variant=(unsigned)(middle+64)/128;if(variant>2)variant=2;
    fzx_road_tile_quad_t q={0};q.solid=primitive;
    if(!primitive) {
        q.width=U16(strip+16);q.height=(uint16_t)height;
        q.offset=(mean?U32(strip+8):U32(strip)+row*q.width*2)+variant*U32(strip+4);
    }
    const int selected=variant==2?255:(int)variant*128;
    uint16_t shades[10];
    for(unsigned i=0;i<n;++i) {
        shades[i]=0x8000;
        for(unsigned c=0;c<3;++c) {
            int f=(d->plan->fog[c]*31+127)/255,native;
            if(primitive)native=((d->plan->primitive[c]*(255-fades[i])+d->plan->fog[c]*fades[i])*31+32512)/65025;
            else native=16+(fades[i]-selected)*(f-(int)U16(strip+20+c*2))/255;
            if(native<0)native=0;
            if(native>31)native=31;
            shades[i]|=(uint16_t)(native<<(c*5));
        }
    }
    RP_END(material_phase);
    return submit(d,&q,(const int32_t (*)[2])xy,shades,n);
}
static int32_t lerp(int32_t a,int32_t b,int32_t n,int32_t den) {
    if(n<=0)return a;
    if(n>=den)return b;
    return a+(int32_t)divide(((int64_t)b-a)*n,den);
}
static bool quad(draw_t *d,const uint8_t *record) {
    if(!d->plan->outer && (U16(record+10)&1u))return true;
    ++d->stats->quads;int32_t v[4][3],uv[4][2],xy[10][2];unsigned out=63;bool front=true;
    for(unsigned i=0;i<4;++i) {
        const uint8_t *p=fzx_road_record(d->tiles->road,FZX_ROAD_VERTEX,U16(record+i*2));
        int32_t world[3];for(unsigned k=0;k<3;++k)world[k]=s16(p+k*2)*4096;
        if(!RP_CALL(FZX_RP_TRANSFORM,fzx_view_transform(d->view,world,v[i])))return fault(d,1);
        uv[i][0]=s16(p+6);uv[i][1]=s16(p+8);
#if FZX_ROAD_TILE_PROJECT_CULL
        bool projected=RP_CALL(FZX_RP_PROJECT,project(d->view,v[i],xy[i]));out&=RP_CALL(FZX_RP_CULL,projected_outcode(d->view,v[i],xy[i],projected));
        if(!projected ||
#else
        unsigned o=0;for(unsigned k=0;k<6;++k)if(RP_CALL(FZX_RP_CULL,fzx_view_plane(d->view,v[i],k))<0)o|=1u<<k;out&=o;
        if(!RP_CALL(FZX_RP_PROJECT,project(d->view,v[i],xy[i])) ||
#endif
           v[i][2]<d->view->near_plane)front=false;
    }
    if(out){++d->stats->culled;return true;}
    if(!front) {
        int32_t clipped[10][3];int n=RP_CALL(FZX_RP_CLIP,fzx_view_clip(d->view,(const int32_t (*)[3])v,4,clipped));
        if(n<0)return fault(d,4);
        if(n<3){++d->stats->culled;return true;}
        for(int i=0;i<n;++i)if(!RP_CALL(FZX_RP_PROJECT,project(d->view,clipped[i],xy[i])))return fault(d,4);
        if(RP_CALL(FZX_RP_CULL,area((const int32_t (*)[2])xy,(unsigned)n))>=0){++d->stats->culled;return true;}
    } else if(RP_CALL(FZX_RP_CULL,area((const int32_t (*)[2])xy,4))>=0){++d->stats->culled;return true;}
    if(U16(record+10)&2u)return piece(d,(const int32_t (*)[3])v,NULL,true,0,0,false);
    const uint8_t *strip=d->tiles->data+d->tiles->strip_at+U16(record+8)*32;
    /* Arrange both source S edges from low to high T. Source winding was
     * checked above; VDP1 texture corner order is independent of culling. */
    unsigned left=uv[0][0]<uv[2][0]?0:3,right=left==0?3:0;
    unsigned le=left==0?1:2,re=right==0?1:2;
    if(uv[left][1]>uv[le][1]){unsigned t=left;left=le;le=t;}
    if(uv[right][1]>uv[re][1]){unsigned t=right;right=re;re=t;}
    int32_t l0=uv[left][1],l1=uv[le][1],r0=uv[right][1],r1=uv[re][1];
    if(l1<=l0 || r1<=r0)return fault(d,1);
    int32_t lo=l0<r0?l0:r0,hi=l1>r1?l1:r1;
    unsigned period=U16(strip+18);int32_t span=hi-lo;
    int32_t screen_span=0;
    if(front) {
        for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j) {
            int32_t dx=xy[i][0]/65536-xy[j][0]/65536,dy=xy[i][1]/65536-xy[j][1]/65536;
            if(dx<0)dx=-dx;
            if(dy<0)dy=-dy;
            if(dx+dy>screen_span)screen_span=dx+dy;
        }
    }
    if(front && (int64_t)screen_span*period*32 < (int64_t)span*3) {
        int32_t whole[4][3];unsigned order[4]={left,right,re,le};
        for(unsigned i=0;i<4;++i)memcpy(whole[i],v[order[i]],sizeof(whole[i]));
        ++d->stats->mean;return piece(d,(const int32_t (*)[3])whole,strip,false,0,1,true);
    }
    const int32_t unit=(int32_t)period*32;
#if FZX_ROAD_TILE_EDGE_REUSE
    int32_t previous[2][3];
#endif
    for(int32_t start=lo;start<hi;) {
        int32_t phase=start%unit;if(phase<0)phase+=unit;
        int32_t end=start+(unit-phase);if(end>hi)end=hi;
        int32_t part[4][3];
        for(unsigned k=0;k<3;++k) {
#if FZX_ROAD_TILE_EDGE_REUSE
            if(start!=lo){part[0][k]=previous[0][k];part[1][k]=previous[1][k];}
            else
#endif
            {
                part[0][k]=RP_CALL(FZX_RP_SPLIT,lerp(v[left][k],v[le][k],start-l0,l1-l0));
                part[1][k]=RP_CALL(FZX_RP_SPLIT,lerp(v[right][k],v[re][k],start-r0,r1-r0));
            }
            part[2][k]=RP_CALL(FZX_RP_SPLIT,lerp(v[right][k],v[re][k],end-r0,r1-r0));
            part[3][k]=RP_CALL(FZX_RP_SPLIT,lerp(v[left][k],v[le][k],end-l0,l1-l0));
        }
        unsigned row=(unsigned)phase/32,height=((unsigned)phase+(unsigned)(end-start)+31)/32-row;
        if(!height || row+height>period)return fault(d,1);
        if(!piece(d,(const int32_t (*)[3])part,strip,false,row,height,false))return false;
#if FZX_ROAD_TILE_EDGE_REUSE
        memcpy(previous[0],part[3],sizeof(previous[0]));
        memcpy(previous[1],part[2],sizeof(previous[1]));
#endif
        start=end;
    }
    return true;
}
static bool chunk(draw_t *d,unsigned index) {
    const uint8_t *c=d->tiles->data+d->tiles->chunk_at+index*8;unsigned first=U32(c),end=first+U32(c+4);
    ++d->stats->chunks;
    for(unsigned i=first;i<end;++i)if(!quad(d,d->tiles->data+d->tiles->quad_at+i*16))return false;
    return true;
}
bool fzx_road_tiles_draw(const fzx_road_tiles_t *tiles,const fzx_view_t *view,const RefRoadPlan *plan,
                         int32_t pa,int32_t pb,uint32_t capacity,fzx_road_tile_emit_fn emit,void *context,fzx_road_tile_stats_t *stats) {
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));stats->hash=2166136261u;stats->fault=1;
    if(!tiles || !tiles->data || !view || !plan || !emit || !capacity || plan->faults ||
       plan->count>REF_ROAD_GROUPS || plan->outer>1 || plan->fog_start<0 || plan->fog_start>996 || view->near_plane<=0)return false;
    draw_t d={tiles,view,plan,pa,pb,128000/(1000-plan->fog_start),(500-plan->fog_start)*256/(1000-plan->fog_start),capacity,emit,context,stats};
    unsigned chunks=tiles->chunks;
    for(unsigned g=0;g<plan->count;++g) {
        const RefRoadGroup *p=plan->groups+g;
        if(p->start>=chunks || p->end>=chunks || (p->direction!=1 && p->direction!=-1))return false;
        unsigned i=p->direction==1?(p->end+chunks-1)%chunks:p->start,visited=0;
        do {
            if(visited++==chunks || !chunk(&d,i))return false;
            if(p->direction==1){if(i==p->start)break;i=(i+chunks-1)%chunks;}
            else{i=(i+1)%chunks;if(i==p->end)break;}
        }while(true);
    }
    if(stats->chunks!=plan->visible_chunks)return false;
    stats->fault=0;return true;
}
