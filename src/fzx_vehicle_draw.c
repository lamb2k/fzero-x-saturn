#include "fzx_vehicle.h"
#include <limits.h>
#include <string.h>
/* FZX_VEHICLE_FAST (default 0) selects exact, order-neutral speedups of the
 * same rules: shade-1 lighting kernels, a per-draw Gouraud memo, unrolled
 * channel packing and native-endian aligned table loads on big-endian targets.
 * Emitted quads and statistics are identical to the default arm.
 * FZX_VEHICLE_NATIVE (default 0, implies FAST) is the approximate Saturn-native
 * arm (owner decision 2026-11-17): see native_draw below. */
#ifndef FZX_VEHICLE_NATIVE
#define FZX_VEHICLE_NATIVE 0
#endif
#if FZX_VEHICLE_NATIVE&&!defined(FZX_VEHICLE_FAST)
#define FZX_VEHICLE_FAST 1
#endif
#ifndef FZX_VEHICLE_FAST
#define FZX_VEHICLE_FAST 0
#endif
#if FZX_VEHICLE_NATIVE&&!FZX_VEHICLE_FAST
#error FZX_VEHICLE_NATIVE requires FZX_VEHICLE_FAST
#endif
#if FZX_VEHICLE_FAST
#include "sh2zam/shade_experimental.h"
#endif
#ifndef FZX_VEHICLE_PROFILE
#define FZX_VEHICLE_PROFILE 0
#endif
#if FZX_VEHICLE_PROFILE
extern void fzx_vehicle_profile_mark(unsigned);
#define MARK(n) fzx_vehicle_profile_mark(n)
#else
#define MARK(n) ((void)0)
#endif
/* Optional command-construction sub-phase profile (default off). Accumulates
 * 16-bit FRT deltas per sub-phase across the face loop; never changes output. */
#ifndef FZX_VEHICLE_SUBPROFILE
#define FZX_VEHICLE_SUBPROFILE 0
#endif
#if FZX_VEHICLE_SUBPROFILE
enum {SUB_CULL,SUB_FETCH,SUB_SHADE,SUB_SEND,SUB_CLIP,SUB_SETUP,SUB_LOOP,SUB_PHASES};
enum {SUBN_FACES,SUBN_PLANE_CULLED,SUBN_OUTCODE_CULLED,SUBN_SENT,SUBN_CLIPPED,SUBN_COUNTS};
extern volatile uint32_t fzx_vehicle_sub_ticks[SUB_PHASES],fzx_vehicle_sub_counts[SUBN_COUNTS];
extern uint32_t fzx_vehicle_sub_now(void);
#define SUB_NOW(v) const uint32_t v=fzx_vehicle_sub_now()
#define SUB_ADD(i,a,b) (fzx_vehicle_sub_ticks[i]+=(uint16_t)((b)-(a)))
#define SUB_COUNT(i) (++fzx_vehicle_sub_counts[i])
#else
#define SUB_NOW(v) ((void)0)
#define SUB_ADD(i,a,b) ((void)0)
#define SUB_COUNT(i) ((void)0)
#endif

static uint16_t u16(const uint8_t *p){return (uint16_t)((unsigned)p[0]*256u+p[1]);}
static int32_t s16(const uint8_t *p){return (int16_t)u16(p);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)*65536u+u16(p+2);}
static int32_t s32(const uint8_t *p){return (int32_t)u32(p);}
static bool zero(const uint8_t *p,unsigned n){for(unsigned i=0;i<n;++i)if(p[i])return false;return true;}
static unsigned align8(unsigned n){return (n+7u)&~7u;}
#if FZX_VEHICLE_FAST&&!FZX_VEHICLE_NATIVE&&defined(__BYTE_ORDER__)&&__BYTE_ORDER__==__ORDER_BIG_ENDIAN__
/* Pack tables are big-endian at 8-byte aligned offsets (checked at open); with
 * a 4-byte aligned pack the u16 fields used per face are 2-byte aligned and the
 * plane word 4-byte aligned, so native loads read the same values. */
#define FAST_NATIVE_LOADS 1
static uint16_t ld16(const uint8_t *p){return *(const uint16_t *)__builtin_assume_aligned(p,2);}
static int32_t lds16(const uint8_t *p){return *(const int16_t *)__builtin_assume_aligned(p,2);}
static int32_t lds32(const uint8_t *p){return *(const int32_t *)__builtin_assume_aligned(p,4);}
#else
#define FAST_NATIVE_LOADS 0
#endif

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

#if !FZX_VEHICLE_NATIVE
static unsigned mul8(unsigned a,unsigned b){const unsigned x=a*b+128u;return (x+(x>>8))>>8;}
#endif
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

