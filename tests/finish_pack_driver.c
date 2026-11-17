/* HOST pack admission/accessor test. Input packs may be synthetic or private.
 * Hashes supplied on argv are the build receipt's expected complete identities.
 */
#include "fzx_finish_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char metadata[FZX_FINISH_METADATA_BYTES],texture[FZX_FINISH_MAX_TEXTURE_BYTES];
static bool load(const char *name,unsigned char *data,unsigned capacity,unsigned *bytes){
    FILE *f=fopen(name,"rb");if(!f)return false;
    *bytes=(unsigned)fread(data,1,capacity,f);const int extra=fgetc(f),error=ferror(f);
    const int closed=fclose(f);return extra==EOF&&!error&&!closed;
}
int main(int argc,char **argv){
    if(argc!=5)return 2;
    unsigned mb,tb;if(!load(argv[1],metadata,sizeof(metadata),&mb)||!load(argv[2],texture,sizeof(texture),&tb))return 3;
    const uint32_t mh=(uint32_t)strtoul(argv[3],NULL,0),th=(uint32_t)strtoul(argv[4],NULL,0);
    struct {uint32_t before;fzx_finish_pack_t pack;uint32_t after;} guard;
    memset(&guard,0xa5,sizeof(guard));const fzx_finish_pack_t unchanged=guard.pack;
    const bool opened=fzx_finish_pack_open(&guard.pack,metadata,mb,mh,tb,th);
    if(guard.before!=0xa5a5a5a5u||guard.after!=0xa5a5a5a5u)return 4;
    if(!opened){if(memcmp(&guard.pack,&unchanged,sizeof(unchanged)))return 5;puts("REJECT_METADATA");return 0;}
    if(!fzx_finish_texture_check(&guard.pack,texture,tb)){puts("REJECT_TEXTURE");return 0;}
    struct {uint32_t before;sh2z_pos16_t v[256];uint32_t after;} positions;
    memset(&positions,0xa5,sizeof(positions));
    if(fzx_finish_positions(&guard.pack,positions.v,255)||
       !fzx_finish_positions(&guard.pack,positions.v,256)||
       positions.before!=0xa5a5a5a5u||positions.after!=0xa5a5a5a5u)return 6;
    unsigned sides=0,faces=0;uint32_t fields=2166136261u;
    for(unsigned i=0;i<256u;++i){fzx_finish_vertex_t v;if(!fzx_finish_vertex(&guard.pack,i,&v))return 7;
        if(v.position[0]!=positions.v[i].x||v.position[1]!=positions.v[i].y||v.position[2]!=positions.v[i].z)return 8;
        for(unsigned j=0;j<3u;++j)fields=(fields^(uint16_t)v.position[j])*16777619u;
        for(unsigned j=0;j<2u;++j)fields=(fields^(uint16_t)v.uv[j])*16777619u;
        for(unsigned j=0;j<4u;++j)fields=(fields^v.attributes[j])*16777619u;}
    for(unsigned i=0;i<222u;++i){fzx_finish_triangle_t t;if(!fzx_finish_triangle(&guard.pack,i,&t)||t.source_index!=i)return 9;
        if(t.material==0u)++sides;else{fzx_finish_image_t im;++faces;
            if(!fzx_finish_image(&guard.pack,t.image,&im)||im.triangle!=i||im.width!=16u||im.height!=16u)return 10;}}
    fzx_finish_vertex_t v;fzx_finish_triangle_t t;fzx_finish_image_t im;
    if(fzx_finish_vertex(&guard.pack,256,&v)||fzx_finish_triangle(&guard.pack,222,&t)||fzx_finish_image(&guard.pack,98,&im))return 11;
    printf("PASS 256 222 %u %u %08x\n",sides,faces,fields);return 0;
}
