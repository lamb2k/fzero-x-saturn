#include "fzx_vehicle.h"
#include <limits.h>
#include <string.h>
#ifndef FZX_VEHICLE_PROFILE
#define FZX_VEHICLE_PROFILE 0
#endif
#if FZX_VEHICLE_PROFILE
extern void fzx_vehicle_profile_mark(unsigned);
#define MARK(n) fzx_vehicle_profile_mark(n)
#else
#define MARK(n) ((void)0)
#endif

static uint16_t u16(const uint8_t *p){return (uint16_t)((unsigned)p[0]*256u+p[1]);}
static int32_t s16(const uint8_t *p){return (int16_t)u16(p);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)*65536u+u16(p+2);}
static int32_t s32(const uint8_t *p){return (int32_t)u32(p);}
static bool zero(const uint8_t *p,unsigned n){for(unsigned i=0;i<n;++i)if(p[i])return false;return true;}
static unsigned align8(unsigned n){return (n+7u)&~7u;}

bool fzx_vehicle_pack_open(fzx_vehicle_pack_t *out,const void *data,uint32_t bytes,
                          uint32_t textures,uint32_t expected,bool fixture){
    if(!out)return false;
    memset(out,0,sizeof(*out));const uint8_t *p=data;
    if(!p||bytes<496||bytes>65536||textures>32768||memcmp(p,"FZXMCH02",8)||
       u32(p+8)!=bytes||u32(p+12)!=textures||u32(p+24)!=(uint32_t)fixture||
       u32(p+28)!=3||u32(p+32)!=96||!zero(p+54,42))return false;
    uint32_t full=2166136261u,self=2166136261u;
    for(unsigned i=0;i<bytes;++i){full=(full^p[i])*16777619u;self=(self^((i>=20&&i<24)?0u:p[i]))*16777619u;}
    if(full!=expected||self!=u32(p+20))return false;
    fzx_vehicle_pack_t next={0};next.data=p;next.bytes=bytes;next.textures=textures;
    next.palettes=u32(p+36);next.palette_at=u32(p+40);next.lut_at=u32(p+44);
    if(!next.palettes||next.palettes>FZX_VEHICLE_PALETTES||next.palette_at>bytes||
       next.palette_at+112*next.palettes!=next.lut_at||next.lut_at+256!=bytes)return false;
    unsigned at=240,pixel=0;const unsigned levels[3]={1,4,5};
    for(unsigned l=0;l<3;++l){
        const uint8_t *d=p+96+48*l;fzx_vehicle_lod_t *m=next.lod+l;
        *m=(fzx_vehicle_lod_t){u32(d+4),u32(d+8),u32(d+12),u32(d+16),u32(d+20),u32(d+24),u32(d+28),u32(d+32),u32(d+36),u32(d+40)};
        if(u32(d)!=levels[l]||u32(d+44)||!m->vertices||m->vertices>512||!m->positions||m->positions>FZX_VEHICLE_POSITIONS||
           !m->normals||m->normals>FZX_VEHICLE_NORMALS||!m->faces||m->faces>FZX_VEHICLE_FACES||
           !m->triangles||m->triangles>256||m->faces>m->triangles||
           m->vertex_at!=align8(at)||m->position_at!=align8(m->vertex_at+20*m->vertices)||
           m->normal_at!=align8(m->position_at+8*m->positions)||m->face_at!=align8(m->normal_at+4*m->normals)||
           m->order_at!=align8(m->face_at+56*m->faces)||m->order_at+16*m->faces>next.palette_at)return false;
        for(unsigned i=0;i<m->positions;++i){const uint8_t *v=p+m->position_at+i*8;if(u16(v+6))return false;for(unsigned k=0;k<3;++k)if(s16(v+k*2)<-1024||s16(v+k*2)>1024)return false;}
        for(unsigned i=0;i<m->normals;++i)if(p[m->normal_at+i*4+3])return false;
        for(unsigned i=0;i<m->vertices;++i){const uint8_t *v=p+m->vertex_at+i*20;unsigned pi=u16(v+14),ni=u16(v+16);
            if(pi>=m->positions||ni>=m->normals||u16(v+18)||memcmp(v,p+m->position_at+pi*8,6)||memcmp(v+10,p+m->normal_at+ni*4,3))return false;}
        uint8_t covered[256]={0};
        for(unsigned i=0;i<m->faces;++i){const uint8_t *f=p+m->face_at+i*56;unsigned n=u16(f+14),w=u16(f+10),h=u16(f+12);
            if((n!=3&&n!=4)||u16(f+8)>=next.palettes||w<8||w>32||w%8||h<2||h>32||h%2||
               u32(f+16)!=pixel||u16(f+26)||u16(f+38)||!zero(f+52,4))return false;
            for(unsigned j=0;j<4;++j)if(u16(f+j*2)>=m->vertices)return false;
            if(n==3&&(u16(f+4)!=u16(f+6)||u16(f+34)!=65535))return false;
            for(unsigned j=0;j<n-2;++j){unsigned id=u16(f+32+j*2);if(id>=m->triangles||covered[id])return false;covered[id]=1;}
            const uint8_t *first=p+m->vertex_at+u16(f)*20;int32_t plane=0;
            for(unsigned j=0;j<3;++j){int32_t normal=s16(f+20+j*2);if(normal< -16384||normal>16384)return false;plane+=normal*s16(first+j*2);}
            if(plane!=s32(f+28))return false;
            pixel+=w*h/2;if(pixel>textures)return false;
        }
        for(unsigned i=0;i<m->triangles;++i)if(!covered[i])return false;
        for(unsigned o=0;o<8;++o){uint8_t seen[256]={0};for(unsigned i=0;i<m->faces;++i){unsigned id=u16(p+m->order_at+(o*m->faces+i)*2);if(id>=m->faces||seen[id])return false;seen[id]=1;}}
        at=m->order_at+16*m->faces;
    }
    if(align8(at)!=next.palette_at||pixel!=textures)return false;
    for(unsigned i=0;i<next.palettes;++i){const uint8_t *pal=p+next.palette_at+i*112;if(pal[6]>1||pal[7]>1||!pal[8]||pal[8]>16||!zero(pal+9,7))return false;}
    *out=next;return true;
}

