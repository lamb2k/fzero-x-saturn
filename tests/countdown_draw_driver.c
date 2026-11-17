#include "fzx_countdown_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"countdown draw check %u\n",(unsigned)__LINE__);return 2;}}while(0)
static unsigned u16(const uint8_t *p){return (unsigned)p[0]*256u+p[1];}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)*65536u+u16(p+2);}
static uint32_t hash(const uint8_t *p,unsigned n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static struct {uint32_t before;fzx_countdown_draw_work_t data;uint32_t after;} work;
static struct {uint32_t before;fzx_countdown_command_t data[32];uint32_t after;} output;
int main(int argc,char **argv){
    CHECK(argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f);
    uint8_t data[2352];CHECK(fread(data,1,sizeof(data),f)==sizeof(data)&&fgetc(f)==EOF&&!fclose(f));
    const uint32_t digest=hash(data,sizeof(data));fzx_countdown_pack_t pack;
    CHECK(fzx_countdown_pack_open(&pack,data,sizeof(data),digest,u32(data+12),u32(data+16)));
    work.before=work.after=output.before=output.after=0x43bc917du;
    CHECK(!fzx_countdown_draw_prepare(&work.data,NULL));
    CHECK(fzx_countdown_draw_prepare(&work.data,&pack));
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .translation={-16*4096,-16*4096,500*4096},.focal={160*65536,160*65536},
        .center={160*65536,112*65536},.near_plane=16*4096,.far_plane=10000*4096,.clip={12,16,308,224}};
    RefCountdown state={0};for(unsigned i=0;i<4;++i)state.matrix[i*5]=65536;
    fzx_countdown_draw_stats_t stats;uint32_t count=99;FILE *capture=fopen(argv[2],"wb");CHECK(capture);
    unsigned shown=0,selected=0;
    for(int timer=0;timer<=460;++timer){
        state.timer=timer;const RefCountdown saved=state;const fzx_view_t saved_view=view;
        CHECK(fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats));
        CHECK(!stats.fault&&count==(timer?23u:0u));
        CHECK(!memcmp(&saved,&state,sizeof(saved))&&!memcmp(&view,&saved_view,sizeof(view)));
        if(timer){++shown;
            const unsigned panel=timer>220?0u:timer>160?1u:timer>100?2u:timer>40?3u:4u;
            const unsigned mouth=(timer>=210&&timer<=220)||(timer>=150&&timer<=160)||(timer>=90&&timer<=100)||timer<=40;
            CHECK(stats.phase.panel==panel&&stats.phase.mouth_open==(mouth!=0));
            for(unsigned q=0;q<23;++q){const fzx_countdown_command_t *out=output.data+q;
                const unsigned variant=q<20?0:q<22?mouth:panel?panel-1:0;
                const uint8_t *image=data+1904+(u16(data+1536+q*16+8)+variant)*16;
                CHECK(out->quad==q&&out->texture==u32(image+4)&&out->width==u16(image)&&out->height==u16(image+2));
                CHECK(out->solid==(q==22&&!panel)&&out->color==0x8000&&!out->reserved);
                for(unsigned k=0;k<4;++k){const sh2z_view_vertex_t v=work.data.projected[u16(data+1536+q*16+k*2)];
                    CHECK(out->x[k]==v.sx&&out->y[k]==v.sy);}
                ++selected;
            }
        }
        CHECK(fwrite(&count,sizeof(count),1,capture)==1&&fwrite(output.data,sizeof(output.data[0]),count,capture)==count);
    }
    CHECK(!fclose(capture));state.timer=220;
    struct {fzx_countdown_command_t command;uint32_t guard;} small={.guard=0xb175ca33u};
    CHECK(!fzx_countdown_draw(&work.data,&view,&state,&small.command,1,&count,&stats));
    CHECK(!count&&stats.fault==FZX_COUNTDOWN_DRAW_CAPACITY&&small.guard==0xb175ca33u);
    state.matrix[0]=-65536;state.matrix[10]=-65536;
    CHECK(fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats)&&!count&&stats.culled==23);
    state.matrix[0]=state.matrix[10]=65536;
    view.translation[0]=10000*4096;
    CHECK(fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats)&&!count&&stats.culled==23);
    view.translation[0]=-16*4096;
    /* Authored front-facing geometry may flatten after pixel rounding. It
     * still retains its command/chart instead of using rounded backfaces. */
    const sh2z_pos16_t thin[4]={{0,0,0},{40,21,0},{39,23,0},{-1,2,0}};
    for(unsigned i=0;i<4;++i)work.data.positions[i]=thin[i];
    view.translation[2]=2000*4096;
    CHECK(fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats));
    CHECK(count==23&&output.data[0].quad==0);
    CHECK(output.data[0].x[0]==output.data[0].x[3]&&output.data[0].y[0]==output.data[0].y[3]&&
          output.data[0].x[1]==output.data[0].x[2]&&output.data[0].y[1]==output.data[0].y[2]);
    CHECK(fzx_countdown_draw_prepare(&work.data,&pack));view.translation[2]=500*4096;
    /* A visible near-plane crossing cannot keep its full textured rectangle. */
    work.data.positions[0].z=-500;
    CHECK(!fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats));
    CHECK(!count&&stats.fault==FZX_COUNTDOWN_DRAW_TEXTURE_CLIP&&stats.error_quad==0);
    CHECK(fzx_countdown_draw_prepare(&work.data,&pack));
    /* The source black panel may be geometrically clipped as a solid. */
    state.timer=300;work.data.positions[88].z=-500;
    CHECK(fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats));
    CHECK(stats.clipped_solids==1&&count>=23);
    for(unsigned i=0;i<count;++i)for(unsigned k=0;k<4;++k)
        CHECK(output.data[i].x[k]>=-1024&&output.data[i].x[k]<=1023&&output.data[i].y[k]>=-1024&&output.data[i].y[k]<=1023);
    CHECK(fzx_countdown_draw_prepare(&work.data,&pack));
    /* Full-screen texture whose corners exceed signed VDP1 coordinates. */
    state.timer=220;view.focal[0]=view.focal[1]=2000*65536;view.translation[2]=16*4096;
    CHECK(!fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats));
    CHECK(!count&&stats.fault==FZX_COUNTDOWN_DRAW_TEXTURE_CLIP);
    state.faults=1;CHECK(!fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats)&&!count);
    state.faults=0;state.timer=-1;CHECK(!fzx_countdown_draw(&work.data,&view,&state,output.data,32,&count,&stats)&&!count);
    CHECK(hash(data,sizeof(data))==digest);
    CHECK(work.before==0x43bc917du&&work.after==0x43bc917du&&output.before==0x43bc917du&&output.after==0x43bc917du);
    printf("countdown draw timers=461 shown=%u chart_selections=%u; clipping rejects unsafe textures, source black clips; bounds/order/phase/capacity PASS\n",shown,selected);
    return 0;
}