#if FZX_VEHICLE_FAST
static bool light_prepare(sh2z_shade_light_t *prepared,const RefMachine *state,const fzx_vehicle_pose_t *pose){
    const sh2z_vec3_t light={(int32_t)state->light_dir[0]*65536,(int32_t)state->light_dir[1]*65536,(int32_t)state->light_dir[2]*65536};
    const sh2z_vec3_t reflection={(int32_t)state->reflection[0]*65536,(int32_t)state->reflection[1]*65536,(int32_t)state->reflection[2]*65536};
    return sh2z_shade_light_prepare(prepared,light,reflection,(const int16_t (*)[3])pose->basis,state->ambient,state->light_rgb);
}
#endif
#if FZX_VEHICLE_FAST&&!FZX_VEHICLE_NATIVE
static bool lights(const fzx_vehicle_pack_t *pack,const fzx_vehicle_lod_t *model,const RefMachine *state,
                   const fzx_vehicle_pose_t *pose,fzx_vehicle_work_t *work){
    sh2z_shade_light_t prepared;
    if(!light_prepare(&prepared,state,pose))return false;
    return sh2z_shade_light_normals(&work->light[0][0],4,(const int8_t *)(pack->data+model->normal_at),4,model->normals,&prepared,pack->data+pack->lut_at);
}
#elif !FZX_VEHICLE_FAST
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
#endif
#if !FZX_VEHICLE_FAST
static uint16_t shade(const uint8_t *base,const uint8_t *light,const uint8_t *fog,uint32_t *saturated){
    uint16_t packed=0;
    for(unsigned k=0;k<3;++k){unsigned lit=blend(mul8(base[k],light[k]),fog[k],255u-light[3]);
        int value=16+(int)(lit>>3)-(int)(base[k]>>4);
        if(value<0){value=0;++*saturated;}if(value>31){value=31;++*saturated;}
        packed|=(uint16_t)((unsigned)value<<(5*k));}
    return packed;
}
#endif
#if FZX_VEHICLE_FAST&&!FZX_VEHICLE_NATIVE
static unsigned clamp5(int value,unsigned *saturated){
    if(value<0){++*saturated;return 0;}if(value>31){++*saturated;return 31;}return (unsigned)value;
}
static uint16_t shade_fast(const uint8_t *base,const uint8_t *light,const uint8_t *fog,unsigned *saturated){
    const unsigned weight=255u-light[3];
    const unsigned r=blend(mul8(base[0],light[0]),fog[0],weight),g=blend(mul8(base[1],light[1]),fog[1],weight),b=blend(mul8(base[2],light[2]),fog[2],weight);
    return (uint16_t)(clamp5(16+(int)(r>>3)-(int)(base[0]>>4),saturated)|
        clamp5(16+(int)(g>>3)-(int)(base[1]>>4),saturated)<<5|
        clamp5(16+(int)(b>>3)-(int)(base[2]>>4),saturated)<<10);
}
#endif
#if !FZX_VEHICLE_NATIVE
static uint16_t solid_color(const uint8_t *base,const uint8_t *light,const uint8_t *fog){
    uint16_t packed=0x8000;
    for(unsigned k=0;k<3;++k){
        const unsigned lit=blend(mul8(base[k],light[k]),fog[k],255u-light[3]);
        packed|=(uint16_t)((lit>>3)<<(5*k));
    }
    return packed;
}
#endif
static bool send(const fzx_vehicle_quad_t *quad,unsigned capacity,fzx_vehicle_emit_fn emit,void *context,fzx_vehicle_stats_t *stats){
    if(stats->draws>=capacity||!emit(context,quad)){stats->fault=8;return false;}++stats->draws;return true;
}