static unsigned mul8(unsigned a,unsigned b){const unsigned x=a*b+128u;return (x+(x>>8))>>8;}
static unsigned blend(unsigned a,unsigned b,unsigned factor){const unsigned x=a*factor+b*(255u-factor)+128u;return (x+(x>>8))>>8;}
static void palettes(const fzx_vehicle_pack_t *pack,const RefMachine *state,fzx_vehicle_work_t *work,fzx_vehicle_stats_t *stats){
    const bool changed=!work->palette_valid||memcmp(work->body,state->body,3)!=0;
    for(unsigned i=0;i<pack->palettes;++i){const uint8_t *p=pack->data+pack->palette_at+i*112;
        work->palette_dirty[i]=0;
        if(work->palette_valid&&(!p[6]||!changed))continue;
        unsigned sum[3]={0,0,0};
        for(unsigned j=0;j<p[8];++j){const uint8_t *tex=p+16+j*4;uint16_t native=0x8000;
            for(unsigned k=0;k<3;++k){unsigned env=p[6]?state->body[k]:p[3+k];
                unsigned value=p[7]?blend(tex[k],env,tex[3]):blend(p[k],env,tex[k]);
                sum[k]+=value;native|=(uint16_t)((value>>4)<<(5*k));}
            work->palette[i][j]=native;
        }
        for(unsigned j=p[8];j<16;++j)work->palette[i][j]=work->palette[i][0];
        for(unsigned k=0;k<3;++k)work->representative[i][k]=(uint8_t)(sum[k]/p[8]);
        work->palette_dirty[i]=1;++stats->palette_updates;
    }
    memcpy(work->body,state->body,3);work->palette_valid=true;
}

