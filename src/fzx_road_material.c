#include "fzx_road_material.h"
#include <string.h>

bool fzx_road_material_open(fzx_road_material_t *out,const fzx_road_pack_t *p,uint32_t index)
{
    if(!out)return false;
    memset(out,0,sizeof(*out));
    const uint8_t *m=fzx_road_record(p,FZX_ROAD_MATERIAL,index);
    if(!m)return false;
    /* Fixed source race road state: fog, smooth unlit vertex colors,
     * perspective, three-point filtering, opaque surface and backface cull. */
    if(fzx_road_u32(m)!=0x00a10405u || fzx_road_u32(m+4)!=0x00082c00u ||
       fzx_road_u32(m+8)!=0xfa006220u || fzx_road_u32(m+16)!=0x44fe793cu ||
       (fzx_road_u32(m+12)!=0xfc147e28u && fzx_road_u32(m+12)!=0xfc357e6au))return false;
    const uint32_t tile0=fzx_road_u32(m+24),tile1=fzx_road_u32(m+28);
    const uint32_t size0=fzx_road_u32(m+32),size1=fzx_road_u32(m+36);
    const uint32_t tile=fzx_road_u16(m+52);
    if((tile0&0xfff80000u)!=0xf5100000u || (tile1>>24)!=tile ||
       size0!=0xf2000000u || (size1>>24)!=tile || (size1&0x00c00c00u) ||
       fzx_road_u16(m+46)!=1 || tile>7)return false;
    const uint8_t *record=fzx_road_record(p,FZX_ROAD_TEXTURE,fzx_road_u16(m+40));
    if(!record)return false;
    const uint32_t width=((size1>>12)&0xfffu)/4u+1u;
    const uint32_t height=(size1&0xfffu)/4u+1u;
    const uint32_t start=(tile0&0x1ffu)*8u,bytes=width*height*2u;
    const uint32_t texture_bytes=fzx_road_u32(record+4);
    if((size1&0x00003003u) || width!=fzx_road_u16(record+8) ||
       ((tile0>>9)&0x1ffu)*8u!=width*2u || start>texture_bytes ||
       bytes>texture_bytes-start || start%(width*2u))return false;
    /* The supported source load begins at TMEM zero and covers the full image.
     * Tile base/line/extent may select one contiguous half of that load. */
    if(fzx_road_u32(m+60)!=0xf3000000u ||
       (((fzx_road_u32(m+64)>>12)&0xfffu)+1u)*2u!=texture_bytes ||
       (fzx_road_u32(m+64)&0xfffu)!=(2048u+width/4u-1u)/(width/4u))return false;
    fzx_road_material_t result={0};
    result.texture=(fzx_material_texture_t){
        p->data+p->offset[FZX_ROAD_TEXELS]+fzx_road_u32(record)+start,
        bytes,(uint16_t)width,(uint16_t)height,FZX_TEX_RGBA16,
        (uint8_t)((tile1>>8)&3u),(uint8_t)((tile1>>18)&3u),
        (uint8_t)((tile1>>4)&15u),(uint8_t)((tile1>>14)&15u),
        (uint8_t)(tile1&15u),(uint8_t)((tile1>>10)&15u)};
    if(!fzx_material_texture_valid(&result.texture))return false;
    result.primitive=fzx_road_u32(m+12)==0xfc357e6au;
    *out=result;return true;
}

static uint32_t multiply(uint32_t a,uint32_t b)
{
    const uint32_t product=a*b+32768u;
    return (product+(product>>16))>>16;
}
fzx_material_color_t fzx_road_material_shade(const fzx_road_material_t *m,
    fzx_material_color_t texel,fzx_material_color_t shade,
    const uint8_t primitive[4],const uint8_t fog[3])
{
    const uint16_t texture[3]={texel.r,texel.g,texel.b};
    const uint16_t light[3]={shade.r,shade.g,shade.b};
    uint16_t result[3];
    const uint32_t alpha=m->primitive?(uint32_t)primitive[3]*257u:texel.a;
    for(unsigned c=0;c<3;++c){
        const uint32_t source=m->primitive?(uint32_t)primitive[c]*257u:texture[c];
        uint32_t mixed=multiply(source,alpha)+multiply(light[c],65535u-alpha);
        if(mixed>65535u)mixed=65535u;
        uint32_t final=multiply(mixed,65535u-shade.a)+multiply((uint32_t)fog[c]*257u,shade.a);
        if(final>65535u)final=65535u;
        result[c]=(uint16_t)final;
    }
    return (fzx_material_color_t){result[0],result[1],result[2],65535};
}
uint16_t fzx_road_material_pixel(const fzx_road_material_t *m,int32_t u,int32_t v,
    fzx_material_color_t shade,const uint8_t primitive[4],const uint8_t fog[3])
{
    const fzx_material_color_t texel=m->primitive?(fzx_material_color_t){0}:
        fzx_material_sample(&m->texture,u,v);
    const fzx_material_color_t color=fzx_road_material_shade(m,texel,shade,primitive,fog);
    const uint16_t channels[3]={color.r,color.g,color.b};
    uint16_t pixel=0x8000u;
    for(unsigned c=0;c<3;++c)
        pixel|=(uint16_t)((((uint32_t)channels[c]+128u)/257u>>3)<<(c*5u));
    return pixel;
}
