#include "fzx_road_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END))return 2;
    long length=ftell(f);if(length<=0 || length>200000)return 2;
    rewind(f);uint8_t *data=malloc((size_t)length);
    if(!data || fread(data,1,(size_t)length,f)!=(size_t)length || fclose(f))return 2;
    fzx_road_stream_pack_t pack,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));pack=sentinel;
    const uint32_t hash=fzx_road_hash(data,(uint32_t)length)^(uint32_t)strtoul(argv[3],NULL,0);
    const bool ok=fzx_road_stream_open(&pack,data,(uint32_t)length,hash,(uint32_t)strtoul(argv[2],NULL,0));
    if(!ok){int unchanged=!memcmp(&pack,&sentinel,sizeof(pack));free(data);return unchanged?1:3;}
    for(unsigned section=0;section<FZX_RLD_SECTIONS;++section) {
        if(fzx_road_stream_record(&pack,section,pack.count[section]))return 4;
        for(uint32_t i=0;i<pack.count[section];++i)if(!fzx_road_stream_record(&pack,section,i))return 4;
    }
    if(fzx_road_stream_record(NULL,0,0) || fzx_road_stream_record(&pack,FZX_RLD_SECTIONS,0))return 4;
    uint16_t *remap=malloc(pack.count[0]*sizeof(*remap));
    sh2z_pos16_t *positions=malloc(pack.count[0]*sizeof(*positions));
    if(!remap || !positions)return 2;
    RefRoadPlan plan={0};plan.count=1;plan.visible_chunks=pack.count[FZX_RLD_CHUNKS];plan.outer=1;
    plan.groups[0].direction=-1;uint32_t count=0;
    if(!fzx_road_stream_gather(&pack,&plan,remap,pack.count[0],positions,pack.count[0],&count) || !count)return 5;
    for(uint32_t i=0;i<pack.count[0];++i)if(remap[i]!=UINT16_MAX) {
        const uint8_t *v=fzx_road_stream_record(&pack,FZX_RLD_VERTICES,i);const sh2z_pos16_t got=positions[remap[i]];
        if(got.x!=(int16_t)fzx_road_u16(v) || got.y!=(int16_t)fzx_road_u16(v+2) || got.z!=(int16_t)fzx_road_u16(v+4))return 5;
    }
    const uint32_t needed=count;
    if(fzx_road_stream_gather(&pack,&plan,remap,pack.count[0],positions,needed-1,&count) || count)return 5;
    plan.groups[0].direction=1;
    if(!fzx_road_stream_gather(&pack,&plan,remap,pack.count[0],positions,pack.count[0],&count) || count!=needed)return 5;
    plan.visible_chunks++;
    if(fzx_road_stream_gather(&pack,&plan,remap,pack.count[0],positions,pack.count[0],&count) || count)return 5;
    plan.visible_chunks--;plan.groups[0].direction=0;
    if(fzx_road_stream_gather(&pack,&plan,remap,pack.count[0],positions,pack.count[0],&count) || count)return 5;
    free(remap);free(positions);
    printf("%u %u %u %u %u %u %u %u\n",pack.count[0],pack.count[1],pack.count[2],pack.count[3],pack.count[4],pack.count[5],pack.count[6],pack.count[7]);
    free(data);return 0;
}
