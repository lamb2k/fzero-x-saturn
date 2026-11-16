/* HOST census only. road_lod_variant.c is a temporary, mechanically adapted
 * copy of the frozen renderer; it is never linked into the Saturn program. */
#include "road_lod_variant.c"
#include "ref.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned char *read_file(const char *name, unsigned *bytes) {
    FILE *f=fopen(name,"rb");
    if(!f || fseek(f,0,SEEK_END))exit(2);
    long size=ftell(f);
    if(size<=0 || (unsigned long)size>UINT32_MAX)exit(2);
    *bytes=(unsigned)size;rewind(f);
    unsigned char *p=malloc(*bytes);
    if(!p || fread(p,1,*bytes,f)!=*bytes || fclose(f))exit(2);
    return p;
}
static bool count_command(void *context,const fzx_road_tile_quad_t *q) {
    (void)context;(void)q;return true;
}
int main(int argc,char **argv) {
    _Static_assert(sizeof(RefCamera)==252,"Camera ABI");
    _Static_assert(sizeof(RefRoadPlan)==796,"Road plan ABI");
    _Static_assert(sizeof(fzx_view_t)==80,"View ABI");
    const unsigned endian=1;
    if(argc!=8 || *(const unsigned char *)&endian!=1)return 1;
    for(unsigned i=0;i<3;++i) {
        char *end=NULL;long value=strtol(argv[5+i],&end,10);
        if(!end || *end || value<1 || value>1024)return 1;
        census_thresholds[i]=(int)value;
    }
    if(census_thresholds[0]<=census_thresholds[1] || census_thresholds[1]<=census_thresholds[2])return 1;
    unsigned rb,mb;unsigned char *r=read_file(argv[1],&rb),*m=read_file(argv[2],&mb);
    fzx_road_pack_t road;fzx_road_tiles_t tiles;
    if(!fzx_road_pack_open(&road,r,rb,fzx_road_u32(r+20),fzx_road_u32(r+104),false) ||
       !fzx_road_tiles_open(&tiles,&road,m,mb,fzx_road_u32(m+20),fzx_road_hash(m,mb)))return 3;
    FILE *in=fopen(argv[3],"rb"),*out=fopen(argv[4],"wb");
    if(!in || !out)return 4;
    unsigned count=0;RefCamera camera;RefRoadPlan plan;
    while(fread(&camera,1,sizeof(camera),in)==sizeof(camera)) {
        if(fread(&plan,1,sizeof(plan),in)!=sizeof(plan))return 5;
        fzx_view_t view;const float crop[4]={12,16,308,224};
        if(camera.faults || !fzx_view_from_source(&view,camera.view,camera.projection,
            camera.viewport,crop,camera.near_plane,camera.far_plane))return 6;
        int32_t pa=(int32_t)(-camera.projection[2][2]*1073741824.0f);
        int32_t pb=(int32_t)(camera.projection[3][2]*4096.0f);
        uint32_t result[6]={0};
        for(unsigned control=0;control<2;++control) {
            census_forced=control;memset(census_tiers,0,sizeof(census_tiers));
            fzx_road_tile_stats_t stats;
            if(!fzx_road_tiles_draw(&tiles,&view,&plan,pa,pb,65536,count_command,NULL,&stats))return 7;
            result[control]=stats.draws+1; /* Native road clip-setup command. */
            if(!control)memcpy(result+2,census_tiers,sizeof(census_tiers));
        }
        if(fwrite(&view,1,sizeof(view),out)!=sizeof(view) || fwrite(result,1,sizeof(result),out)!=sizeof(result))return 8;
        ++count;
    }
    if(!feof(in) || fclose(in) || fclose(out))return 9;
    free(r);free(m);return count==9907?0:10;
}
