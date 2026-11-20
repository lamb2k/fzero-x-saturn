#include "fzx_road_stream.h"
#include <string.h>
#define U16 fzx_road_u16
#define U32 fzx_road_u32
static const uint8_t strides[FZX_RLD_SECTIONS]={10,16,4,8,16,32,16,16};
static bool zero(const uint8_t *p,uint32_t n){for(uint32_t i=0;i<n;++i)if(p[i])return false;return true;}
const uint8_t *fzx_road_stream_record(const fzx_road_stream_pack_t *p,unsigned section,uint32_t index) {
    if(!p || !p->data || section>=FZX_RLD_SECTIONS || index>=p->count[section])return NULL;
    return p->data+p->offset[section]+index*strides[section];
}
static bool texture_range(uint32_t at,uint32_t length,uint32_t bytes) {
    return at>=64 && !(at&7u) && at<=bytes && length<=bytes-at;
}
/* FZX_LOAD_VERIFIED (engine scenes image): the CD loader has just checked this
 * file's FNV over every byte as it read it; the open does not hash it again. */
#ifndef FZX_LOAD_VERIFIED
#define FZX_LOAD_VERIFIED 0
#endif
/* Engine scenes image: the loading screen's turn inside the long open loops. */
#if FZX_CD_STREAM
extern void (*volatile fzx_cd_yield)(void);
#define LOAD_TURN() do { void (*h_)(void) = fzx_cd_yield; if (h_) h_(); } while (0)
#else
#define LOAD_TURN() ((void)0)
#endif
/* Boot-only: cold (optimized for size, kept out of the hot text). */
__attribute__((cold)) bool fzx_road_stream_open(fzx_road_stream_pack_t *out,const void *data,uint32_t bytes,
                          uint32_t hash,uint32_t texture_bytes) {
    const uint8_t *p=data;
    if(!out || !p || bytes<160 || bytes>160000 || texture_bytes<64 || texture_bytes>150000 ||
       !hash || (!FZX_LOAD_VERIFIED && fzx_road_hash(p,bytes)!=hash) || memcmp(p,"FZXRLD01",8) ||
       U32(p+8)!=1 || U32(p+12)!=bytes || U32(p+16)!=1 || p[155]!=255 || !zero(p+156,4))return false;
#if !FZX_LOAD_VERIFIED
    uint32_t embedded=2166136261u;
    for(uint32_t i=0;i<bytes;++i)embedded=(embedded^((i>=20 && i<24)?0u:p[i]))*16777619u;
    if(U32(p+20)!=embedded)return false;
#endif
    fzx_road_stream_pack_t pack={0};pack.data=p;pack.bytes=bytes;pack.texture_bytes=texture_bytes;
    const uint32_t nv=U32(p+88),nq=U32(p+92),nc=U32(p+96),ns=U32(p+100),ni=U32(p+104);
    if(!nv || nv>65535 || !nq || nq>32768 || !nc || nc>65535 || !ns || ns>256 ||
       ni!=ns*5 || U32(p+116)!=texture_bytes)return false;
    const uint32_t counts[FZX_RLD_SECTIONS]={nv,nq,nq,nc,ns,ni,U32(p+108),U32(p+112)};
    uint32_t cursor=160;
    for(unsigned i=0;i<FZX_RLD_SECTIONS;++i) {
        const uint32_t at=U32(p+120+i*4),count=counts[i],stride=strides[i];
        if(at!=cursor || at>bytes || count>(bytes-at)/stride)return false;
        const uint32_t end=at+count*stride,aligned=(end+7u)&~7u;
        if(aligned>bytes || !zero(p+end,aligned-end))return false;
        pack.count[i]=count;pack.offset[i]=at;cursor=aligned;
    }
    if(cursor!=bytes)return false;
    for(unsigned i=0;i<3;++i)pack.base_fog[i]=p[152+i];
    for(uint32_t i=0;i<ns;++i) {
        const uint8_t *s=fzx_road_stream_record(&pack,FZX_RLD_STRIPS,i);
        const unsigned width=U16(s+2),period=U16(s+4);
        if(!width || (width&7u) || width>504 || (period!=8 && period!=16 && period!=32) ||
           U16(s+6) || (int16_t)U16(s+8)>=(int16_t)U16(s+10) || U16(s+12)!=i*5 || U16(s+14)!=5)return false;
    }
    for(uint32_t i=0;i<nq;++i) {
        if((i&255u)==0u)LOAD_TURN();
        const uint8_t *q=fzx_road_stream_record(&pack,FZX_RLD_QUADS,i);
        const unsigned strip=U16(q+8),flags=U16(q+10);
        if((flags&~3u) || U16(q+12)!=i*2 || U16(q+14)>=nc)return false;
        const uint8_t *v[4];
        for(unsigned j=0;j<4;++j) {
            const unsigned id=U16(q+j*2);if(id>=nv)return false;
            for(unsigned k=0;k<j;++k)if(id==U16(q+k*2))return false;
            v[j]=fzx_road_stream_record(&pack,FZX_RLD_VERTICES,id);
        }
        const uint32_t repeat=U32(fzx_road_stream_record(&pack,FZX_RLD_REPEATS,i));
        if(flags&2u){if(strip!=65535 || repeat)return false;continue;}
        if(strip>=ns)return false;
        const uint8_t *s=fzx_road_stream_record(&pack,FZX_RLD_STRIPS,strip);
        const int a=(int16_t)U16(v[0]+6),b=(int16_t)U16(v[2]+6);
        if(a!=(int16_t)U16(v[1]+6) || b!=(int16_t)U16(v[3]+6) ||
           (a<b?a:b)!=(int16_t)U16(s+8) || (a>b?a:b)!=(int16_t)U16(s+10))return false;
        int lo[2],hi[2];
        for(unsigned j=0;j<2;++j){const int t=(int16_t)U16(v[j*2]+8),u=(int16_t)U16(v[j*2+1]+8);lo[j]=t<u?t:u;hi[j]=t>u?t:u;if(lo[j]==hi[j])return false;}
        const int first=lo[0]<lo[1]?lo[0]:lo[1],last=hi[0]>hi[1]?hi[0]:hi[1],unit=(int)U16(s+4)*32;
        const unsigned phase=(unsigned)((first%unit+unit)%unit),end=(unsigned)((last%unit+unit)%unit);
        const unsigned pieces=(phase+(unsigned)(last-first)+(unsigned)unit-1)/(unsigned)unit;
        if(pieces>63)return false;
        const uint32_t expected=phase|(pieces<<10)|(end<<16)|((uint32_t)(lo[0]!=lo[1] || hi[0]!=hi[1])<<26);
        if(repeat!=expected)return false;
    }
    cursor=0;
    for(uint32_t i=0;i<nc;++i) {
        if((i&63u)==0u)LOAD_TURN();
        const uint8_t *c=fzx_road_stream_record(&pack,FZX_RLD_CHUNKS,i);
        const uint32_t first=U32(c),count=U32(c+4);
        if(first!=cursor || count>nq-cursor)return false;
        for(uint32_t j=first;j<first+count;++j)if(U16(fzx_road_stream_record(&pack,FZX_RLD_QUADS,j)+14)!=i)return false;
        cursor+=count;
    }
    if(cursor!=nq)return false;
    static const unsigned factors[5]={1,2,4,8,0};
    for(uint32_t i=0;i<ni;++i) {
        const uint8_t *im=fzx_road_stream_record(&pack,FZX_RLD_IMAGES,i),*s=fzx_road_stream_record(&pack,FZX_RLD_STRIPS,i/5);
        const unsigned mode=U16(im),width=U16(im+2),height=U16(im+4),factor=U16(im+6);
        const unsigned source_width=U16(s+2),expected_width=mode && (source_width&15u)?source_width*2:source_width;
        if(mode>1 || width!=expected_width || width>504 || factor!=factors[i%5] || height!=(factor?U16(s+4):1u))return false;
        const uint32_t length=mode?width*height/2:width*height*2;
        for(unsigned j=0;j<3;++j) {
            const uint32_t at=U32(im+8+j*4),palette=U32(im+20+j*4);
            if(!texture_range(at,length,texture_bytes))return false;
            if(mode){if(at!=U32(im+8) || !texture_range(palette,32,texture_bytes))return false;}
            else if(palette)return false;
        }
    }
    for(unsigned section=FZX_RLD_FAR2;section<=FZX_RLD_FAR4;++section)for(uint32_t i=0;i<pack.count[section];++i) {
        const uint8_t *f=fzx_road_stream_record(&pack,section,i);
        for(unsigned j=0;j<4;++j){if(U16(f+j*2)>=nv)return false;for(unsigned k=0;k<j;++k)if(U16(f+j*2)==U16(f+k*2))return false;}
        if(U16(f+8)>=ns || U16(f+10)>=nq || (U16(f+14)&~1u))return false;
        const uint8_t *q=fzx_road_stream_record(&pack,FZX_RLD_QUADS,U16(f+10));
        if(U16(q+8)!=U16(f+8) || U16(q+10)!=U16(f+14))return false;
    }
    *out=pack;return true;
}