#if FZX_VEHICLE_FAST&&!FZX_VEHICLE_NATIVE
static inline __attribute__((always_inline)) bool face_loop(const fzx_vehicle_pack_t *pack,const fzx_vehicle_lod_t *model,
        const fzx_view_t *view,const fzx_vehicle_pose_t *pose,const RefMachine *state,fzx_vehicle_work_t *work,
        unsigned capacity,fzx_vehicle_emit_fn emit,void *context,fzx_vehicle_stats_t *stats,const bool native){
#if FAST_NATIVE_LOADS
#define L16(x) (native?ld16(x):u16(x))
#define LS16(x) (native?lds16(x):s16(x))
#define LS32(x) (native?lds32(x):s32(x))
#else
#define L16(x) ((void)native,u16(x))
#define LS16(x) s16(x)
#define LS32(x) s32(x)
#endif
    const uint8_t *p=pack->data,*orders=p+model->order_at+pose->octant*model->faces*2u,*faces=p+model->face_at,*vertices=p+model->vertex_at;
    memset(work->gouraud_tag,0,model->normals);
    SUB_NOW(loop_start);
    for(unsigned order=0;order<model->faces;++order){
        SUB_NOW(t_face);SUB_COUNT(SUBN_FACES);
        const unsigned id=L16(orders+order*2);const uint8_t *face=faces+id*56;
        if(pose->cull_valid){const uint8_t *first=vertices+L16(face)*20;int32_t dot=-LS32(face+28),margin=3;
            for(unsigned k=0;k<3;++k){int32_t normal=LS16(face+20+k*2),distance=pose->eye[k]-LS16(first+k*2);dot+=normal*pose->eye[k];margin+=(distance<0?-distance:distance)+(normal<0?-normal:normal)*pose->eye_error;}
            if(dot< -margin){++stats->culled;SUB_COUNT(SUBN_PLANE_CULLED);{SUB_NOW(t_cull);SUB_ADD(SUB_CULL,t_face,t_cull);}continue;}
        }
        SUB_NOW(t_cull);SUB_ADD(SUB_CULL,t_face,t_cull);
        unsigned pi[4],ni[4],n=L16(face+14),all=63;bool simple=true;
        fzx_vehicle_quad_t q={0};q.face=(uint16_t)id;q.palette=L16(face+8);q.width=L16(face+10);q.height=L16(face+12);q.texture=(uint32_t)LS32(face+16);
        const uint8_t *base=work->representative[q.palette];const unsigned tag=q.palette+1u;
        for(unsigned j=0;j<4;++j){const uint8_t *v=vertices+L16(face+j*2)*20;pi[j]=L16(v+14);ni[j]=L16(v+16);const sh2z_view_vertex_t *a=work->projected+pi[j];all&=a->outcode;
            q.x[j]=a->sx;q.y[j]=a->sy;
            if((a->outcode&3u)||a->sx< -1023||a->sx>1023||a->sy< -1023||a->sy>1023)simple=false;
            SUB_NOW(t_shade0);
            const unsigned normal=ni[j];
            if(work->gouraud_tag[normal]!=tag){unsigned saturated=0;
                work->gouraud_value[normal]=shade_fast(base,work->light[normal],state->fog,&saturated);
                work->gouraud_saturated[normal]=(uint8_t)saturated;work->gouraud_tag[normal]=(uint8_t)tag;}
            q.gouraud[j]=work->gouraud_value[normal];stats->saturated+=work->gouraud_saturated[normal];
            SUB_NOW(t_shade1);SUB_ADD(SUB_SHADE,t_shade0,t_shade1);
        }
        SUB_NOW(t_fetch);SUB_ADD(SUB_FETCH,t_cull,t_fetch);
        if(all){++stats->culled;SUB_COUNT(SUBN_OUTCODE_CULLED);continue;}
        if(simple){if(!send(&q,capacity,emit,context,stats))return false;SUB_COUNT(SUBN_SENT);{SUB_NOW(t_send);SUB_ADD(SUB_SEND,t_fetch,t_send);}}
        else {SUB_COUNT(SUBN_CLIPPED);
            int32_t points[4][3];
            for(unsigned j=0;j<4;++j){const sh2z_view_vertex_t *a=work->projected+pi[j];points[j][0]=a->x/4;points[j][1]=a->y/4;points[j][2]=a->z/4;}
            int32_t clipped[10][3],screen[10][2];int count=fzx_view_clip(view,(const int32_t(*)[3])points,n,clipped);
            if(count<0){stats->fault=5;return false;}if(count<3){++stats->culled;continue;}++stats->clipped;
            for(int j=0;j<count;++j)if(!fzx_view_project(view,clipped[j],screen[j])){stats->fault=6;return false;}
            uint8_t average[4];
            for(unsigned k=0;k<4;++k){unsigned sum=0;for(unsigned j=0;j<n;++j)sum+=work->light[ni[j]][k];average[k]=(uint8_t)(sum/n);}
            q.solid=true;q.color=solid_color(base,average,state->fog);
            for(int j=1;j<count-1;++j){const unsigned indices[4]={0,(unsigned)j,(unsigned)j+1,(unsigned)j+1};for(unsigned k=0;k<4;++k){q.x[k]=(int16_t)(screen[indices[k]][0]/65536);q.y[k]=(int16_t)(screen[indices[k]][1]/65536);}if(!send(&q,capacity,emit,context,stats))return false;}
            SUB_NOW(t_clip);SUB_ADD(SUB_CLIP,t_fetch,t_clip);
        }
    }
#if FZX_VEHICLE_SUBPROFILE
    {SUB_NOW(loop_end);SUB_ADD(SUB_LOOP,loop_start,loop_end);}
#endif
#undef L16
#undef LS16
#undef LS32
    return true;
}
#endif

