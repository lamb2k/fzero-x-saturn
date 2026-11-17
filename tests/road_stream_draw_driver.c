#include "fzx_road_stream_draw.h"
#include "fzx_road_tiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if(!(test)){fprintf(stderr,"road stream draw check failed at line %u\n",(unsigned)__LINE__);return 1;} } while(0)
static uint8_t *read_file(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");if(!f||fseek(f,0,SEEK_END))return NULL;
    const long size=ftell(f);if(size<=0||size>200000)return NULL;
    *bytes=(uint32_t)size;rewind(f);uint8_t *data=malloc(*bytes);
    if(!data||fread(data,1,*bytes,f)!=*bytes||fclose(f))return NULL;
    return data;
}
static struct {uint32_t before;uint16_t rows[32];uint32_t after;} remap;
static struct {uint32_t before;sh2z_pos16_t rows[32];uint32_t after;} input;
static struct {uint32_t before;sh2z_view_vertex_t rows[32];uint32_t after;} projected;
static struct {uint32_t before;fzx_road_stream_image_t rows[32];uint32_t after;} images;
static struct {uint32_t before;fzx_road_stream_command_t rows[512];uint32_t after;} output;
static fzx_road_tile_quad_t original[512];
static unsigned original_count;
static bool emit(void *ctx,const fzx_road_tile_quad_t *q){
    (void)ctx;if(original_count==512)return false;original[original_count++]=*q;return true;
}
static int fog_exact(int32_t z,int32_t pa,int32_t pb,int start){
    const int32_t fm=128000/(1000-start),fo=(500-start)*256/(1000-start);
    int64_t ratio=pa+(int64_t)pb*INT64_C(1073741824)/(z/4);
    if(ratio< -INT64_C(1073741824))ratio= -INT64_C(1073741824);
    if(ratio>INT64_C(1073741824))ratio=INT64_C(1073741824);
    const int64_t value=ratio*fm+(int64_t)fo*INT64_C(1073741824);
    return value<=0?0:value>=255*INT64_C(1073741824)?255:(int)(value/INT64_C(1073741824));
}
static unsigned table_value(const fzx_road_stream_draw_work_t *w,uint32_t z){
    unsigned result=0;for(unsigned i=1;i<256;++i)if(z>=w->fog_depth[i])result=i;return result;
}
int main(int argc,char **argv){
    CHECK(argc==7);
    uint32_t nb,tb,rb,mb;uint8_t *n=read_file(argv[1],&nb),*t=read_file(argv[2],&tb);
    uint8_t *r=read_file(argv[3],&rb),*m=read_file(argv[4],&mb);CHECK(n&&t&&r&&m);
    fzx_road_stream_pack_t pack;fzx_road_pack_t road;fzx_road_tiles_t tiles;
    CHECK(fzx_road_stream_open(&pack,n,nb,fzx_road_hash(n,nb),tb));
    CHECK(fzx_road_pack_open(&road,r,rb,fzx_road_u32(r+20),fzx_road_u32(r+104),true));
    CHECK(fzx_road_tiles_open(&tiles,&road,m,mb,fzx_road_u32(m+20),fzx_road_hash(m,mb)));
    remap.before=remap.after=input.before=input.after=projected.before=projected.after=
        images.before=images.after=output.before=output.after=0xa53c719du;
    fzx_road_stream_draw_work_t work={.remap=remap.rows,.positions=input.rows,.projected=projected.rows,
        .images=images.rows,.remap_capacity=32,.vertex_capacity=32,.image_capacity=32};
    CHECK(!fzx_road_stream_draw_prepare(&work,&pack,t,tb,fzx_road_hash(t,tb)^1u));
    CHECK(!fzx_road_stream_draw_prepare(&work,&pack,t,tb-1,fzx_road_hash(t,tb)));
    CHECK(fzx_road_stream_draw_prepare(&work,&pack,t,tb,fzx_road_hash(t,tb)));
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .translation={0,-600*4096,1000*4096},.focal={160*65536,160*65536},
        .center={160*65536,112*65536},.near_plane=16*4096,.far_plane=10000*4096,.clip={12,16,308,224}};
    int32_t eye[3]={0,600*65536,-1000*65536};
    RefRoadPlan plan={0};plan.count=1;plan.visible_chunks=pack.count[FZX_RLD_CHUNKS];plan.outer=1;
    plan.fog_start=990;plan.fog[0]=252;plan.fog[1]=192;plan.fog[2]=253;plan.fog[3]=255;
    plan.primitive[0]=255;plan.primitive[3]=255;plan.groups[0].direction=-1;
    const fzx_view_t saved_view=view;const RefRoadPlan saved_plan=plan;int32_t saved_eye[3];memcpy(saved_eye,eye,sizeof(eye));
    fzx_road_stream_draw_stats_t stats;uint32_t count=999;
    if(!strcmp(argv[5],"unaligned")){
        CHECK(!fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,FZX_RS_FORCE_FIRST,output.rows,512,&count,&stats));
        CHECK(!count&&stats.fault==FZX_RS_TEXTURE_ALIGNMENT&&stats.unaligned);
        CHECK(fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,FZX_RS_FORCE_FIRST|FZX_RS_AUDIT_UNALIGNED,output.rows,512,&count,&stats));
        CHECK(count&&stats.unaligned);puts("unaligned CLUT starts reject; explicit HOST audit preserves byte addresses PASS");
    }else{
        CHECK(fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,0,output.rows,512,&count,&stats));
        const unsigned adaptive=count;CHECK(adaptive&&stats.positions==16&&stats.fog_updates==1);
        CHECK(fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,FZX_RS_FORCE_FIRST,output.rows,512,&count,&stats));
        const unsigned forced=count;CHECK(forced>adaptive&&stats.fog_updates==0);
        fzx_road_tile_stats_t old_stats;
        CHECK(fzx_road_tiles_draw(&tiles,&view,&plan,1073741824,-8192,512,emit,NULL,&old_stats));
        CHECK(count==original_count);
        unsigned max_error=0;
        for(unsigned i=0;i<count;++i){
            CHECK((output.rows[i].material.width==original[i].width||output.rows[i].material.width==original[i].width*2u)&&output.rows[i].material.height==original[i].height);
            for(unsigned j=0;j<4;++j){int dx=output.rows[i].x[j]-original[i].x[j],dy=output.rows[i].y[j]-original[i].y[j];
                if(dx<0)dx=-dx;
                if(dy<0)dy=-dy;
                CHECK(dx<=1&&dy<=1);if((unsigned)dx>max_error)max_error=(unsigned)dx;if((unsigned)dy>max_error)max_error=(unsigned)dy;}
        }
        FILE *capture=fopen(argv[6],"wb");CHECK(capture);
        CHECK(fwrite(&count,4,1,capture)==1&&fwrite(output.rows,sizeof(output.rows[0]),count,capture)==count&&
            fwrite(original,sizeof(original[0]),count,capture)==count&&fclose(capture)==0);
        struct {fzx_road_stream_command_t row;uint32_t guard;} tiny={.guard=0x3a9517cdu};
        CHECK(!fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,FZX_RS_FORCE_FIRST,&tiny.row,1,&count,&stats));
        CHECK(!count&&stats.fault==FZX_RS_CAPACITY&&tiny.guard==0x3a9517cdu);
        work.vertex_capacity=15;
        CHECK(!fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,0,output.rows,512,&count,&stats));
        CHECK(!count&&stats.fault==FZX_RS_GATHER);work.vertex_capacity=32;
        CHECK(!memcmp(&view,&saved_view,sizeof(view))&&!memcmp(&plan,&saved_plan,sizeof(plan))&&!memcmp(eye,saved_eye,sizeof(eye)));
        /* Table thresholds must reproduce the frozen integer fog equation,
         * including both sides of every threshold and several source settings. */
        const int starts[4]={0,500,990,996};
        for(unsigned s=0;s<4;++s){plan.fog_start=starts[s];
            CHECK(fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-81920,0,output.rows,512,&count,&stats));
            for(unsigned a=0;a<256;++a)for(int delta=-1;delta<=1;++delta){
                const int64_t z=(int64_t)work.fog_depth[a]+delta;
                if(z<view.near_plane*4||z>view.far_plane*4)continue;
                CHECK(table_value(&work,(uint32_t)z)==(unsigned)fog_exact((int32_t)z,1073741824,-81920,plan.fog_start));}
        }
        plan.fog_start=0;view.translation[1]=-10*4096;view.translation[2]=8*4096;eye[1]=10*65536;eye[2]=-8*65536;
        CHECK(fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,FZX_RS_FORCE_FIRST,output.rows,512,&count,&stats));
        CHECK(count&&stats.clipped);const unsigned clipped=stats.clipped;
        for(unsigned i=0;i<count;++i){CHECK(!output.rows[i].solid);for(unsigned j=0;j<4;++j)
            CHECK(output.rows[i].x[j]>=-900&&output.rows[i].x[j]<=900&&output.rows[i].y[j]>=-900&&output.rows[i].y[j]<=900);}
        const unsigned sample=count-1;const uint16_t shade=output.rows[sample].shade[0];
        plan.fog[0]=0;plan.fog[1]=0;plan.fog[2]=0;
        CHECK(fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,FZX_RS_FORCE_FIRST,output.rows,512,&count,&stats));
        CHECK(output.rows[sample].shade[0]!=shade&&!stats.fog_updates);
        plan.fog_start=1000;CHECK(!fzx_road_stream_draw(&work,&view,eye,&plan,1073741824,-8192,0,output.rows,512,&count,&stats)&&!count);
        printf("road stream array PASS adaptive %u forced %u max-edge %u px clipped %u; exact fog thresholds and source immutability PASS\n",adaptive,forced,max_error,clipped);
    }
    CHECK(remap.before==0xa53c719du&&remap.after==0xa53c719du&&input.before==0xa53c719du&&input.after==0xa53c719du&&
        projected.before==0xa53c719du&&projected.after==0xa53c719du&&images.before==0xa53c719du&&images.after==0xa53c719du&&
        output.before==0xa53c719du&&output.after==0xa53c719du);
    free(n);free(t);free(r);free(m);return 0;
}