static bool lights(const fzx_vehicle_pack_t *pack,const fzx_vehicle_lod_t *model,const RefMachine *state,
                   const fzx_vehicle_pose_t *pose,fzx_vehicle_work_t *work){
    sh2z_vec3_t light=sh2z_vec3_normalize((sh2z_vec3_t){(int32_t)state->light_dir[0]*65536,(int32_t)state->light_dir[1]*65536,(int32_t)state->light_dir[2]*65536});
    sh2z_vec3_t reflection=sh2z_vec3_normalize((sh2z_vec3_t){(int32_t)state->reflection[0]*65536,(int32_t)state->reflection[1]*65536,(int32_t)state->reflection[2]*65536});
    if((!light.x&&!light.y&&!light.z)||(!reflection.x&&!reflection.y&&!reflection.z))return false;
    const int32_t li[3]={light.x/4,light.y/4,light.z/4},ri[3]={reflection.x/4,reflection.y/4,reflection.z/4};
    int32_t local_light[3],local_reflection[3];
    for(unsigned j=0;j<3;++j){int32_t l=0,r=0;for(unsigned k=0;k<3;++k){l+=pose->basis[k][j]*li[k];r+=pose->basis[k][j]*ri[k];}local_light[j]=l/16384;local_reflection[j]=r/16384;}
    for(unsigned i=0;i<model->normals;++i){const uint8_t *normal=pack->data+model->normal_at+i*4;int32_t diffuse=0,reflected=0;
        for(unsigned k=0;k<3;++k){diffuse+=(int8_t)normal[k]*local_light[k];reflected+=(int8_t)normal[k]*local_reflection[k];}
        if(diffuse<0)diffuse=0;
        if(diffuse>127*16384)diffuse=127*16384;
        for(unsigned k=0;k<3;++k){unsigned value=state->ambient[k]+(unsigned)diffuse*state->light_rgb[k]/(127u*16384u);work->light[i][k]=(uint8_t)(value>255?255:value);}
        int32_t index=reflected/16384;if(index< -128)index=-128;if(index>127)index=127;
        work->light[i][3]=pack->data[pack->lut_at+(unsigned)(index+128)];
    }
    return true;
}
static uint16_t shade(const uint8_t *base,const uint8_t *light,const uint8_t *fog,uint32_t *saturated){
    uint16_t packed=0;
    for(unsigned k=0;k<3;++k){unsigned lit=blend(mul8(base[k],light[k]),fog[k],255u-light[3]);
        int value=16+(int)(lit>>3)-(int)(base[k]>>4);
        if(value<0){value=0;++*saturated;}if(value>31){value=31;++*saturated;}
        packed|=(uint16_t)((unsigned)value<<(5*k));}
    return packed;
}
static uint16_t solid_color(const uint8_t *base,const uint8_t *light,const uint8_t *fog){
    uint16_t packed=0x8000;
    for(unsigned k=0;k<3;++k){
        const unsigned lit=blend(mul8(base[k],light[k]),fog[k],255u-light[3]);
        packed|=(uint16_t)((lit>>3)<<(5*k));
    }
    return packed;
}
static bool send(const fzx_vehicle_quad_t *quad,unsigned capacity,fzx_vehicle_emit_fn emit,void *context,fzx_vehicle_stats_t *stats){
    if(stats->draws>=capacity||!emit(context,quad)){stats->fault=8;return false;}++stats->draws;return true;
}

