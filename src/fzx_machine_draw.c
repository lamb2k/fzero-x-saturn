#include "fzx_machine_draw.h"
#include <limits.h>
#include <math.h>
#include <string.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)((unsigned)p[0]*256+p[1]);}
static int32_t s16(const uint8_t *p){return (int16_t)u16(p);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)*65536+u16(p+2);}
static uint32_t hash_data(const uint8_t *p,uint32_t n){uint32_t h=2166136261u;for(uint32_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
static bool zero(const uint8_t *p,unsigned n){for(unsigned i=0;i<n;++i)if(p[i])return false;return true;}
bool fzx_machine_pack_open(fzx_machine_pack_t *out,const void *data,uint32_t bytes,uint32_t textures,uint32_t hash,bool fixture){
    if(!out)return false;
    memset(out,0,sizeof(*out));const uint8_t *p=data;
    if(!p||bytes<424||bytes>65536||textures>24576||memcmp(p,"FZXMCH01",8)||u32(p+8)!=bytes||u32(p+12)!=textures||
       u32(p+20)!=(unsigned)fixture||u32(p+24)!=3||u32(p+28)!=64||u32(p+32)!=160||u32(p+36)!=416||
       !zero(p+40,24)||!zero(p+419,5)||hash_data(p,bytes)!=hash)return false;
    unsigned at=424,pixel=0;const unsigned lods[3]={1,4,5};
    for(unsigned l=0;l<3;++l){
        const uint8_t *m=p+64+l*32;unsigned vc=u16(m+2),tc=u16(m+4),mc=u16(m+6),va=u32(m+8),ta=u32(m+12),ma=u32(m+16),pc=u16(m+20),nc=u16(m+22);
        if(u16(m)!=lods[l]||!vc||vc>512||!tc||tc>FZX_MACHINE_TRIANGLE_MAX||!mc||mc>64||!pc||pc>FZX_MACHINE_POSITION_MAX||pc>vc||
           !nc||nc>FZX_MACHINE_NORMAL_MAX||nc>vc||!zero(m+24,8)||va!=at||ma!=va+vc*20||ta!=ma+mc*12||ta+tc*24>bytes)return false;
        uint16_t pos_source[FZX_MACHINE_POSITION_MAX],normal_source[FZX_MACHINE_NORMAL_MAX];
        for(unsigned i=0;i<pc;++i)pos_source[i]=65535;
        for(unsigned i=0;i<nc;++i)normal_source[i]=65535;
        for(unsigned i=0;i<vc;++i){
            const uint8_t *v=p+va+i*20;unsigned pi=u16(v+14),ni=u16(v+16);
            if(pi>=pc||ni>=nc||v[13]||u16(v+18))return false;
            for(unsigned k=0;k<3;++k)if(s16(v+k*2)<-1024||s16(v+k*2)>1024)return false;
            if(pos_source[pi]==65535)pos_source[pi]=(uint16_t)i;
            else if(memcmp(v,p+va+pos_source[pi]*20,6))return false;
            if(normal_source[ni]==65535)normal_source[ni]=(uint16_t)i;
            else if(memcmp(v+10,p+va+normal_source[ni]*20+10,3))return false;
        }
        for(unsigned i=0;i<pc;++i)if(pos_source[i]==65535)return false;
        for(unsigned i=0;i<nc;++i)if(normal_source[i]==65535)return false;
        for(unsigned i=0;i<mc;++i){const uint8_t *a=p+ma+i*12;if(a[6]>1||a[7]>1||u16(a+8)>63||u16(a+10))return false;}
        at=ta+tc*24;
        for(unsigned i=0;i<tc;++i){
            const uint8_t *t=p+ta+i*24;unsigned width=u16(t+8),height=u16(t+10),colors=u16(t+12),pa=u32(t+20);
            if(u16(t)>=vc||u16(t+2)>=vc||u16(t+4)>=vc||u16(t+6)>=mc||!width||width>16||(width&7)||height<2||height>16||(height&1)||
               !colors||colors>16||u16(t+14)||u32(t+16)!=pixel||pa!=at||colors*8>bytes-at||width*height/2>textures-pixel)return false;
            at+=colors*8;pixel+=width*height/2;
        }
    }
    if(at!=bytes||pixel!=textures)return false;
    *out=(fzx_machine_pack_t){p,bytes,textures,hash,(uint8_t)fixture};return true;
}
const uint8_t *fzx_machine_pack_fog(const fzx_machine_pack_t *p){return p&&p->data?p->data+416:NULL;}
static bool finite_float(float x){uint32_t u;memcpy(&u,&x,4);return (u&0x7f800000u)!=0x7f800000u;}
static bool fixed(float x,float factor,float bound,int32_t *q){if(!finite_float(x)||x < -bound||x>bound)return false;*q=(int32_t)(x*factor);return true;}
static uint32_t root(uint64_t n){uint64_t r=0,b=UINT64_C(1)<<62;while(b>n)b>>=2;while(b){if(n>=r+b){n-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}return (uint32_t)r;}
static bool unit(const int32_t in[3],int32_t out[3]){uint64_t sum=0;for(unsigned k=0;k<3;++k)sum+=(uint64_t)((int64_t)in[k]*in[k]);uint32_t n=root(sum);if(!n)return false;for(unsigned k=0;k<3;++k)out[k]=(int32_t)((int64_t)in[k]*16384/n);return true;}
static void hash_word(fzx_machine_stats_t *s,uint32_t v){for(unsigned k=0;k<4;++k)s->hash=(s->hash^((v>>(24-k*8))&255))*16777619u;}
static bool send(fzx_machine_quad_t *q,unsigned capacity,fzx_machine_emit_fn emit,void *ctx,fzx_machine_stats_t *s){
    if(s->draws>=capacity){s->fault=7;return false;}
    hash_word(s,q->triangle);hash_word(s,q->solid);hash_word(s,q->offset);hash_word(s,q->width);hash_word(s,q->height);hash_word(s,q->color);
    for(unsigned k=0;k<4;++k){hash_word(s,(uint16_t)q->x[k]);hash_word(s,(uint16_t)q->y[k]);hash_word(s,q->light.gouraud[k]);}
    for(unsigned k=0;k<16;++k)hash_word(s,q->light.palette[k]);
    if(!emit(ctx,q)){s->fault=8;return false;}++s->draws;return true;
}
static uint16_t mean_color(const fzx_machine_palette_t *p,unsigned n){unsigned r=0,g=0,b=0;for(unsigned i=0;i<n;++i){r+=p->palette[i]&31;g+=(p->palette[i]>>5)&31;b+=(p->palette[i]>>10)&31;}return (uint16_t)(0x8000|(r/n)|((g/n)<<5)|((b/n)<<10));}
bool fzx_machine_draw(const fzx_machine_pack_t *pack,const fzx_view_t *view,const RefMachine *state,
    fzx_machine_work_t *work,unsigned capacity,fzx_machine_emit_fn emit,void *ctx,fzx_machine_stats_t *stats){
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));stats->hash=2166136261u;
    if(!pack||!pack->data||!view||!state||!work||!emit||state->faults||state->character!=0||state->custom_type!=0||state->source_lod<0||state->source_lod>6){stats->fault=1;return false;}
    if(!state->normal_body_visible||state->source_lod==0)return true;
    unsigned lod=state->source_lod<=3?0:state->source_lod==4?1:2;const uint8_t *p=pack->data,*m=p+64+lod*32;
    unsigned vc=u16(m+2),tc=u16(m+4),va=u32(m+8),ta=u32(m+12),ma=u32(m+16);stats->lod=u16(m);stats->triangles=tc;
    int32_t position[3],matrix[3][3],basis[3][3],light_in[3],reflection_in[3],light[3],reflection[3];
    for(unsigned i=0;i<3;++i){
        if(!fixed(state->model_pos[i],4096.0f,120000.0f,&position[i])){stats->fault=2;return false;}
        light_in[i]=state->light_dir[i];reflection_in[i]=state->reflection[i];
        for(unsigned j=0;j<3;++j){unsigned source=2-j;
            if(!fixed(state->model_basis[source][i],16384.0f,1.05f,&basis[i][j])||!finite_float(state->model_scale[source])||state->model_scale[source]<=0||state->model_scale[source]>1.0f||
               !fixed(state->model_basis[source][i]*state->model_scale[source],65536.0f,1.05f,&matrix[i][j])){stats->fault=2;return false;}
        }
    }
    if(!unit(light_in,light)||!unit(reflection_in,reflection)){stats->fault=3;return false;}
    memset(work->position_ready,0,sizeof(work->position_ready));memset(work->normal_ready,0,sizeof(work->normal_ready));
    for(unsigned i=0;i<vc;++i){
        const uint8_t *v=p+va+i*20;unsigned pi=u16(v+14),ni=u16(v+16);
        if(!work->position_ready[pi]){
            int32_t world[3];for(unsigned k=0;k<3;++k){int64_t value=0;for(unsigned j=0;j<3;++j)value+=(int64_t)s16(v+j*2)*matrix[k][j];value=value/16+position[k];if(value<INT32_MIN||value>INT32_MAX){stats->fault=4;return false;}world[k]=(int32_t)value;}
            if(!fzx_view_transform(view,world,work->positions[pi])){stats->fault=4;return false;}
            unsigned out=0;for(unsigned k=0;k<6;++k)if(fzx_view_plane(view,work->positions[pi],k)<0)out|=1u<<k;
            if(!fzx_view_project(view,work->positions[pi],work->screen[pi])){out|=64;work->screen[pi][0]=work->screen[pi][1]=0;}
            work->outcodes[pi]=out;work->position_ready[pi]=1;++stats->positions;
        }
        if(!work->normal_ready[ni]){
            int32_t normal[3];for(unsigned k=0;k<3;++k){int32_t value=0;for(unsigned j=0;j<3;++j)value+=(int8_t)v[10+j]*basis[k][j];normal[k]=value;}
            int64_t diffuse=0,reflect=0;for(unsigned k=0;k<3;++k){diffuse+=(int64_t)normal[k]*light[k];reflect+=(int64_t)normal[k]*reflection[k];}
            if(diffuse<0)diffuse=0;
            uint16_t rgb[3];for(unsigned k=0;k<3;++k){int64_t c=state->ambient[k]+diffuse*state->light_rgb[k]/(INT64_C(127)*16384*16384);if(c>255)c=255;rgb[k]=(uint16_t)(c*257);}
            int64_t index=reflect/(INT64_C(16384)*16384);if(index< -128)index=-128;if(index>127)index=127;
            work->normals[ni]=(fzx_material_color_t){rgb[0],rgb[1],rgb[2],(uint16_t)(p[160+128+index]*257u)};
            work->normal_ready[ni]=1;++stats->normals;
        }
    }
    for(unsigned i=0;i<tc;++i){const uint8_t *t=p+ta+i*24;int64_t z=0;for(unsigned j=0;j<3;++j)z+=work->positions[u16(p+va+u16(t+j*2)*20+14)][2];work->depth[i]=(uint32_t)(z<0?0:z>UINT32_MAX?UINT32_MAX:z);work->order[i]=(uint16_t)i;}
    for(unsigned i=1;i<tc;++i){uint16_t key=work->order[i];unsigned j=i;while(j&&work->depth[work->order[j-1]]<work->depth[key]){work->order[j]=work->order[j-1];--j;}work->order[j]=key;}
    for(unsigned order=0;order<tc;++order){
        unsigned i=work->order[order];const uint8_t *t=p+ta+i*24,*mat=p+ma+u16(t+6)*12;unsigned pi[3],ni[3],and_code=63;bool simple=true;
        int32_t points[3][3];fzx_material_color_t shade[3],base[16];
        for(unsigned j=0;j<3;++j){const uint8_t *v=p+va+u16(t+j*2)*20;pi[j]=u16(v+14);ni[j]=u16(v+16);and_code&=work->outcodes[pi[j]];shade[j]=work->normals[ni[j]];memcpy(points[j],work->positions[pi[j]],sizeof(points[j]));
            if(work->outcodes[pi[j]]&67u)simple=false;
            if(work->screen[pi[j]][0]<-1023*65536||work->screen[pi[j]][0]>1023*65536||work->screen[pi[j]][1]<-1023*65536||work->screen[pi[j]][1]>1023*65536)simple=false;
        }
        if(and_code){++stats->culled;continue;}
        fzx_material_combine_t combine={{mat[0],mat[1],mat[2]},{mat[3],mat[4],mat[5]},mat[6]!=0};if(mat[7])memcpy(combine.environment,state->body,3);
        const fzx_material_color_t white={65535,65535,65535,0};unsigned colors=u16(t+12);const uint8_t *pal=p+u32(t+20);
        for(unsigned j=0;j<colors;++j){fzx_material_color_t tex={u16(pal+j*8),u16(pal+j*8+2),u16(pal+j*8+4),u16(pal+j*8+6)};base[j]=fzx_material_shade(&combine,tex,white,state->fog);}
        fzx_machine_quad_t q;memset(&q,0,sizeof(q));q.width=u16(t+8);q.height=u16(t+10);q.triangle=(uint16_t)i;q.offset=u32(t+16);
        if(!fzx_machine_light(base,colors,shade,state->fog,&q.light)){stats->fault=5;return false;}stats->saturated+=q.light.saturated_channels;
        if(simple){
            for(unsigned j=0;j<3;++j){q.x[j]=(int16_t)(work->screen[pi[j]][0]/65536);q.y[j]=(int16_t)(work->screen[pi[j]][1]/65536);}q.x[3]=q.x[2];q.y[3]=q.y[2];
            if(!send(&q,capacity,emit,ctx,stats))return false;
        }else{
            int32_t clipped[10][3],screen[10][2];int n=fzx_view_clip(view,points,3,clipped);if(n<0){stats->fault=6;return false;}if(n<3){++stats->culled;continue;}++stats->clipped;
            for(int j=0;j<n;++j)if(!fzx_view_project(view,clipped[j],screen[j])){stats->fault=6;return false;}
            q.solid=true;q.color=mean_color(&q.light,colors);memset(&q.light,0,sizeof(q.light));
            for(int j=1;j<n-1;++j){unsigned ids[4]={0,(unsigned)j,(unsigned)j+1,(unsigned)j+1};for(unsigned k=0;k<4;++k){q.x[k]=(int16_t)(screen[ids[k]][0]/65536);q.y[k]=(int16_t)(screen[ids[k]][1]/65536);}if(!send(&q,capacity,emit,ctx,stats))return false;}
        }
    }
    return true;
}
