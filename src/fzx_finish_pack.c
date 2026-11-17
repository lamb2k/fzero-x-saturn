#include "fzx_finish_pack.h"
#include <string.h>

static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)<<16|u16(p+2);}
static int32_t s16(const uint8_t *p){const uint32_t v=u16(p);return v<32768u?(int32_t)v:(int32_t)v-65536;}
static uint32_t hash(const uint8_t *p,uint32_t bytes){uint32_t h=2166136261u;for(uint32_t i=0;i<bytes;++i)h=(h^p[i])*16777619u;return h;}
static bool valid(const fzx_finish_pack_t *p){return p&&p->data&&p->bytes==FZX_FINISH_METADATA_BYTES;}

bool fzx_finish_pack_open(fzx_finish_pack_t *out,const void *data,uint32_t bytes,
                         uint32_t metadata_hash,uint32_t texture_bytes,uint32_t texture_hash){
    const uint8_t *p=data;
    if(!out||!p||bytes!=FZX_FINISH_METADATA_BYTES||texture_bytes<160u||
       texture_bytes>FZX_FINISH_MAX_TEXTURE_BYTES||(texture_bytes-32u)%128u||memcmp(p,"FZXFIN01",8))return false;
    uint32_t self=2166136261u;
    for(uint32_t i=0;i<bytes;++i)self=(self^((i>=44u&&i<48u)?0u:p[i]))*16777619u;
    if(hash(p,bytes)!=metadata_hash||self!=u32(p+44)||u32(p+8)!=bytes||u32(p+12)!=texture_bytes||
       u32(p+16)!=texture_hash||u32(p+20)!=256u||u32(p+24)!=222u||u32(p+28)!=98u||
       u32(p+32)!=64u||u32(p+36)!=4160u||u32(p+40)!=7712u||
       u32(p+48)<0x8000u||u32(p+48)>0xffffu||u32(p+52)!=FZX_FINISH_SOURCE_GEOMETRY||
       u32(p+56)||u32(p+60))return false;
    bool used[256]={false},charts[98]={false};unsigned faces=0,sides=0;
    const unsigned chart_count=(texture_bytes-32u)/128u;
    for(unsigned i=0;i<256u;++i)if(u16(p+64u+i*16u+14u))return false;
    for(unsigned i=0;i<222u;++i){
        const uint8_t *t=p+4160u+i*16u;
        const unsigned a=u16(t),b=u16(t+2),c=u16(t+4),material=u16(t+6),image=u16(t+8);
        if(a>=256u||b>=256u||c>=256u||a==b||a==c||b==c||material>1u||
           u16(t+10)!=i||u32(t+12))return false;
        const uint8_t *v[3]={p+64u+a*16u,p+64u+b*16u,p+64u+c*16u};
        int64_t ab[3],ac[3];bool nonzero=false;
        for(unsigned j=0;j<3u;++j){ab[j]=(int64_t)s16(v[1]+j*2u)-s16(v[0]+j*2u);ac[j]=(int64_t)s16(v[2]+j*2u)-s16(v[0]+j*2u);}
        for(unsigned j=0;j<3u;++j)if(ab[(j+1u)%3u]*ac[(j+2u)%3u]!=ab[(j+2u)%3u]*ac[(j+1u)%3u])nonzero=true;
        if(!nonzero)return false;
        used[a]=used[b]=used[c]=true;
        if(material==0u){if(image!=0xffffu)return false;++sides;}
        else{
            if(image!=faces||faces>=98u)return false;
            const uint8_t *im=p+7712u+faces*16u;const uint32_t at=u32(im+4);
            if(u16(im)!=16u||u16(im+2)!=16u||u16(im+8)!=i||u16(im+10)||u32(im+12)||
               at<32u||(at-32u)%128u||at>texture_bytes||128u>texture_bytes-at)return false;
            charts[(at-32u)/128u]=true;++faces;
        }
    }
    if(faces!=98u||sides!=124u)return false;
    for(unsigned i=0;i<256u;++i)if(!used[i])return false;
    for(unsigned i=0;i<chart_count;++i)if(!charts[i])return false;
    *out=(fzx_finish_pack_t){p,bytes,texture_bytes,texture_hash,64,4160,7712,0,(uint16_t)u32(p+48)};
    return true;
}

bool fzx_finish_texture_check(const fzx_finish_pack_t *p,const void *data,uint32_t bytes){
    const uint8_t *v=data;
    if(!valid(p)||!v||bytes!=p->texture_bytes||bytes<160u||hash(v,bytes)!=p->texture_hash)return false;
    const uint16_t first=u16(v);
    for(unsigned i=0;i<16u;++i){const uint16_t c=u16(v+i*2u);
        if(c<0x8000u||(i>0u&&i<13u&&c<=u16(v+(i-1u)*2u))||(i>=13u&&c!=first))return false;}
    for(uint32_t i=32;i<bytes;++i)if((v[i]>>4)>=13u||(v[i]&15u)>=13u)return false;
    return true;
}

bool fzx_finish_vertex(const fzx_finish_pack_t *p,unsigned index,fzx_finish_vertex_t *out){
    if(!valid(p)||!out||index>=256u)return false;
    const uint8_t *v=p->data+p->vertex_at+index*16u;fzx_finish_vertex_t next;
    for(unsigned i=0;i<3u;++i)next.position[i]=(int16_t)s16(v+i*2u);
    for(unsigned i=0;i<2u;++i)next.uv[i]=(int16_t)s16(v+6u+i*2u);
    memcpy(next.attributes,v+10,4);*out=next;return true;
}
bool fzx_finish_positions(const fzx_finish_pack_t *p,sh2z_pos16_t *out,unsigned capacity){
    if(!valid(p)||!out||capacity<256u)return false;
    for(unsigned i=0;i<256u;++i){const uint8_t *v=p->data+p->vertex_at+i*16u;
        out[i]=(sh2z_pos16_t){(int16_t)s16(v),(int16_t)s16(v+2),(int16_t)s16(v+4)};}
    return true;
}
bool fzx_finish_triangle(const fzx_finish_pack_t *p,unsigned index,fzx_finish_triangle_t *out){
    if(!valid(p)||!out||index>=222u)return false;
    const uint8_t *t=p->data+p->triangle_at+index*16u;
    *out=(fzx_finish_triangle_t){{u16(t),u16(t+2),u16(t+4)},u16(t+6),u16(t+8),u16(t+10)};return true;
}
bool fzx_finish_image(const fzx_finish_pack_t *p,unsigned index,fzx_finish_image_t *out){
    if(!valid(p)||!out||index>=98u)return false;
    const uint8_t *im=p->data+p->image_at+index*16u;
    *out=(fzx_finish_image_t){u32(im+4),u16(im),u16(im+2),u16(im+8)};return true;
}