bool fzx_vehicle_draw(const fzx_vehicle_pack_t *pack,const fzx_view_t *view,const RefCamera *camera,
                      const RefMachine *state,fzx_vehicle_work_t *work,unsigned capacity,
                      fzx_vehicle_emit_fn emit,void *context,fzx_vehicle_stats_t *stats){
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));
    if(!pack||!pack->data||!view||!camera||!state||!work||!emit||state->faults||state->source_lod<0||state->source_lod>6){stats->fault=1;return false;}
    if(!state->normal_body_visible||!state->source_lod)return true;
    unsigned lod=state->source_lod<=3?0u:state->source_lod==4?1u:2u;const fzx_vehicle_lod_t *model=pack->lod+lod;
    const uint8_t *p=pack->data;fzx_vehicle_pose_t pose;
    MARK(0);
    if(!fzx_vehicle_pose(&pose,view,camera,state)){stats->fault=2;return false;}
    MARK(1);
    if(work->cached_lod!=lod+1){for(unsigned i=0;i<model->positions;++i){const uint8_t *v=p+model->position_at+i*8;work->input[i]=(sh2z_pos16_t){(int16_t)u16(v),(int16_t)u16(v+2),(int16_t)u16(v+4)};}work->cached_lod=lod+1;}
    if(!sh2z_view_batch_divu(work->projected,&pose.view,work->input,sizeof(*work->input),model->positions)){stats->fault=3;return false;}
    MARK(2);
    palettes(pack,state,work,stats);MARK(3);
    if(!lights(pack,model,state,&pose,work)){stats->fault=4;return false;}
    MARK(4);
    stats->positions=model->positions;stats->normals=model->normals;stats->lod=lod==0?1u:lod==1?4u:5u;
    for(unsigned order=0;order<model->faces;++order){
        unsigned id=u16(p+model->order_at+(pose.octant*model->faces+order)*2);const uint8_t *face=p+model->face_at+id*56;
        const uint8_t *first=p+model->vertex_at+u16(face)*20;
        if(pose.cull_valid){int32_t dot=-s32(face+28),margin=3;
            for(unsigned k=0;k<3;++k){int32_t normal=s16(face+20+k*2),distance=pose.eye[k]-s16(first+k*2);dot+=normal*pose.eye[k];margin+=(distance<0?-distance:distance)+(normal<0?-normal:normal)*pose.eye_error;}
            if(dot< -margin){++stats->culled;continue;}
        }
        unsigned pi[4],ni[4],n=u16(face+14),all=63;bool simple=true;int32_t points[4][3];
        fzx_vehicle_quad_t q={0};q.face=(uint16_t)id;q.palette=u16(face+8);q.width=u16(face+10);q.height=u16(face+12);q.texture=u32(face+16);
        for(unsigned j=0;j<4;++j){const uint8_t *v=p+model->vertex_at+u16(face+j*2)*20;pi[j]=u16(v+14);ni[j]=u16(v+16);const sh2z_view_vertex_t *a=work->projected+pi[j];all&=a->outcode;
            q.x[j]=a->sx;q.y[j]=a->sy;points[j][0]=a->x/4;points[j][1]=a->y/4;points[j][2]=a->z/4;
            if((a->outcode&3u)||a->sx< -1023||a->sx>1023||a->sy< -1023||a->sy>1023)simple=false;
            q.gouraud[j]=shade(work->representative[q.palette],work->light[ni[j]],state->fog,&stats->saturated);
        }
        if(all){++stats->culled;continue;}
        if(simple){if(!send(&q,capacity,emit,context,stats))return false;}
        else {
            int32_t clipped[10][3],screen[10][2];int count=fzx_view_clip(view,(const int32_t(*)[3])points,n,clipped);
            if(count<0){stats->fault=5;return false;}if(count<3){++stats->culled;continue;}++stats->clipped;
            for(int j=0;j<count;++j)if(!fzx_view_project(view,clipped[j],screen[j])){stats->fault=6;return false;}
            /* Clipped charts use a solid approximation, retaining the current
             * face-average lighting/reflection and source fog at full intensity. */
            uint8_t average[4];
            for(unsigned k=0;k<4;++k){unsigned sum=0;for(unsigned j=0;j<n;++j)sum+=work->light[ni[j]][k];average[k]=(uint8_t)(sum/n);}
            q.solid=true;q.color=solid_color(work->representative[q.palette],average,state->fog);
            for(int j=1;j<count-1;++j){const unsigned indices[4]={0,(unsigned)j,(unsigned)j+1,(unsigned)j+1};for(unsigned k=0;k<4;++k){q.x[k]=(int16_t)(screen[indices[k]][0]/65536);q.y[k]=(int16_t)(screen[indices[k]][1]/65536);}if(!send(&q,capacity,emit,context,stats))return false;}
        }
    }
    MARK(5);return true;
}
