#include "fzx_machine_menu_model.h"
#include <limits.h>
#include <string.h>

static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int16_t s16(const uint8_t *p){const uint16_t n=u16(p);return n<=INT16_MAX?(int16_t)n:(int16_t)(-1-(int)(UINT16_MAX-n));}
static int32_t s32(const uint8_t *p){const uint32_t n=u32(p);return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static int signed8(uint8_t n){return n<=127?n:(int)n-256;}
static uint32_t hash(const uint8_t *p,uint32_t n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static bool span(uint32_t off,uint32_t count,uint32_t stride,uint32_t bytes){return off>=64 && off<=bytes && count<=(bytes-off)/stride;}
static const uint8_t *descriptor(const fzx_mm_pack_t *p,unsigned c){return p->data+64+c*96;}

bool fzx_mm_open(fzx_mm_pack_t *out,const void *data,uint32_t bytes,uint32_t expected) {
    const uint8_t *p=data;
    if(!out||!p||bytes<2944||bytes>524288||!expected||hash(p,bytes)!=expected||
       memcmp(p,"FZXMM01\0",8)||u32(p+8)!=1||u32(p+12)!=bytes||u32(p+16)!=30||
       u32(p+20)!=64||u32(p+24)!=96||u32(p+32)!=629||u32(p+56)||u32(p+60)||
       hash(p+64,bytes-64)!=u32(p+52)||!span(u32(p+28),629,64,bytes)||
       !span(u32(p+36),u32(p+40),32,bytes)||!span(u32(p+44),u32(p+48),1,bytes))return false;
    const uint32_t motion=u32(p+28),texture=u32(p+36),raw=u32(p+44),texture_count=u32(p+40);
    if(motion<2944||(motion&3)||!texture_count||texture_count>=256||
       texture-motion!=629*64||raw-texture!=texture_count*32||bytes-raw!=u32(p+48))return false;
    uint32_t tris=0;
    for(unsigned c=0;c<30;++c) {
        const uint8_t *d=p+64+c*96;
        const unsigned nv=u32(d+8),nt=u32(d+16),nm=u32(d+24),nb=u32(d+32),rv=u32(d+40),rt=u32(d+48);
        if(u32(d)!=c||!nv||nv>263||!nt||nt>132||!nm||nm>64||nb>32||rv>263||rt>132||
           !span(u32(d+4),nv,16,bytes)||!span(u32(d+12),nt,8,bytes)||
           !span(u32(d+20),nm,16,bytes)||!span(u32(d+28),nb,16,bytes)||
           !span(u32(d+36),rv,16,bytes)||!span(u32(d+44),rt,8,bytes)||
           !span(u32(d+52),4,16,bytes)||u32(d+56)>=u32(p+40)||
           !span(u32(d+60),12,1,bytes)||u32(d+64)!=tris||u32(d+68)!=nt||
           u32(d+72)!=0xA20405u||u32(d+76)!=0xA60405u)return false;
        static const unsigned offsets[8]={4,12,20,28,36,44,52,60};
        const unsigned counts[8]={nv,nt,nm,nb,rv,rt,4,12},strides[8]={16,8,16,16,16,8,16,1};
        for(unsigned j=0;j<8;++j){const unsigned off=u32(d+offsets[j]);
            if(off<2944||(off&3)||!span(off,counts[j],strides[j],motion))return false;}
        for(unsigned j=80;j<96;j+=4)if(u32(d+j))return false;
        for(unsigned pass=0;pass<2;++pass) {
            const uint8_t *t=p+u32(d+(pass?44:12));
            for(unsigned i=0;i<(pass?rt:nt);++i,t+=8) {
                for(unsigned j=0;j<3;++j)if(u16(t+j*2)>=(pass?rv:nv))return false;
                if(u16(t+6)>=(pass?1:nm))return false;
            }
        }
        for(unsigned i=0;i<nm;++i) {
            const uint8_t *m=p+u32(d+20)+i*16;
            if(m[2]>2||m[3]>1||u32(m+12))return false;
        }
        for(unsigned i=0;i<nb;++i)if(u16(p+u32(d+28)+i*16)>=u32(p+40))return false;
        tris+=nt;
    }
    for(unsigned i=0;i<u32(p+40);++i) {
        const uint8_t *t=p+u32(p+36)+i*32;
        if(u32(t+4)>2||!u32(t+8)||!u32(t+12)||u32(t+8)>256||u32(t+12)>256||u32(t+28)||
           !span(u32(t+16),u32(t+20),1,bytes)||hash(p+u32(t+16),u32(t+20))!=u32(t+24))return false;
    }
    for(unsigned i=0;i<629;++i) {
        const uint8_t *m=p+u32(p+28)+i*64;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
            if(s32(m+(row*4+col)*4)<-65537||s32(m+(row*4+col)*4)>65537)return false;
        if(s32(m+12)||s32(m+28)||s32(m+44)||s32(m+48)||s32(m+52)||s32(m+56)||s32(m+60)!=65536)return false;
    }
    *out=(fzx_mm_pack_t){p,bytes,u32(p+28)};return true;
}

bool fzx_mm_charts_open(fzx_mm_charts_t *out,const void *data,uint32_t bytes,uint32_t expected,
    const fzx_mm_pack_t *models,unsigned screen,unsigned character,unsigned skin) {
    const uint8_t *p=data;
    if(!out||!p||!models||!models->data||screen>1||character>=30||skin>=4||bytes<64||bytes>196608||
       !expected||hash(p,bytes)!=expected||memcmp(p,"FZXMC01\0",8)||u32(p+8)!=1||u32(p+12)!=bytes||
       u32(p+20)!=64||u32(p+52)||u32(p+56)||u32(p+60)||hash(p+64,bytes-64)!=u32(p+32))return false;
    const unsigned count=u32(p+16),off=u32(p+24),blob=u32(p+28),w=u32(p+36),h=u32(p+40);
    unsigned total=0;for(unsigned c=0;c<30;++c)total+=u32(descriptor(models,c)+16);
    if(count!=(screen?u32(descriptor(models,character)+16):total)||
       w!=(screen?16u:8u)||h!=(screen?16u:2u)||u32(p+44)!=(screen?character:30u)||u32(p+48)!=(screen?skin:0u)||
       off!=64+16*count||off>bytes||blob!=bytes-off||(blob&7))return false;
    for(unsigned i=0;i<count;++i) {
        const uint8_t *d=p+64+i*16;unsigned mode=u32(d+8),pix=u32(d),pal=u32(d+4);
        if((mode!=4&&mode!=16)||u32(d+12)||(pix&7)||pix>blob||w*h*(mode==4?1u:4u)/2u>blob-pix)return false;
        if(mode==4){if((pal&7)||pal>blob||32>blob-pal)return false;}
        else if(pal!=UINT32_MAX)return false;
    }
    *out=(fzx_mm_charts_t){p,p+off,bytes,count,blob,(uint8_t)w,(uint8_t)h,(uint8_t)u32(p+44),(uint8_t)u32(p+48)};return true;
}

bool fzx_mm_view(const fzx_mm_pack_t *p,const int32_t camera[18],unsigned motion,
    int32_t scale,int cx,int cy,sh2z_view_t *out,sh2z_mat34_t *model) {
    if(!p||!p->data||!camera||!out||!model||motion>=629||scale<6553||scale>68813||cx<0||cx>319||cy<0||cy>239)return false;
    const uint8_t *m=p->data+p->motion_offset+motion*64;
    sh2z_mat34_t view={0},scaled={0},combined;
    for(unsigned r=0;r<3;++r) {
        for(unsigned c=0;c<4;++c) {
            view.m[r][c]=camera[c*4+r];model->m[r][c]=s32(m+(c*4+r)*4);
            scaled.m[r][c]=c==3?model->m[r][c]:sh2z_mul(model->m[r][c],scale);
        }
    }
    sh2z_mat34_multiply(&combined,&view,&scaled);
    sh2z_view_t v={0};
    for(unsigned r=0;r<3;++r) {
        for(unsigned c=0;c<3;++c) {
            const int32_t n=combined.m[r][c]/4;
            if(n < -32767||n>32767)return false;
            v.r[r][c]=(int16_t)(r==2?-n:n);
        }
        v.t[r]=combined.m[r][3]/4;if(r==2)v.t[r]=-v.t[r];
    }
    if(camera[16]<=0||camera[17]<=0||camera[16]>INT32_MAX/160||camera[17]>INT32_MAX/120)return false;
    v.focal_x=camera[16]*160;v.focal_y=camera[17]*120;
    v.center_x=(int16_t)cx;v.center_y=(int16_t)(cy-9);
    v.near_z=16*16384;v.far_z=8192*16384;v.clip[2]=319;v.clip[3]=223;
    return sh2z_view_init(out,&v);
}

static uint8_t channel5(unsigned n){return (uint8_t)((n*255u+15)/31u);}
static bool prepare(const fzx_mm_pack_t *p,const uint8_t *d,unsigned pass,unsigned motion,
    unsigned screen,unsigned locked,const int32_t camera[18],const uint8_t *reflection,
    int cx,int cy,fzx_mm_work_t *w,sh2z_view_t *view) {
    const unsigned vo=u32(d+(pass==FZX_MM_REFLECTION?36:pass==FZX_MM_SHADOW?52:4));
    const unsigned nv=pass==FZX_MM_SHADOW?4:u32(d+(pass==FZX_MM_REFLECTION?40:8));
    sh2z_mat34_t model;
    if(!fzx_mm_view(p,camera,motion,screen?(pass==FZX_MM_OUTLINE?68813:65536):6554,cx,cy,view,&model))return false;
    /* Normalize each original directional vector: select (100,50,69),
     * settings (50,25,34). Their integer directions differ slightly. */
    const sh2z_vec3_t light=screen?(sh2z_vec3_t){50081,25041,34055}:
                                  (sh2z_vec3_t){49882,24941,34419};
    for(unsigned i=0;i<nv;++i) {
        const uint8_t *v=p->data+vo+i*16;
        w->positions[i]=(sh2z_pos16_t){s16(v),s16(v+2),s16(v+4)};
        sh2z_vec3_t n={(int32_t)signed8(v[12])*516,(int32_t)signed8(v[13])*516,(int32_t)signed8(v[14])*516};
        n=sh2z_mat34_transform_direction(&model,n);
        if(pass==FZX_MM_REFLECTION) {
            const sh2z_vec3_t ax={camera[0],camera[4],camera[8]},ay={camera[1],camera[5],camera[9]};
            int32_t x=sh2z_vec3_dot(n,ax),y=sh2z_vec3_dot(n,ay);
            if(x < -65536)x=-65536;
            if(x>65536)x=65536;
            if(y < -65536)y=-65536;
            if(y>65536)y=65536;
            /* Source G_TEXTURE_GEN: (dot+1)/4 *2600, then 10.5 UV /32.
             * Native samples at vertices; Gouraud interpolates source colors. */
            unsigned u=(unsigned)(((int64_t)(x+65536)*2600*512)/(INT64_C(65536)*65536))&15u;
            unsigned vtex=(unsigned)(((int64_t)(y+65536)*2600*512)/(INT64_C(65536)*65536))&15u;
            const uint16_t rgb=u16(reflection+(vtex*16+u)*2);
            for(unsigned c=0;c<3;++c)w->shade[i][c]=channel5((rgb>>(c*5))&31u);
        } else {
            int32_t intensity=sh2z_vec3_dot(n,light);if(intensity<0)intensity=0;if(intensity>65536)intensity=65536;
            for(unsigned c=0;c<3;++c)w->shade[i][c]=(uint8_t)((intensity*(locked?(c?70:100):255)+32768)/65536);
        }
    }
    return sh2z_view_batch_divu(w->projected,view,w->positions,sizeof(*w->positions),nv);
}

static bool emit_triangles(const fzx_mm_pack_t *p,const uint8_t *d,unsigned pass,unsigned character,
    unsigned chart_first,fzx_mm_work_t *w,fzx_mm_emit_t emit,void *context,unsigned *commands) {
    const unsigned nt=u32(d+(pass==FZX_MM_REFLECTION?48:16));
    const uint8_t *tri=p->data+u32(d+(pass==FZX_MM_REFLECTION?44:12));
    unsigned n=0;
    for(unsigned i=0;i<nt;++i) {
        const uint8_t *t=tri+i*8;const sh2z_view_vertex_t *v[3];
        for(unsigned j=0;j<3;++j)v[j]=w->projected+u16(t+j*2);
        if(v[0]->outcode&v[1]->outcode&v[2]->outcode)continue;
        for(unsigned j=0;j<3;++j)if((v[j]->outcode&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR))||v[j]->sx < -1024||v[j]->sx>1023||v[j]->sy < -1024||v[j]->sy>1023)return false;
        const int64_t area=(int64_t)(v[1]->sx-v[0]->sx)*(v[2]->sy-v[0]->sy)-(int64_t)(v[1]->sy-v[0]->sy)*(v[2]->sx-v[0]->sx);
        if(!area)continue;
        /* All three source passes use back-face culling, including the black
         * enlarged outline under the base. Painter sort replaces source Z. */
        if(area>0)continue;
        const int32_t depth=(int32_t)(((int64_t)v[0]->z+v[1]->z+v[2]->z)/3);
        unsigned k=n++;while(k && w->depth[k-1]<depth){w->depth[k]=w->depth[k-1];w->order[k]=w->order[k-1];--k;}
        w->depth[k]=depth;w->order[k]=(uint16_t)i;
    }
    for(unsigned i=0;i<n;++i) {
        const unsigned index=w->order[i];const uint8_t *t=tri+index*8;
        fzx_mm_quad_t q={0};q.kind=(uint8_t)pass;q.character=(uint8_t)character;q.triangle=(uint16_t)(chart_first+index);
        for(unsigned j=0;j<3;++j){const unsigned vi=u16(t+j*2);q.x[j]=w->projected[vi].sx;q.y[j]=w->projected[vi].sy;memcpy(q.shade[j],w->shade[vi],3);}
        q.x[3]=q.x[2];q.y[3]=q.y[2];memcpy(q.shade[3],q.shade[2],3);
        if(!emit(context,&q))return false;
        ++*commands;
    }
    return true;
}

bool fzx_mm_render(const fzx_mm_pack_t *p,const fzx_mm_charts_t *charts,
    const int32_t camera[2][18],const uint8_t slots[30],const uint8_t reflection[512],
    const fzx_machine_menu_snapshot_t *f,fzx_mm_work_t *w,fzx_mm_emit_t emit,void *context,unsigned *commands) {
    if(!commands)return false;
    *commands=0;
    if(!p||!p->data||!charts||!charts->data||!camera||!slots||!f||!w||!emit||f->screen>1||f->character>=30||f->slot>=30||f->skin>=4||f->unlocked>30)return false;
    uint32_t mask=0;for(unsigned i=0;i<30;++i){if(slots[i]>=30 || (mask&(1u<<slots[i])))return false;mask|=1u<<slots[i];}
    if(slots[f->slot]!=f->character||charts->character!=(f->screen?f->character:30)||charts->skin!=(f->screen?f->skin:0))return false;
    if(!f->screen) {
        for(unsigned slot=0;slot<30;++slot) {
            const unsigned character=slots[slot];const uint8_t *d=descriptor(p,character);sh2z_view_t view;
            const unsigned motion=slot==f->slot?1+f->model_ticks%209:0;
            if(!prepare(p,d,FZX_MM_BASE,motion,0,slot>=f->unlocked,camera[0],NULL,57+(int)(slot%6)*40,57+(int)(slot/6)*34,w,&view)||
               !emit_triangles(p,d,FZX_MM_BASE,character,u32(d+64),w,emit,context,commands))return false;
        }
    } else {
        if(!reflection)return false;
        const uint8_t *d=descriptor(p,f->character);sh2z_view_t view;
        const unsigned motion=210+f->model_ticks%419;
        if(!prepare(p,d,FZX_MM_SHADOW,motion,1,0,camera[1],reflection,216,210,w,&view))return false;
        fzx_mm_quad_t q={0};q.kind=FZX_MM_SHADOW;q.character=f->character;
        static const unsigned order[4]={0,3,1,2};
        for(unsigned j=0;j<4;++j){const sh2z_view_vertex_t *v=&w->projected[order[j]];if(v->outcode&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR))return false;q.x[j]=v->sx;q.y[j]=v->sy;}
        if(!emit(context,&q))return false;
        ++*commands;
        static const unsigned passes[3]={FZX_MM_OUTLINE,FZX_MM_BASE,FZX_MM_REFLECTION};
        for(unsigned i=0;i<3;++i)if(!prepare(p,d,passes[i],motion,1,0,camera[1],reflection,220,200,w,&view)||
            !emit_triangles(p,d,passes[i],f->character,0,w,emit,context,commands))return false;
    }
    return true;
}