#if FZX_VEHICLE_NATIVE
/* Saturn-native vehicle arm (approximate by owner decision, 2026-11-17).
 * Per LOD, once: native-endian plane records, the frozen back-to-front orders,
 * face records, corner position ids, a static (palette, normal) pair table
 * with corner pair ids, and (template emit) prebuilt VDP1 command words.
 * Per draw (the SH2ZAM vehicle-kernels-1 plan):
 *  1. pose (fzx_vehicle_pose);
 *  2. shade-1 plane cull through the octant's frozen order;
 *  3. mark the position ids of the kept faces; project only those (indexed);
 *  4. screen-space facing on the projected quads, compacting the kept list;
 *  5. mark the pair ids of the surviving faces, and from them the normal ids;
 *     light only those normals (indexed); one Gouraud batch over the pairs;
 *  6. emission in the frozen order: template commands with the coordinates
 *     patched in (no callback), or the emit callback for host drivers.
 * The four per-draw kernels are SH2ZAM vehicle-kernels-1 (198848f). Id lists
 * are built by branch-free ascending scans that also clear the marks. */
#ifndef FZX_VEHICLE_NATIVE_FACING
/* Front faces of this pack project with negative signed area (y down); 0 disables. */
#define FZX_VEHICLE_NATIVE_FACING (-1)
#endif
#if FZX_VEHICLE_NATIVE_FACING!=0&&FZX_VEHICLE_NATIVE_FACING!=1&&FZX_VEHICLE_NATIVE_FACING!=-1
#error FZX_VEHICLE_NATIVE_FACING must be -1, 0 or 1
#endif
static bool native_tables(const fzx_vehicle_pack_t *pack,const fzx_vehicle_lod_t *model,unsigned lod,fzx_vehicle_work_t *work){
    if(work->native_lod==lod+1)return true;
    const uint8_t *p=pack->data;
    if(model->faces>FZX_VEHICLE_NATIVE_FACES||model->positions>FZX_VEHICLE_POSITIONS||model->normals>FZX_VEHICLE_NORMALS)return false;
    uint8_t head[FZX_VEHICLE_NORMALS],next[FZX_VEHICLE_NATIVE_PAIRS];unsigned pairs=0;
    memset(head,0xff,sizeof(head));
    for(unsigned i=0;i<model->faces;++i){const uint8_t *f=p+model->face_at+i*56,*first=p+model->vertex_at+u16(f)*20;
        sh2z_shade_plane_t *plane=work->planes+i;
        for(unsigned k=0;k<3;++k){plane->normal[k]=(int16_t)s16(f+20+k*2);plane->first[k]=(int16_t)s16(first+k*2);}
        plane->plane=s32(f+28);
        const unsigned palette=u16(f+8);
        for(unsigned j=0;j<4;++j){const uint8_t *v=p+model->vertex_at+u16(f+j*2)*20;const unsigned normal=u16(v+16);
            if(u16(v+14)>=model->positions||normal>=model->normals)return false;
            work->corner_position[i][j]=(uint8_t)u16(v+14);
            unsigned id=head[normal];
            while(id!=0xffu&&work->pair_table[id].base!=palette)id=next[id];
            if(id==0xffu){if(pairs>=FZX_VEHICLE_NATIVE_PAIRS)return false;
                id=pairs++;work->pair_table[id].base=(uint16_t)palette;work->pair_table[id].light=(uint16_t)normal;next[id]=head[normal];head[normal]=(uint8_t)id;}
            work->corner_pair[i][j]=(uint8_t)id;}
        work->face_info[i][0]=(uint16_t)palette;work->face_info[i][1]=u16(f+10);work->face_info[i][2]=u16(f+12);work->face_info[i][3]=u16(f+14);
        work->face_texture[i]=u32(f+16);
    }
    for(unsigned o=0;o<8;++o)for(unsigned i=0;i<model->faces;++i)work->orders[o][i]=u16(p+model->order_at+(o*model->faces+i)*2);
    for(unsigned o=0;o<8;++o)if(!sh2z_shade_planes_valid(work->planes,sizeof(work->planes[0]),model->faces,work->orders[o],model->faces))return false;
    for(unsigned i=0;i<model->positions;++i){const uint8_t *v=p+model->position_at+i*8;work->input[i]=(sh2z_pos16_t){(int16_t)u16(v),(int16_t)u16(v+2),(int16_t)u16(v+4)};}
    memset(work->position_seen,0,sizeof(work->position_seen));memset(work->normal_seen,0,sizeof(work->normal_seen));memset(work->pair_seen,0,sizeof(work->pair_seen));
    work->native_pairs=(uint16_t)pairs;work->template_lod=0;work->native_lod=(uint16_t)(lod+1);return true;
}
static uint32_t pack_words(unsigned high,unsigned low){return (uint32_t)(high&0xffffu)<<16|(low&0xffffu);}
static uint32_t vram_word(const uint8_t *at){return (uint32_t)(((uintptr_t)at&0x7ffffu)>>3);}
static bool native_templates(const fzx_vehicle_lod_t *model,unsigned lod,fzx_vehicle_work_t *work,const fzx_vehicle_target_t *target){
    if(work->template_lod==lod+1&&work->template_key[0]==(uintptr_t)target->pixels&&work->template_key[1]==(uintptr_t)target->shades)return true;
    if(((uintptr_t)target->commands&3u)||((uintptr_t)(target->shades+target->palette_bytes)&3u))return false;
    for(unsigned i=0;i<model->faces;++i){const unsigned palette=work->face_info[i][0],width=work->face_info[i][1],height=work->face_info[i][2];
        if(i*8u+8u>target->gouraud_bytes||palette*32u+32u>target->palette_bytes)return false;
        uint32_t *t=work->templates[i];
        t[0]=pack_words(target->texture_pmod,vram_word(target->shades+palette*32u));
        t[1]=pack_words(vram_word(target->pixels+work->face_texture[i]),((width>>3)<<8|height)&0x3fffu);
        t[2]=pack_words(vram_word(target->shades+target->palette_bytes+i*8u),0);
    }
    work->template_key[0]=(uintptr_t)target->pixels;work->template_key[1]=(uintptr_t)target->shades;work->template_lod=(uint16_t)(lod+1);return true;
}
/* No library calls in the per-draw path: the Saturn build has no builtin memcpy. */
static uint32_t corner_word(const sh2z_view_vertex_t *a){return pack_words((uint16_t)a->sx,(uint16_t)a->sy);}
/* Two Gouraud words in memory order g0 g1 | g2 g3 (big-endian VDP1 VRAM on the Saturn). */
static uint32_t gouraud_word(unsigned first,unsigned second){
#if defined(__BYTE_ORDER__)&&__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__
    return pack_words(second,first);
#else
    return pack_words(first,second);
#endif
}
static inline __attribute__((always_inline)) bool native_draw(const fzx_vehicle_pack_t *pack,const fzx_view_t *view,const RefCamera *camera,
        const RefMachine *state,fzx_vehicle_work_t *work,const fzx_vehicle_target_t *target,
        fzx_vehicle_emit_fn emit,void *context,unsigned capacity,fzx_vehicle_stats_t *stats){
    if(!state->normal_body_visible||!state->source_lod)return true;
    const unsigned lod=state->source_lod<=3?0u:state->source_lod==4?1u:2u;const fzx_vehicle_lod_t *model=pack->lod+lod;
    fzx_vehicle_pose_t pose;
    MARK(0);
    if(!fzx_vehicle_pose(&pose,view,camera,state)){stats->fault=2;return false;}
    MARK(1);
#if FZX_VEHICLE_SUBPROFILE
    for(unsigned i=0;i<SUB_PHASES;++i)fzx_vehicle_sub_ticks[i]=0;
    for(unsigned i=0;i<SUBN_COUNTS;++i)fzx_vehicle_sub_counts[i]=0;
#endif
    SUB_NOW(loop_start);
    if(!native_tables(pack,model,lod,work)||(target&&!native_templates(model,lod,work,target))){stats->fault=7;return false;}
    stats->lod=lod==0?1u:lod==1?4u:5u;
    size_t kept=0;const uint16_t *order=work->orders[pose.octant];
    const bool cull=pose.eye[0]>=-SH2Z_SHADE_EYE_MAX&&pose.eye[0]<=SH2Z_SHADE_EYE_MAX&&pose.eye[1]>=-SH2Z_SHADE_EYE_MAX&&
        pose.eye[1]<=SH2Z_SHADE_EYE_MAX&&pose.eye[2]>=-SH2Z_SHADE_EYE_MAX&&pose.eye[2]<=SH2Z_SHADE_EYE_MAX;
    if(cull){if(!sh2z_shade_cull(work->kept,&kept,order,model->faces,work->planes,sizeof(work->planes[0]),pose.eye,pose.eye_error)){stats->fault=7;return false;}}
    else{memcpy(work->kept,order,model->faces*sizeof(order[0]));kept=model->faces;}
    stats->culled+=(uint32_t)(model->faces-kept);
    SUB_NOW(t_cull);SUB_ADD(SUB_CULL,loop_start,t_cull);
    /* Referenced positions of the kept faces, then indexed projection. */
    for(size_t i=0;i<kept;++i){const uint8_t *c=work->corner_position[work->kept[i]];
        work->position_seen[c[0]]=1;work->position_seen[c[1]]=1;work->position_seen[c[2]]=1;work->position_seen[c[3]]=1;}
    unsigned positions=0;
    for(unsigned i=0;i<model->positions;++i){work->position_index[positions]=(uint8_t)i;positions+=work->position_seen[i];work->position_seen[i]=0;}
    SUB_NOW(t_fetch);SUB_ADD(SUB_FETCH,t_cull,t_fetch);
    if(!sh2z_view_batch_divu_indexed(work->projected,&pose.view,work->input,sizeof(work->input[0]),model->positions,work->position_index,positions)){stats->fault=3;return false;}
#if FZX_VEHICLE_NATIVE_FACING
    {const size_t before=kept;
     if(!sh2z_shade_screen_facing(work->kept,&kept,work->kept,kept,(const uint8_t (*)[4])work->corner_position,work->projected,&pose.view,FZX_VEHICLE_NATIVE_FACING)){stats->fault=7;return false;}
     stats->culled+=(uint32_t)(before-kept);}
#endif
    stats->positions=positions;
    MARK(2);
    palettes(pack,state,work,stats);MARK(3);
    SUB_NOW(t_palettes);
    /* Pairs of the surviving faces, then their normals. */
    for(size_t i=0;i<kept;++i){const uint8_t *c=work->corner_pair[work->kept[i]];
        work->pair_seen[c[0]]=1;work->pair_seen[c[1]]=1;work->pair_seen[c[2]]=1;work->pair_seen[c[3]]=1;}
    unsigned pairs=0,normals=0;
    for(unsigned i=0;i<work->native_pairs;++i){const unsigned seen=work->pair_seen[i];
        work->pair_index[pairs]=(uint8_t)i;work->pairs[pairs]=work->pair_table[i];work->normal_seen[work->pair_table[i].light]|=(uint8_t)seen;
        pairs+=seen;work->pair_seen[i]=0;}
    for(unsigned i=0;i<model->normals;++i){work->normal_index[normals]=(uint8_t)i;normals+=work->normal_seen[i];work->normal_seen[i]=0;}
    stats->normals=normals;
    sh2z_shade_light_t prepared;
    if(!light_prepare(&prepared,state,&pose)||
       !sh2z_shade_light_normals_indexed(&work->light[0][0],4,(const int8_t *)(pack->data+model->normal_at),4,model->normals,work->normal_index,normals,&prepared,pack->data+pack->lut_at)){stats->fault=4;return false;}
    MARK(4);
    SUB_NOW(t_setup);SUB_ADD(SUB_SETUP,t_palettes,t_setup);
    uint32_t saturated=0;
    if(!sh2z_shade_gouraud_approx(work->pair_value,&saturated,work->pairs,pairs,&work->representative[0][0],3,&work->light[0][0],4,state->fog)){stats->fault=7;return false;}
    for(size_t k=pairs;k-->0;)work->pair_value[work->pair_index[k]]=work->pair_value[k];
    stats->saturated+=saturated;
    SUB_NOW(t_shade);SUB_ADD(SUB_SHADE,t_setup,t_shade);
    const unsigned limit=target?target->capacity:capacity;
    uint8_t *const gouraud=target?target->shades+target->palette_bytes:NULL;
    const uint32_t texture_ctrl=target?pack_words(target->texture_ctrl,0):0;
    for(size_t i=0;i<kept;++i){const unsigned id=work->kept[i];SUB_NOW(t_face);
        const uint8_t *c=work->corner_position[id],*cp=work->corner_pair[id];
        const sh2z_view_vertex_t *const a[4]={work->projected+c[0],work->projected+c[1],work->projected+c[2],work->projected+c[3]};
        const unsigned any=a[0]->outcode|a[1]->outcode|a[2]->outcode|a[3]->outcode;
        bool simple=!any;
        if(!simple&&!(any&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR))){simple=true;
            for(unsigned j=0;j<4;++j)if(a[j]->sx< -SH2Z_SHADE_FACING_LIMIT||a[j]->sx>SH2Z_SHADE_FACING_LIMIT||a[j]->sy< -SH2Z_SHADE_FACING_LIMIT||a[j]->sy>SH2Z_SHADE_FACING_LIMIT)simple=false;}
#if !FZX_VEHICLE_NATIVE_FACING
        if((a[0]->outcode&a[1]->outcode&a[2]->outcode&a[3]->outcode)){++stats->culled;continue;}
#endif
        if(simple){
            const uint16_t g[4]={work->pair_value[cp[0]],work->pair_value[cp[1]],work->pair_value[cp[2]],work->pair_value[cp[3]]};
            if(target){
                if(stats->draws>=limit){stats->fault=8;return false;}
                uint32_t *cmd=target->commands[stats->draws];const uint32_t *t=work->templates[id];
                cmd[0]=texture_ctrl;cmd[1]=t[0];cmd[2]=t[1];
                cmd[3]=corner_word(a[0]);cmd[4]=corner_word(a[1]);cmd[5]=corner_word(a[2]);cmd[6]=corner_word(a[3]);cmd[7]=t[2];
                volatile uint32_t *slot=(volatile uint32_t *)(void *)(gouraud+id*8u);slot[0]=gouraud_word(g[0],g[1]);slot[1]=gouraud_word(g[2],g[3]);
                ++stats->draws;
            } else {
                fzx_vehicle_quad_t q={0};q.face=(uint16_t)id;q.palette=work->face_info[id][0];q.width=work->face_info[id][1];q.height=work->face_info[id][2];q.texture=work->face_texture[id];
                for(unsigned j=0;j<4;++j){q.x[j]=a[j]->sx;q.y[j]=a[j]->sy;q.gouraud[j]=g[j];}
                if(!send(&q,capacity,emit,context,stats))return false;
            }
            SUB_COUNT(SUBN_SENT);{SUB_NOW(t_send);SUB_ADD(SUB_SEND,t_face,t_send);}
        } else {SUB_COUNT(SUBN_CLIPPED);const unsigned n=work->face_info[id][3];
            int32_t points[4][3];
            for(unsigned j=0;j<4;++j){points[j][0]=a[j]->x/4;points[j][1]=a[j]->y/4;points[j][2]=a[j]->z/4;}
            int32_t clipped[10][3],screen[10][2];int count=fzx_view_clip(view,(const int32_t(*)[3])points,n,clipped);
            if(count<0){stats->fault=5;return false;}if(count<3){++stats->culled;continue;}++stats->clipped;
            for(int j=0;j<count;++j)if(!fzx_view_project(view,clipped[j],screen[j])){stats->fault=6;return false;}
            uint8_t average[4];
            for(unsigned k=0;k<4;++k){unsigned sum=0;for(unsigned j=0;j<n;++j)sum+=work->light[work->pair_table[cp[j]].light][k];average[k]=(uint8_t)(sum/n);}
            const uint16_t color=sh2z_shade_solid(work->representative[work->face_info[id][0]],average,state->fog);
            for(int j=1;j<count-1;++j){const unsigned indices[4]={0,(unsigned)j,(unsigned)j+1,(unsigned)j+1};
                int16_t x[4],y[4];for(unsigned k=0;k<4;++k){x[k]=(int16_t)(screen[indices[k]][0]/65536);y[k]=(int16_t)(screen[indices[k]][1]/65536);}
                if(target){
                    if(stats->draws>=limit){stats->fault=8;return false;}
                    uint32_t *cmd=target->commands[stats->draws];
                    cmd[0]=pack_words(target->solid_ctrl,0);cmd[1]=pack_words(target->solid_pmod,color);cmd[2]=0;
                    for(unsigned k=0;k<4;++k)cmd[3+k]=pack_words((uint16_t)x[k],(uint16_t)y[k]);
                    cmd[7]=0;++stats->draws;
                } else {
                    fzx_vehicle_quad_t q={0};q.face=(uint16_t)id;q.palette=work->face_info[id][0];q.solid=true;q.color=color;
                    for(unsigned k=0;k<4;++k){q.x[k]=x[k];q.y[k]=y[k];}
                    if(!send(&q,capacity,emit,context,stats))return false;
                }
            }
            SUB_NOW(t_clip);SUB_ADD(SUB_CLIP,t_face,t_clip);
        }
    }
