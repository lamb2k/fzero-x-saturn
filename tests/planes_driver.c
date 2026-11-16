#include "fzx_planes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef FZX_PLANES_RETAIL
#include "planes_identity.h"
#endif
static unsigned count;static uint32_t hash;
static bool emit(void *context,const fzx_plane_quad_t *q) {
    if(context)return false;
    assert(q->layer<2 && q->level<4 && q->variant<8);
    assert(q->width==(64u>>q->level) && q->height==(32u>>q->level));
    assert(q->offset>=q->layer*FZX_PLANE_LAYER_BYTES);
    assert(q->offset+q->width*q->height*2u<=((unsigned)q->layer+1)*FZX_PLANE_LAYER_BYTES);
    assert(!(q->offset&7u));
    for(unsigned i=0;i<4;++i)assert(q->x[i]>=-900 && q->x[i]<=900 && q->y[i]>=-900 && q->y[i]<=900);
    const uint8_t *bytes=(const uint8_t *)q;
    for(unsigned i=0;i<sizeof(*q);++i)hash=(hash^bytes[i])*16777619u;
    ++count;return true;
}
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int16_t half(const unsigned char *p){return (int16_t)((unsigned)p[0]*256+p[1]);}
int main(int argc,char **argv) {
#ifdef FZX_PLANES_RETAIL
    const uint16_t *fog=fzx_plane_fog,*mean=fzx_plane_mean;
#else
    const uint16_t fog[3]={31,23,31},mean[3]={15,15,15};
#endif
    fzx_plane_stats_t stats;
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .focal={160*65536,160*65536},.center={160*65536,112*65536},.near_plane=4096,.far_plane=1000*4096,.clip={12,8,308,216}};
    RefBackdrop scene={.count=20};hash=2166136261u;
    if(argc==2) {
        FILE *file=fopen(argv[1],"rb");if(!file)return 20;
        unsigned char bytes[504];unsigned row=0;
        while(fread(bytes,1,sizeof(bytes),file)==sizeof(bytes)) {
            int32_t fields[18];for(unsigned i=0;i<18;++i)fields[i]=(int32_t)word(bytes+i*4);memcpy(&view,fields,sizeof(fields));
            for(unsigned i=0;i<4;++i)view.clip[i]=half(bytes+72+i*2);
            for(unsigned i=0;i<20;++i) {
                const unsigned char *v=bytes+80+104+i*16;
                for(unsigned j=0;j<3;++j)scene.vertices[i].position[j]=half(v+j*2);
                for(unsigned j=0;j<2;++j)scene.vertices[i].uv[j]=half(v+8+j*2);
                memcpy(scene.vertices[i].color,v+12,4);
            }
            count=0;
            bool ok=fzx_planes_draw(&view,&scene,fog,mean,640,emit,NULL,&stats);
            printf("view %u ok %u draws %u tiles %u split %u clipped %u near %u fault %u hash %08x\n",++row,ok,count,stats.tiles,stats.split,stats.clipped,stats.near_adapted,stats.fault,stats.hash);
            if(!ok)return 21;
        }
        fclose(file);printf("planes hash %08x\n",hash);return 0;
    }
    for(unsigned layer=0;layer<2;++layer)for(unsigned i=0;i<8;++i) {
        RefBackdropVertex *v=&scene.vertices[(layer?12:0)+i];
        v->position[0]=(i&1)?200:-200;v->position[1]=layer?80:-40;
        unsigned row=i<4?i/2:1+(i-4)/2;
        v->position[2]=(int16_t)(500-(int)row*400);
        v->uv[0]=(i&1)?2048:0;v->uv[1]=(int16_t)(1024-(int)row*1024);
        memset(v->color,row==1?0:255,4);
    }
    assert(fzx_planes_draw(&view,&scene,fog,mean,640,emit,NULL,&stats));assert(count>4 && count<640);
    unsigned expected=count;uint32_t expected_hash=hash;
    count=0;assert(!fzx_planes_draw(&view,&scene,fog,mean,1,emit,NULL,&stats) && stats.fault==2);
    assert(!fzx_planes_draw(&view,&scene,fog,mean,640,emit,&scene,&stats) && stats.fault==3);
    /* Rolled plane crosses near and coordinate guards: the clipped leaf has
     * five corners. All corners must survive the bounded fan decomposition. */
    view.rotation[0][0]=view.rotation[1][1]=759250125;
    view.rotation[0][1]=759250125;view.rotation[1][0]=-759250125;
    view.translation[0]=-200*4096;view.translation[2]=-300*4096;
    count=0;assert(fzx_planes_draw(&view,&scene,fog,mean,640,emit,NULL,&stats));
    assert(count>0 && stats.near_adapted>0);
    for(unsigned i=0;i<20;++i)for(unsigned j=0;j<2;++j)scene.vertices[i].uv[j]/=64;
    count=0;assert(fzx_planes_draw(&view,&scene,fog,mean,640,emit,NULL,&stats));
    assert(count>0);
    scene.count=19;assert(!fzx_planes_draw(&view,&scene,fog,mean,640,emit,NULL,&stats) && stats.fault==1);
    printf("planes fixture %u native draws hash %08x, capacity/callback/schema and clipped-fan/wide-UV controls PASS\n",expected,expected_hash);
}
