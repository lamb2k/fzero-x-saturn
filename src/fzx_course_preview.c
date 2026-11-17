#include "fzx_course_preview.h"
#include <limits.h>
#include <string.h>

static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int32_t s32(const uint8_t *p){const uint32_t n=u32(p);return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static int16_t s16(const uint8_t *p){const uint16_t n=u16(p);return n<=INT16_MAX?(int16_t)n:(int16_t)(-1-(int)(UINT16_MAX-n));}
static uint32_t hash(const uint8_t *p,uint32_t n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}

bool fzx_course_preview_view(const fzx_course_preview_pack_t *p,unsigned yaw,
                             int32_t quarters,sh2z_view_t *out) {
    if(!p || !p->data || !out || yaw>=360 || quarters < -8192 || quarters>8192)return false;
    const uint8_t *m=p->data+p->camera_offset+yaw*72u;
    sh2z_view_t v={0};
    for(unsigned row=0;row<3;++row) {
        for(unsigned col=0;col<3;++col) {
            const int32_t n=s32(m+(col*4+row)*4);
            if(n < -65536 || n>65536)return false;
            v.r[row][col]=(int16_t)((row==2?-n:n)/16); /* Q16 * scale .25 -> Q14 */
        }
        const int32_t n=s32(m+(12+row)*4)/4;
        v.t[row]=row==2?-n:n;
    }
    if(s32(m+12)||s32(m+28)||s32(m+44)||s32(m+60)!=65536)return false;
    const int32_t fx=s32(m+64),fy=s32(m+68);
    if(fx<=0 || fx>INT32_MAX/80 || fy<=0 || fy>INT32_MAX/60)return false;
    v.focal_x=fx*80;v.focal_y=fy*60;
    v.center_x=(int16_t)(160+quarters/4-(quarters%4<0));
    /* Source y150, eight-line crop; SH2ZAM rounds y toward +infinity. */
    v.center_y=141;v.near_z=16*16384;v.far_z=16384*16384;
    v.clip[0]=v.clip[1]=0;v.clip[2]=319;v.clip[3]=223;
    return sh2z_view_init(out,&v);
}

bool fzx_course_preview_open(fzx_course_preview_pack_t *out,const void *data,
                             uint32_t bytes,uint32_t expected_hash) {
    const uint8_t *p=data;
    if(!out || !p || bytes<64 || bytes>512000 || !expected_hash || hash(p,bytes)!=expected_hash ||
       memcmp(p,"FZXCPV01",8) || u32(p+8)!=1 || u32(p+12)!=bytes || u32(p+16)!=24 ||
       u32(p+28)!=360 || u32(p+32)!=64 || u32(p+60) || hash(p+64,bytes-64)!=u32(p+48))return false;
    fzx_course_preview_pack_t a={0};a.data=p;a.bytes=bytes;
    a.vertices=u32(p+20);a.quads=u32(p+24);a.xyz_offset=u32(p+36);
    a.quad_offset=u32(p+40);a.camera_offset=u32(p+44);
    a.max_vertices=u32(p+52);a.max_quads=u32(p+56);
    if(!a.vertices || a.vertices>24*FZX_COURSE_PREVIEW_VERTEX_LIMIT || a.quads>a.vertices ||
       a.xyz_offset!=448 || a.quad_offset!=((a.xyz_offset+a.vertices*6+3u)&~3u) ||
       a.camera_offset!=a.quad_offset+a.quads*8 || bytes!=a.camera_offset+360*72)return false;
    for(uint32_t at=a.xyz_offset+a.vertices*6;at<a.quad_offset;++at)if(p[at])return false;
    uint32_t cv=0,cq=0,mv=0,mq=0;
    static const uint8_t order[3][4]={{1,0,3,4},{0,2,5,3},{2,1,4,5}};
    for(unsigned c=0;c<24;++c) {
        const uint8_t *d=p+64+c*16;
        const uint32_t nv=u32(d+4),nq=u32(d+12);
        if(u32(d)!=cv || u32(d+8)!=cq || nv<6 || nv>FZX_COURSE_PREVIEW_VERTEX_LIMIT ||
           nv%3 || nq!=nv-3 || nv>a.vertices-cv || nq>a.quads-cq)return false;
        if(memcmp(p+a.xyz_offset+cv*6,p+a.xyz_offset+(cv+nv-3)*6,18))return false;
        for(uint32_t q=0;q<nq;++q)for(unsigned j=0;j<4;++j)
            if(u16(p+a.quad_offset+(cq+q)*8+j*2)!=(q/3)*3+order[q%3][j])return false;
        cv+=nv;cq+=nq;if(nv>mv)mv=nv;if(nq>mq)mq=nq;
    }
    if(cv!=a.vertices || cq!=a.quads || mv!=a.max_vertices || mq!=a.max_quads)return false;
    for(unsigned i=0;i<360;++i){sh2z_view_t view;if(!fzx_course_preview_view(&a,i,0,&view))return false;}
    *out=a;return true;
}

bool fzx_course_preview_gather(const fzx_course_preview_pack_t *p,unsigned course,
                               sh2z_pos16_t *positions,unsigned capacity,unsigned *count) {
    if(!count)return false;
    *count=0;
    if(!p || !p->data || !positions || course>=24)return false;
    const uint8_t *d=p->data+64+course*16;
    const unsigned n=u32(d+4);if(n>capacity)return false;
    const uint8_t *v=p->data+p->xyz_offset+u32(d)*6;
    for(unsigned i=0;i<n;++i,v+=6)positions[i]=(sh2z_pos16_t){s16(v),s16(v+2),s16(v+4)};
    *count=n;return true;
}

static bool triangle(const sh2z_view_t *view,const sh2z_view_vertex_t *a,
    const sh2z_view_vertex_t *b,const sh2z_view_vertex_t *c,
    fzx_course_preview_emit_t emit,void *context,unsigned *count) {
    if(a->outcode&b->outcode&c->outcode)return true;
    const sh2z_view_vertex_t *v[3]={a,b,c};bool clip=false;
    for(unsigned i=0;i<3;++i)if((v[i]->outcode&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR)) ||
        v[i]->sx < -1024 || v[i]->sx>1023 || v[i]->sy < -1024 || v[i]->sy>1023)clip=true;
    if(!clip) {
        fzx_course_preview_triangle_t t;
        for(unsigned i=0;i<3;++i){t.x[i]=v[i]->sx;t.y[i]=v[i]->sy;}
        if(!emit(context,&t))return false;
        ++*count;return true;
    }
    sh2z_view_guard_vertex_t in[3]={0},out[SH2Z_VIEW_GUARD_MAX_OUT];
    const int16_t guard[4]={-1024,-1024,1023,1023};
    for(unsigned i=0;i<3;++i)in[i].v=*v[i];
    const int n=sh2z_view_clip_guard_divu(out,view,guard,in,3,0);
    if(n<0)return false;
    for(int i=1;i+1<n;++i) {
        const fzx_course_preview_triangle_t t={{out[0].v.sx,out[i].v.sx,out[i+1].v.sx},
                                              {out[0].v.sy,out[i].v.sy,out[i+1].v.sy}};
        if(!emit(context,&t))return false;
        ++*count;
    }
    return true;
}

bool fzx_course_preview_render(const fzx_course_preview_pack_t *p,unsigned course,
    unsigned yaw,int32_t quarters,sh2z_pos16_t *positions,sh2z_view_vertex_t *projected,
    unsigned capacity,fzx_course_preview_emit_t emit,void *context,unsigned *triangles) {
    if(!triangles)return false;
    *triangles=0;unsigned n=0;sh2z_view_t view;
    if(!projected || !emit || !fzx_course_preview_view(p,yaw,quarters,&view) ||
       !fzx_course_preview_gather(p,course,positions,capacity,&n) ||
       !sh2z_view_batch_divu(projected,&view,positions,sizeof(*positions),n))return false;
    const uint8_t *d=p->data+64+course*16,*q=p->data+p->quad_offset+u32(d+8)*8;
    for(unsigned i=0;i<u32(d+12);++i,q+=8) {
        const sh2z_view_vertex_t *v[4];for(unsigned j=0;j<4;++j)v[j]=projected+u16(q+j*2);
        if(!triangle(&view,v[0],v[1],v[3],emit,context,triangles) ||
           !triangle(&view,v[3],v[1],v[2],emit,context,triangles))return false;
    }
    return true;
}