#if FZX_VEHICLE_SUBPROFILE
    {SUB_NOW(loop_end);SUB_ADD(SUB_LOOP,loop_start,loop_end);}
#endif
    MARK(5);return true;
}
bool fzx_vehicle_draw(const fzx_vehicle_pack_t *pack,const fzx_view_t *view,const RefCamera *camera,
                      const RefMachine *state,fzx_vehicle_work_t *work,unsigned capacity,
                      fzx_vehicle_emit_fn emit,void *context,fzx_vehicle_stats_t *stats){
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));
    if(!pack||!pack->data||!view||!camera||!state||!work||!emit||state->faults||state->source_lod<0||state->source_lod>6){stats->fault=1;return false;}
    return native_draw(pack,view,camera,state,work,NULL,emit,context,capacity,stats);
}
bool fzx_vehicle_draw_native(const fzx_vehicle_pack_t *pack,const fzx_view_t *view,const RefCamera *camera,
                             const RefMachine *state,fzx_vehicle_work_t *work,const fzx_vehicle_target_t *target,
                             uint32_t *count,fzx_vehicle_stats_t *stats){
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));
    if(!count||!pack||!pack->data||!view||!camera||!state||!work||!target||!target->commands||!target->pixels||!target->shades||
       state->faults||state->source_lod<0||state->source_lod>6){stats->fault=1;return false;}
    const bool ok=native_draw(pack,view,camera,state,work,target,NULL,NULL,0,stats);
    *count=stats->draws;return ok;
}
#endif

