/* Geometry/culling consumer check. Supply privately generated packs locally;
 * the checked-in driver contains no retail geometry or texture data. */
#include "fzx_finish_draw.h"
#include "fzx_results.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void Math_SinTableInit(void);
static uint8_t metadata[FZX_FINISH_METADATA_BYTES];
static fzx_finish_work_t work;
static fzx_finish_quad_t commands[FZX_FINISH_TRIANGLES];
static unsigned emitted;
static bool emit(void *context,const fzx_finish_quad_t *q) {
    (void)context;if(emitted>=FZX_FINISH_TRIANGLES)return false;
    commands[emitted++]=*q;return true;
}
#define CHECK(c) do {if(!(c)){fprintf(stderr,"finish draw line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(int argc,char **argv) {
    CHECK(argc==5);
    FILE *f=fopen(argv[1],"rb");CHECK(f);
    CHECK(fread(metadata,1,sizeof(metadata),f)==sizeof(metadata));CHECK(fgetc(f)==EOF);CHECK(fclose(f)==0);
    fzx_finish_pack_t pack;
    CHECK(fzx_finish_pack_open(&pack,metadata,sizeof(metadata),(uint32_t)strtoul(argv[2],NULL,0),
                              (uint32_t)strtoul(argv[3],NULL,0),(uint32_t)strtoul(argv[4],NULL,0)));
    CHECK(fzx_finish_prepare(&pack,&work));Math_SinTableInit();
    unsigned poses=0,draws=0,maximum=0;double worst=0;uint32_t digest=2166136261u;
    fzx_results_snapshot_t s={0};s.finish.visible=1;s.finish.scale=0.07f;
    for(unsigned position=0;position<3;++position)for(unsigned angle=0;angle<4096;++angle) {
        s.finish.angle=(int32_t)angle;s.finish.y=-(float)(20*position);s.finish.z=(float)(180+20*position);
        int32_t matrix[18];sh2z_view_t view;unsigned count;
        CHECK(fzx_results_finish_matrix(&s,matrix));CHECK(fzx_finish_view(matrix,&view));
        emitted=0;CHECK(fzx_finish_draw(&pack,&view,&work,emit,NULL,&count));CHECK(count==emitted);
        for(unsigned i=0;i<FZX_FINISH_VERTICES;++i) {
            /* Original row-vector Q16 matrix and original final-coordinate
             * truncation, evaluated independently with double precision. */
            const sh2z_pos16_t *p=&work.positions[i];
            double point[3];
            for(unsigned axis=0;axis<3;++axis)point[axis]=
                ((double)p->x*matrix[axis]+(double)p->y*matrix[4+axis]+
                 (double)p->z*matrix[8+axis]+matrix[12+axis])/65536.0;
            CHECK(point[2]<-16.0);
            const double x=160.0-point[0]*((double)matrix[16]/65536.0)*160.0/point[2];
            const double y=112.0+point[1]*((double)matrix[17]/65536.0)*120.0/point[2];
            const double dx=fabs((double)work.projected[i].sx-(double)(int)x);
            const double dy=fabs((double)work.projected[i].sy-(double)(int)y);
            if(dx>worst)worst=dx;if(dy>worst)worst=dy;
            CHECK(dx<=1.0&&dy<=1.0);
        }
        unsigned expected=0;
        for(unsigned i=0;i<FZX_FINISH_TRIANGLES;++i) {
            fzx_finish_triangle_t t;CHECK(fzx_finish_triangle(&pack,i,&t));
            const sh2z_view_vertex_t *a=&work.projected[t.vertex[0]],*b=&work.projected[t.vertex[1]],*c=&work.projected[t.vertex[2]];
            const int64_t area=(int64_t)a->sx*(b->sy-c->sy)+(int64_t)b->sx*(c->sy-a->sy)+(int64_t)c->sx*(a->sy-b->sy);
            if((a->outcode&b->outcode&c->outcode)||area>=0)continue;
            CHECK(expected<count);const fzx_finish_quad_t *q=&commands[expected++];
            CHECK(q->triangle==i&&q->material==t.material&&q->image==t.image);
            CHECK(q->x[0]==a->sx&&q->x[1]==b->sx&&q->x[2]==c->sx&&q->x[3]==c->sx);
            CHECK(q->y[0]==a->sy&&q->y[1]==b->sy&&q->y[2]==c->sy&&q->y[3]==c->sy);
            digest=(digest^q->triangle)*16777619u;
            for(unsigned k=0;k<4;++k){digest=(digest^(uint16_t)q->x[k])*16777619u;digest=(digest^(uint16_t)q->y[k])*16777619u;}
        }
        CHECK(expected==count);++poses;draws+=count;if(count>maximum)maximum=count;
    }
    CHECK(draws>0);
    printf("FINISH draw HOST poses=%u draws=%u max=%u edge=%.0f digest=%08x PASS\n",poses,draws,maximum,worst,digest);
    return 0;
}
