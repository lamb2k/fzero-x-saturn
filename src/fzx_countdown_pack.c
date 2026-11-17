#include "fzx_countdown_pack.h"
#include <string.h>

static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)<<16|u16(p+2);}
static bool zero(const uint8_t *p,unsigned n){for(unsigned i=0;i<n;++i)if(p[i])return false;return true;}

bool fzx_countdown_pack_open(fzx_countdown_pack_t *out,const void *data,
    uint32_t bytes,uint32_t metadata_hash,uint32_t texture_bytes,uint32_t texture_hash) {
    const uint8_t *p=data;
    if(!out||!p||bytes!=2352||!texture_bytes||texture_bytes>98304||memcmp(p,"FZXCDN01",8))return false;
    uint32_t file=2166136261u,self=2166136261u;
    for(unsigned i=0;i<bytes;++i){file=(file^p[i])*16777619u;self=(self^((i>=44&&i<48)?0u:p[i]))*16777619u;}
    if(file!=metadata_hash||self!=u32(p+44)||u32(p+8)!=bytes||u32(p+12)!=texture_bytes||
       u32(p+16)!=texture_hash||u32(p+20)!=92||u32(p+24)!=23||u32(p+28)!=28||
       u32(p+32)!=64||u32(p+36)!=1536||u32(p+40)!=1904||!zero(p+48,16))return false;
    bool used[FZX_COUNTDOWN_VERTICES]={false};unsigned next_image=0;uint32_t next_texture=0;
    for(unsigned i=0;i<FZX_COUNTDOWN_VERTICES;++i)if(!zero(p+64+i*16+14,2))return false;
    for(unsigned i=0;i<FZX_COUNTDOWN_QUADS;++i){
        const uint8_t *q=p+1536+i*16;const unsigned kind=i<20?0u:i<22?1u:2u,states=1u<<kind;
        if(u16(q+8)!=next_image||u16(q+10)!=states||u16(q+12)!=kind||u16(q+14)!=i)return false;
        for(unsigned j=0;j<4;++j){const unsigned row=u16(q+j*2);if(row>=92||used[row])return false;used[row]=true;}
        for(unsigned state=0;state<states;++state){
            const uint8_t *im=p+1904+(next_image+state)*16;
            const unsigned width=u16(im),height=u16(im+2),texture=u16(im+12);
            if(!width||(width&7u)||width>128||!height||(height&1u)||height>128||
               u32(im+4)!=next_texture||(next_texture&7u)||u16(im+8)!=i||u16(im+10)!=state||u16(im+14)||
               (kind==0?texture>=3:kind==1?texture!=3+state:texture!=5+state))return false;
            const uint32_t size=width*height*2u;
            if(next_texture>texture_bytes||size>texture_bytes-next_texture)return false;
            next_texture+=size;
        }
        next_image+=states;
    }
    if(next_image!=28||next_texture!=texture_bytes)return false;
    for(unsigned i=0;i<FZX_COUNTDOWN_VERTICES;++i)if(!used[i])return false;
    *out=(fzx_countdown_pack_t){p,bytes,texture_bytes,64,1536,1904};return true;
}

bool fzx_countdown_positions(const fzx_countdown_pack_t *p,sh2z_pos16_t *out,unsigned capacity){
    if(!p||!p->data||!out||capacity<FZX_COUNTDOWN_VERTICES)return false;
    for(unsigned i=0;i<FZX_COUNTDOWN_VERTICES;++i){const uint8_t *v=p->data+p->vertex_at+i*16;
        out[i]=(sh2z_pos16_t){(int16_t)u16(v),(int16_t)u16(v+2),(int16_t)u16(v+4)};}
    return true;
}

bool fzx_countdown_image(const fzx_countdown_pack_t *p,unsigned quad,
    const fzx_countdown_phase_t *phase,fzx_countdown_image_t *out){
    if(!p||!p->data||!phase||!out||quad>=FZX_COUNTDOWN_QUADS||!phase->visible||phase->panel>4)return false;
    const uint8_t *q=p->data+p->quad_at+quad*16;const unsigned kind=u16(q+12);
    const unsigned state=kind==1?(unsigned)phase->mouth_open:kind==2&&phase->panel?(unsigned)phase->panel-1:0u;
    const uint8_t *im=p->data+p->image_at+(u16(q+8)+state)*16;
    *out=(fzx_countdown_image_t){u32(im+4),u16(im),u16(im+2),kind==2&&phase->panel==0};return true;
}
