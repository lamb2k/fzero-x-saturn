#include "countdown_draw_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"countdown native check %u\n",(unsigned)__LINE__);return 2;}}while(0)
static unsigned u16(const uint8_t *p){return (unsigned)p[0]*256u+p[1];}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)*65536u+u16(p+2);}
static uint32_t hash(const uint8_t *p,unsigned n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static struct {uint32_t before;fzx_countdown_native_work_t data;uint32_t after;} work;
static struct {uint32_t before;vdp1_cmdt_t data[FZX_COUNTDOWN_NATIVE_COMMANDS];uint32_t after;} output;
static vdp1_cmdt_t saved[FZX_COUNTDOWN_NATIVE_COMMANDS];
static fzx_countdown_native_stats_t stats;
static uint32_t count;
static bool reject(const fzx_view_t *view,const RefCountdown *state,const fzx_countdown_phase_t *phase,
    uintptr_t base,uint32_t bytes,uint32_t capacity,unsigned fault){
    memset(output.data,0xa5,sizeof(output.data));memcpy(saved,output.data,sizeof(saved));count=99;
    return !fzx_countdown_draw_native(&work.data,view,state,phase,(void *)base,bytes,output.data,capacity,&count,&stats)&&
        !count&&stats.fault==fault&&!memcmp(output.data,saved,sizeof(saved));
}
int main(int argc,char **argv){
    CHECK(argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f);uint8_t data[2352];
    CHECK(fread(data,1,sizeof(data),f)==sizeof(data)&&fgetc(f)==EOF&&!fclose(f));
    const uint32_t digest=hash(data,sizeof(data)),bytes=u32(data+12);fzx_countdown_pack_t pack;
    CHECK(fzx_countdown_pack_open(&pack,data,sizeof(data),digest,bytes,u32(data+16)));
    work.before=work.after=output.before=output.after=0x76bc913du;
    CHECK(!fzx_countdown_draw_native_prepare(&work.data,NULL));
    CHECK(fzx_countdown_draw_native_prepare(&work.data,&pack));
    const uintptr_t base=0x25c04000u;
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .translation={-16*4096,-16*4096,500*4096},.focal={160*65536,160*65536},
        .center={160*65536,112*65536},.near_plane=16*4096,.far_plane=10000*4096,.clip={12,16,308,224}};
    RefCountdown state={0};for(unsigned i=0;i<4;++i)state.matrix[i*5]=65536;
    fzx_countdown_phase_t phase;unsigned charts=0,solids=0;
    FILE *capture=fopen(argv[2],"wb");CHECK(capture);
    for(int timer=0;timer<=460;++timer){
        state.timer=timer;CHECK(fzx_countdown_phase(timer,&phase));
        const RefCountdown old_state=state;const fzx_view_t old_view=view;const fzx_countdown_phase_t old_phase=phase;
        memset(output.data,0xa5,sizeof(output.data));memcpy(saved,output.data,sizeof(saved));
        CHECK(fzx_countdown_draw_native(&work.data,&view,&state,&phase,(void *)base,bytes,
            output.data,FZX_COUNTDOWN_NATIVE_COMMANDS,&count,&stats));
        CHECK(!stats.fault&&count==(timer?24u:0u)&&count==stats.commands);
        CHECK(!memcmp(&state,&old_state,sizeof(state))&&!memcmp(&view,&old_view,sizeof(view))&&!memcmp(&phase,&old_phase,sizeof(phase)));
        if(!timer)CHECK(!memcmp(output.data,saved,sizeof(saved)));
        else {
            CHECK(output.data[0].kind==8&&output.data[0].cmd_xa==12&&output.data[0].cmd_ya==16&&
                output.data[0].cmd_xc==307&&output.data[0].cmd_yc==223);
            for(unsigned i=1;i<count;++i){const vdp1_cmdt_t *out=output.data+i;const fzx_countdown_command_t *q=work.data.pending+i-1;
                CHECK(out->kind==(q->solid?4u:2u)&&out->mode.end_code_disable==1&&out->mode.trans_pixel_disable==(q->solid?1u:0u));
                CHECK(out->mode.user_clipping_mode==2&&out->mode.color_mode==VDP1_CMDT_CM_RGB_32768&&out->mode.cc_mode==VDP1_CMDT_CC_REPLACE&&!out->cmd_grda);
                if(q->solid){CHECK(out->cmd_colr==0x8000&&!out->texture&&!out->width&&!out->height);++solids;}
                else {CHECK(out->texture==base+q->texture&&out->width==q->width&&out->height==q->height);++charts;}
                CHECK(out->cmd_xa==q->x[0]&&out->cmd_ya==q->y[0]&&out->cmd_xb==q->x[1]&&out->cmd_yb==q->y[1]&&
                    out->cmd_xc==q->x[2]&&out->cmd_yc==q->y[2]&&out->cmd_xd==q->x[3]&&out->cmd_yd==q->y[3]);
            }
            CHECK(!memcmp(output.data+count,saved+count,(FZX_COUNTDOWN_NATIVE_COMMANDS-count)*sizeof(saved[0])));
        }
        CHECK(fwrite(&count,sizeof(count),1,capture)==1&&fwrite(output.data,sizeof(output.data[0]),count,capture)==count);
    }
    CHECK(!fclose(capture));state.timer=220;CHECK(fzx_countdown_phase(state.timer,&phase));
    CHECK(reject(&view,&state,&phase,base+1,bytes,30,FZX_COUNTDOWN_NATIVE_VRAM));
    CHECK(reject(&view,&state,&phase,base,bytes-1,30,FZX_COUNTDOWN_NATIVE_VRAM));
    CHECK(reject(&view,&state,&phase,0x25c7fff8u,bytes,30,FZX_COUNTDOWN_NATIVE_VRAM));
    CHECK(reject(&view,&state,&phase,UINTPTR_MAX-7u,bytes,30,FZX_COUNTDOWN_NATIVE_VRAM));
    CHECK(reject(&view,&state,&phase,base,bytes,23,FZX_COUNTDOWN_NATIVE_CAPACITY));
    CHECK(reject(&view,&state,&phase,base,bytes,0,FZX_COUNTDOWN_NATIVE_CAPACITY));
    phase.panel=4;CHECK(reject(&view,&state,&phase,base,bytes,30,FZX_COUNTDOWN_NATIVE_PHASE));
    CHECK(fzx_countdown_phase(state.timer,&phase));phase.mouth_open=false;
    CHECK(reject(&view,&state,&phase,base,bytes,30,FZX_COUNTDOWN_NATIVE_PHASE));
    CHECK(fzx_countdown_phase(state.timer,&phase));state.faults=1;
    CHECK(reject(&view,&state,&phase,base,bytes,30,FZX_COUNTDOWN_NATIVE_BUILD));state.faults=0;
    work.data.draw.positions[0].z=-500;
    CHECK(reject(&view,&state,&phase,base,bytes,30,FZX_COUNTDOWN_NATIVE_BUILD));
    CHECK(stats.draw.fault==FZX_COUNTDOWN_DRAW_TEXTURE_CLIP&&stats.draw.error_quad==0);
    CHECK(fzx_countdown_draw_native_prepare(&work.data,&pack));
    state.timer=300;CHECK(fzx_countdown_phase(state.timer,&phase));work.data.draw.positions[88].z=-500;
    CHECK(fzx_countdown_draw_native(&work.data,&view,&state,&phase,(void *)base,bytes,output.data,30,&count,&stats));
    CHECK(stats.draw.clipped_solids==1&&count>=24&&count<=30);
    for(unsigned i=23;i<count;++i)CHECK(output.data[i].kind==4&&output.data[i].cmd_colr==0x8000&&output.data[i].mode.trans_pixel_disable==1);
    CHECK(hash(data,sizeof(data))==digest);
    CHECK(work.before==0x76bc913du&&work.after==0x76bc913du&&output.before==0x76bc913du&&output.after==0x76bc913du);
    printf("countdown native API recorder: timers461 charts%u solids%u; phase/span/capacity/fail-atomic/mode/clip PASS; no SDK or hardware layout claim\n",charts,solids);
    return 0;
}