bool fzx_road_stream_gather(const fzx_road_stream_pack_t *p,const RefRoadPlan *plan,
                            uint16_t *remap,uint32_t remap_count,sh2z_pos16_t *positions,
                            uint32_t capacity,uint32_t *count) {
    if(!count)return false;
    *count=0;
    if(!p || !p->data || !plan || !remap || !positions || !capacity || capacity>65535 ||
       remap_count<p->count[FZX_RLD_VERTICES] || plan->faults || plan->count>REF_ROAD_GROUPS ||
       plan->outer>1)return false;
    memset(remap,0xff,p->count[FZX_RLD_VERTICES]*sizeof(*remap));
    const uint32_t chunks=p->count[FZX_RLD_CHUNKS];uint32_t used=0,visited_total=0;
    for(unsigned g=0;g<plan->count;++g) {
        const RefRoadGroup *group=plan->groups+g;
        if(group->start>=chunks || group->end>=chunks || (group->direction!=1 && group->direction!=-1))return false;
        uint32_t chunk=group->direction==1?(group->end+chunks-1)%chunks:group->start,visited=0;
        do {
            if(visited++==chunks)return false;
            ++visited_total;
            const uint8_t *c=fzx_road_stream_record(p,FZX_RLD_CHUNKS,chunk);
            const uint32_t first=U32(c),number=U32(c+4);
            for(uint32_t q=first;q<first+number;++q) {
                const uint8_t *quad=fzx_road_stream_record(p,FZX_RLD_QUADS,q);
                if(!plan->outer && (U16(quad+10)&1u))continue;
                for(unsigned j=0;j<4;++j) {
                    const uint16_t id=U16(quad+j*2);if(remap[id]!=UINT16_MAX)continue;
                    if(used==capacity)return false;
                    const uint8_t *v=fzx_road_stream_record(p,FZX_RLD_VERTICES,id);
                    positions[used]=(sh2z_pos16_t){(int16_t)U16(v),(int16_t)U16(v+2),(int16_t)U16(v+4)};
                    remap[id]=(uint16_t)used++;
                }
            }
            if(group->direction==1){if(chunk==group->start)break;chunk=(chunk+chunks-1)%chunks;}
            else{chunk=(chunk+1)%chunks;if(chunk==group->end)break;}
        }while(true);
    }
    if(visited_total!=plan->visible_chunks)return false;
    *count=used;return true;
}
