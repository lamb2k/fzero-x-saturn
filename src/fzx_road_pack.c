#include "fzx_road_pack.h"
#include <string.h>

static const uint32_t strides[FZX_ROAD_SECTIONS]={16,12,64,80,16,1};
uint16_t fzx_road_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]*256u+p[1]);
}
uint32_t fzx_road_u32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
uint32_t fzx_road_hash(const void *data,uint32_t bytes) {
    const uint8_t *p=data; uint32_t h=2166136261u;
    for(uint32_t i=0;i<bytes;++i) h=(h^p[i])*16777619u;
    return h;
}
static bool zero(const uint8_t *p,uint32_t n) {
    for(uint32_t i=0;i<n;++i) if(p[i]) return false;
    return true;
}
const uint8_t *fzx_road_record(const fzx_road_pack_t *p,unsigned section,uint32_t i) {
    if(!p || !p->data || section>=FZX_ROAD_SECTIONS || i>=p->count[section]) return NULL;
    return p->data+p->offset[section]+i*strides[section];
}
bool fzx_road_pack_open(fzx_road_pack_t *out,const void *data,uint32_t bytes,
                        uint32_t expected_hash,uint32_t expected_course,bool fixture) {
    const uint8_t *p=data;
    fzx_road_pack_t pack;
    uint32_t end=FZX_ROAD_HEADER_SIZE,hash=2166136261u,triangle_end=0,texel_end=0;
    static const uint32_t maximum[]={65535,65535,1024,256,64,FZX_ROAD_MAX_BYTES};
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!p || bytes<FZX_ROAD_HEADER_SIZE || bytes>FZX_ROAD_MAX_BYTES ||
       !expected_hash || !expected_course || memcmp(p,"FZXROAD1",8) ||
       fzx_road_u32(p+8)!=1 || fzx_road_u32(p+12)!=bytes ||
       fzx_road_u32(p+16)!=(fixture?FZX_ROAD_FIXTURE:0u) ||
       fzx_road_u32(p+20)!=expected_hash || fzx_road_u32(p+104)!=expected_course ||
       !zero(p+108,20)) return false;
    for(uint32_t i=0;i<bytes;++i) hash=(hash^((i>=20 && i<24)?0u:p[i]))*16777619u;
    if(hash!=expected_hash) return false;
    memset(&pack,0,sizeof(pack));
    pack.data=p; pack.bytes=bytes; pack.hash=hash;
    pack.flags=fzx_road_u32(p+16); pack.course_hash=expected_course;
    for(unsigned s=0;s<FZX_ROAD_SECTIONS;++s) {
        const uint32_t n=fzx_road_u32(p+24+s*4),at=fzx_road_u32(p+48+s*4);
        if(!n || n>maximum[s] || at!=end || n>(bytes-end)/strides[s]) return false;
        pack.count[s]=n; pack.offset[s]=at;
        end+=n*strides[s];
        const uint32_t aligned=(end+3u)&~3u;
        if(aligned>bytes || !zero(p+end,aligned-end)) return false;
        end=aligned;
    }
    if(end!=bytes) return false;
    for(uint32_t i=0;i<pack.count[FZX_ROAD_VERTEX];++i) {
        const uint8_t *v=fzx_road_record(&pack,FZX_ROAD_VERTEX,i);
        if(!zero(v+14,2)) return false;
    }
    for(uint32_t i=0;i<pack.count[FZX_ROAD_CHUNK];++i) {
        const uint8_t *c=fzx_road_record(&pack,FZX_ROAD_CHUNK,i);
        const uint32_t first=fzx_road_u32(c+8),n=fzx_road_u32(c+12);
        if(fzx_road_u16(c+4)>=64 || fzx_road_u16(c+6) || first!=triangle_end ||
           n>pack.count[FZX_ROAD_TRIANGLE]-triangle_end) return false;
        triangle_end+=n;
        for(uint32_t t=first;t<triangle_end;++t) {
            const uint8_t *tri=fzx_road_record(&pack,FZX_ROAD_TRIANGLE,t);
            for(unsigned k=0;k<3;++k)
                if(fzx_road_u16(tri+2*k)>=pack.count[FZX_ROAD_VERTEX]) return false;
            if(fzx_road_u16(tri+6)>=pack.count[FZX_ROAD_MATERIAL] ||
               fzx_road_u16(tri+8)!=i || (fzx_road_u16(tri+10)&~FZX_ROAD_OUTER)) return false;
        }
    }
    if(triangle_end!=pack.count[FZX_ROAD_TRIANGLE]) return false;
    for(uint32_t i=0;i<pack.count[FZX_ROAD_MATERIAL];++i) {
        const uint8_t *m=fzx_road_record(&pack,FZX_ROAD_MATERIAL,i);
        if(fzx_road_u16(m+40)>=pack.count[FZX_ROAD_TEXTURE] || !zero(m+68,12)) return false;
        const uint8_t *t=fzx_road_record(&pack,FZX_ROAD_TEXTURE,fzx_road_u16(m+40));
        if(fzx_road_u16(m+42)!=fzx_road_u16(t+8) || fzx_road_u16(m+44)!=fzx_road_u16(t+10) ||
           fzx_road_u16(m+46)!=1 || fzx_road_u16(m+52)>7 || fzx_road_u16(m+54)) return false;
    }
    for(uint32_t i=0;i<pack.count[FZX_ROAD_TEXTURE];++i) {
        const uint8_t *t=fzx_road_record(&pack,FZX_ROAD_TEXTURE,i);
        const uint32_t at=fzx_road_u32(t),n=fzx_road_u32(t+4);
        const uint32_t w=fzx_road_u16(t+8),h=fzx_road_u16(t+10);
        if(!w || !h || w>128 || h>64 || n!=w*h*2u || at!=texel_end ||
           n>pack.count[FZX_ROAD_TEXELS]-texel_end ||
           fzx_road_hash(p+pack.offset[FZX_ROAD_TEXELS]+at,n)!=fzx_road_u32(t+12)) return false;
        texel_end+=n;
    }
    if(texel_end!=pack.count[FZX_ROAD_TEXELS]) return false;
    *out=pack;
    return true;
}