#if !FZX_VEHICLE_NATIVE
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
#if FZX_VEHICLE_SUBPROFILE
    for(unsigned i=0;i<SUB_PHASES;++i)fzx_vehicle_sub_ticks[i]=0;
    for(unsigned i=0;i<SUBN_COUNTS;++i)fzx_vehicle_sub_counts[i]=0;
#endif
#if !FZX_VEHICLE_FAST
    SUB_NOW(loop_start);
#endif
    stats->positions=model->positions;stats->normals=model->normals;stats->lod=lod==0?1u:lod==1?4u:5u;
#if FZX_VEHICLE_FAST
#if FAST_NATIVE_LOADS
    if(((uintptr_t)p&3u)==0){if(!face_loop(pack,model,view,&pose,state,work,capacity,emit,context,stats,true))return false;}
    else
#endif
    if(!face_loop(pack,model,view,&pose,state,work,capacity,emit,context,stats,false))return false;
#else
    for(unsigned order=0;order<model->faces;++order){
        SUB_NOW(t_face);SUB_COUNT(SUBN_FACES);
        unsigned id=u16(p+model->order_at+(pose.octant*model->faces+order)*2);const uint8_t *face=p+model->face_at+id*56;
        const uint8_t *first=p+model->vertex_at+u16(face)*20;
        if(pose.cull_valid){int32_t dot=-s32(face+28),margin=3;
            for(unsigned k=0;k<3;++k){int32_t normal=s16(face+20+k*2),distance=pose.eye[k]-s16(first+k*2);dot+=normal*pose.eye[k];margin+=(distance<0?-distance:distance)+(normal<0?-normal:normal)*pose.eye_error;}
            if(dot< -margin){++stats->culled;SUB_COUNT(SUBN_PLANE_CULLED);{SUB_NOW(t_cull);SUB_ADD(SUB_CULL,t_face,t_cull);}continue;}
        }
        SUB_NOW(t_cull);SUB_ADD(SUB_CULL,t_face,t_cull);
        unsigned pi[4],ni[4],n=u16(face+14),all=63;bool simple=true;int32_t points[4][3];
        fzx_vehicle_quad_t q={0};q.face=(uint16_t)id;q.palette=u16(face+8);q.width=u16(face+10);q.height=u16(face+12);q.texture=u32(face+16);
        for(unsigned j=0;j<4;++j){const uint8_t *v=p+model->vertex_at+u16(face+j*2)*20;pi[j]=u16(v+14);ni[j]=u16(v+16);const sh2z_view_vertex_t *a=work->projected+pi[j];all&=a->outcode;
            q.x[j]=a->sx;q.y[j]=a->sy;points[j][0]=a->x/4;points[j][1]=a->y/4;points[j][2]=a->z/4;
            if((a->outcode&3u)||a->sx< -1023||a->sx>1023||a->sy< -1023||a->sy>1023)simple=false;
            SUB_NOW(t_shade0);
            q.gouraud[j]=shade(work->representative[q.palette],work->light[ni[j]],state->fog,&stats->saturated);
            SUB_NOW(t_shade1);SUB_ADD(SUB_SHADE,t_shade0,t_shade1);
        }
        SUB_NOW(t_fetch);SUB_ADD(SUB_FETCH,t_cull,t_fetch);
        if(all){++stats->culled;SUB_COUNT(SUBN_OUTCODE_CULLED);continue;}
        if(simple){if(!send(&q,capacity,emit,context,stats))return false;SUB_COUNT(SUBN_SENT);{SUB_NOW(t_send);SUB_ADD(SUB_SEND,t_fetch,t_send);}}
        else {SUB_COUNT(SUBN_CLIPPED);
            int32_t clipped[10][3],screen[10][2];int count=fzx_view_clip(view,(const int32_t(*)[3])points,n,clipped);
            if(count<0){stats->fault=5;return false;}if(count<3){++stats->culled;continue;}++stats->clipped;
            for(int j=0;j<count;++j)if(!fzx_view_project(view,clipped[j],screen[j])){stats->fault=6;return false;}
            /* Clipped charts use a solid approximation, retaining the current
             * face-average lighting/reflection and source fog at full intensity. */
            uint8_t average[4];
            for(unsigned k=0;k<4;++k){unsigned sum=0;for(unsigned j=0;j<n;++j)sum+=work->light[ni[j]][k];average[k]=(uint8_t)(sum/n);}
            q.solid=true;q.color=solid_color(work->representative[q.palette],average,state->fog);
            for(int j=1;j<count-1;++j){const unsigned indices[4]={0,(unsigned)j,(unsigned)j+1,(unsigned)j+1};for(unsigned k=0;k<4;++k){q.x[k]=(int16_t)(screen[indices[k]][0]/65536);q.y[k]=(int16_t)(screen[indices[k]][1]/65536);}if(!send(&q,capacity,emit,context,stats))return false;}
            SUB_NOW(t_clip);SUB_ADD(SUB_CLIP,t_fetch,t_clip);
        }
    }
#endif
#if FZX_VEHICLE_SUBPROFILE&&!FZX_VEHICLE_FAST
    {SUB_NOW(loop_end);SUB_ADD(SUB_LOOP,loop_start,loop_end);}
#endif
    MARK(5);return true;
}
#endif
