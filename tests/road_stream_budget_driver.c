/* HOST checks of the C3 road command budget guard (FZX_ROAD_STREAM_BUDGET=1)
 * on the synthetic road: a frame within budget is the default draw, byte for
 * byte; one over it is the coarse draw; one still over is the coarse draw with
 * its first (farthest) commands dropped, leaving exactly the budget. */
#include "fzx_road_stream_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if(!(test)){fprintf(stderr,"road stream budget check failed at line %u\n",(unsigned)__LINE__);return 1;} } while(0)
static uint8_t *read_file(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");if(!f||fseek(f,0,SEEK_END))return NULL;
    const long size=ftell(f);if(size<=0||size>200000)return NULL;
    *bytes=(uint32_t)size;rewind(f);uint8_t *data=malloc(*bytes);
    if(!data||fread(data,1,*bytes,f)!=*bytes||fclose(f))return NULL;
    return data;
}
static uint16_t remap[64];static sh2z_pos16_t positions[64];static sh2z_view_vertex_t projected[64];
static fzx_road_stream_image_t images[64];
static fzx_road_stream_command_t normal[512],coarse[512],out[512];
static int draw(fzx_road_stream_draw_work_t *w,const sh2z_view_center_t *c,const RefRoadPlan *plan,unsigned flags,
    fzx_road_stream_command_t *dst,uint32_t *count){
    fzx_road_stream_draw_stats_t stats;
    return fzx_road_stream_draw_center(w,c,plan,1073741824,-8192,flags,dst,512,count,&stats);
}
static int guarded(fzx_road_stream_draw_work_t *w,const sh2z_view_center_t *c,const RefRoadPlan *plan,uint32_t budget,
    uint32_t *count,fzx_road_stream_budget_t *info){
    fzx_road_stream_draw_stats_t stats;memset(out,0xa5,sizeof(out));
    return fzx_road_stream_draw_guarded(w,c,plan,1073741824,-8192,0,out,budget,count,info,&stats);
}
int main(int argc,char **argv){
    CHECK(argc==3);
    uint32_t nb,tb;uint8_t *n=read_file(argv[1],&nb),*t=read_file(argv[2],&tb);CHECK(n&&t);
    fzx_road_stream_pack_t pack;CHECK(fzx_road_stream_open(&pack,n,nb,fzx_road_hash(n,nb),tb));
    fzx_road_stream_draw_work_t work={.remap=remap,.positions=positions,.projected=projected,
        .images=images,.remap_capacity=64,.vertex_capacity=64,.image_capacity=64};
    CHECK(fzx_road_stream_draw_prepare(&work,&pack,t,tb,fzx_road_hash(t,tb)));
    /* A low, close camera looking along the road, so pieces are cut. */
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .translation={0,-60*4096,200*4096},.focal={160*65536,160*65536},
        .center={160*65536,112*65536},.near_plane=16*4096,.far_plane=10000*4096,.clip={12,16,308,224}};
    int32_t eye[3]={0,60*65536,-200*65536};
    RefRoadPlan plan={0};plan.count=1;plan.visible_chunks=pack.count[FZX_RLD_CHUNKS];plan.outer=1;
    plan.fog_start=990;plan.fog[0]=252;plan.fog[1]=192;plan.fog[2]=253;plan.fog[3]=255;
    plan.primitive[0]=255;plan.primitive[3]=255;plan.groups[0].direction=-1;
    sh2z_view_center_t center;CHECK(fzx_view16_from_source_center(&center,&view,eye));
    uint32_t nn=0,nc=0,count=0;fzx_road_stream_budget_t info;
    CHECK(draw(&work,&center,&plan,0,normal,&nn));
    CHECK(draw(&work,&center,&plan,FZX_RS_COARSE,coarse,&nc));
    CHECK(nn>=4&&nc>=2&&nc<nn);   /* this view has thin far quads */
    /* Within budget: the default draw, unchanged, in one pass. */
    for(uint32_t b=nn;b<=nn+2;++b){
        CHECK(guarded(&work,&center,&plan,b,&count,&info));
        CHECK(info.passes==1&&!info.coarse&&!info.dropped&&info.full==nn&&count==nn);
        CHECK(!memcmp(out,normal,nn*sizeof(*out)));
    }
    /* Over it, but the coarse draw fits: the coarse draw. */
    if(nc<nn){
        CHECK(guarded(&work,&center,&plan,nn-1,&count,&info));
        CHECK(info.passes==2&&info.coarse&&!info.dropped&&info.full==nc&&count==nc);
        CHECK(!memcmp(out,coarse,nc*sizeof(*out)));
    }
    /* Still over: the coarse draw without its first commands, exactly the budget. */
    for(uint32_t b=1;b<nc;++b){
        CHECK(guarded(&work,&center,&plan,b,&count,&info));
        CHECK(info.passes==3&&info.coarse&&info.dropped==nc-b&&info.full==nc&&count==b);
        CHECK(!memcmp(out,coarse+(nc-b),b*sizeof(*out)));
        CHECK(out[b].quad==0xa5a5u);   /* nothing written past the budget */
    }
    /* The guard owns the coarse flag; zero budget is refused. */
    {fzx_road_stream_draw_stats_t stats;
     CHECK(!fzx_road_stream_draw_guarded(&work,&center,&plan,1073741824,-8192,FZX_RS_COARSE,out,nn,&count,&info,&stats));
     CHECK(!fzx_road_stream_draw_guarded(&work,&center,&plan,1073741824,-8192,0,out,0,&count,&info,&stats));}
    printf("budget guard: normal %u coarse %u commands\n",nn,nc);
    return 0;
}
