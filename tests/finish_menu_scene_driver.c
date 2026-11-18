#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdbool.h>
static unsigned blank_reads;
static bool fixture_blank=true,leave_blank;
static bool vdp2_tvmd_vblank_in(void){return fixture_blank&&!(leave_blank&&blank_reads++);}
/* HOST only: actual scene TU, synthetic collaborators and mapped fake MMIO.
 * Neither Saturn raster/palette timing nor source menu semantics are proved. */
#include "records_scene_under_test.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned menu_begins,menu_inputs,menu_steps,menu_commits,menu_draws,record_steps,uploads;
static bool request_action,ghost_absent=true,upload_ok=true;
static uint32_t current_frame;
bool fzx_race_workspace_guards(void){return true;}
bool fzx_records_begin(fzx_records_state_t *s,const fzx_results_state_t *r){
    memset(s,0,sizeof(*s));s->initialized=1;s->snapshot.records=r->records;return true;
}
bool ref_records_input(fzx_records_input_t *o,uint32_t f,bool c){
    *o=(fzx_records_input_t){.frame=f,.connected=c};return true;
}
bool fzx_records_step(fzx_records_state_t *s,const fzx_records_input_t *i){
    ++record_steps;s->snapshot.frame=i->frame;s->snapshot.tick++;
    s->snapshot.phase=FZX_RECORDS_FINISH_MENU_READY;return true;
}
bool fzx_records_snapshot(const fzx_records_state_t *s,fzx_records_snapshot_t *o){*o=s->snapshot;return true;}
bool ref_records_commit_rng(const fzx_records_input_t *i,const fzx_records_snapshot_t *s){(void)i;(void)s;return false;}
bool fzx_records_draw_capture(const fzx_records_snapshot_t *s,fzx_records_draw_snapshot_t *o){
    memset(o,0,sizeof(*o));o->frame=s->frame;o->tick=s->tick;return true;
}
bool ref_finish_menu_no_recorded_ghost(void){return ghost_absent;}
bool fzx_finish_menu_begin(fzx_finish_menu_state_t *s,const fzx_records_snapshot_t *r,bool absent){
    ++menu_begins;if(!absent||r->frame!=current_frame||r->phase!=FZX_RECORDS_FINISH_MENU_READY)return false;
    memset(s,0,sizeof(*s));s->initialized=1;return true;
}
bool ref_finish_menu_input(fzx_finish_menu_input_t *i,uint32_t f,bool c){
    ++menu_inputs;*i=(fzx_finish_menu_input_t){.frame=f,.connected=c,.seed1=11,.mask1=12,.seed2=21,.mask2=22};return true;
}
bool fzx_finish_menu_step(fzx_finish_menu_state_t *s,const fzx_finish_menu_input_t *i){
    ++menu_steps;if(!s->initialized||s->snapshot.action||i->frame!=current_frame)return false;
    s->snapshot.input=*i;++s->snapshot.tick;++s->snapshot.selection;
    s->snapshot.seed1=31;s->snapshot.mask1=32;s->snapshot.seed2=41;s->snapshot.mask2=42;
    s->snapshot.rng1_calls=1;s->snapshot.rng2_calls=1;
    s->snapshot.action=request_action?FZX_FINISH_MENU_RETRY:0;return true;
}
bool fzx_finish_menu_snapshot(const fzx_finish_menu_state_t *s,fzx_finish_menu_snapshot_t *o){*o=s->snapshot;return true;}
bool ref_finish_menu_commit_rng(const fzx_finish_menu_input_t *i,const fzx_finish_menu_snapshot_t *s){
    ++menu_commits;return i->frame==current_frame&&i->seed1==11&&i->mask1==12&&i->seed2==21&&i->mask2==22&&
        s->seed1==31&&s->mask1==32&&s->seed2==41&&s->mask2==42&&s->rng1_calls==1&&s->rng2_calls==1;
}
bool fzx_finish_menu_draw_capture(const fzx_finish_menu_snapshot_t *s,fzx_finish_menu_draw_snapshot_t *o){
    ++menu_draws;if(menu_commits!=menu_steps)return false;
    memset(o,0,sizeof(*o));o->frame=s->input.frame;o->tick=s->tick;return true;
}
bool fzx_finish_menu_palette_upload(const fzx_finish_menu_palette_t *p,volatile uint16_t *c,unsigned n){
    ++uploads;if(!upload_ok||n!=1024||c!=(volatile uint16_t *)0x25f00000u)return false;
    c[256]=p->words[0][0];return true;
}
static void seed(void){
    scene=(scene_t *)(uintptr_t)0x00200020u;memset(scene,0,sizeof(*scene));
    fzx_records_low_start=0x00200020u;
    fzx_records_low_end=(uint32_t)(((uintptr_t)scene+sizeof(*scene)+31u)&~(uintptr_t)31u);
    fzx_records_cart_start=0x04000020u;fzx_records_cart_end=0x04000120u;
    guards(fzx_records_low_start,fzx_records_low_end);guards(fzx_records_cart_start,fzx_records_cart_end);
    fzx_source_records=&scene->snapshot;fzx_source_records_draw=&scene->draw;
    fzx_source_finish_menu=&scene->menu_snapshot;fzx_source_finish_menu_draw=&scene->menu_draw;
    fzx_records_status=FZX_RECORDS_READY;fzx_records_active=0;fzx_records_busy=0;
    fzx_finish_menu_pending=fzx_finish_menu_presented=fzx_finish_menu_palette_commits=0;
    menu_begins=menu_inputs=menu_steps=menu_commits=menu_draws=record_steps=uploads=0;
    request_action=false;ghost_absent=upload_ok=fixture_blank=true;leave_blank=false;blank_reads=0;
}
static void pending(void){
    scene->menu_active=true;scene->menu_palette.tick=7;scene->menu_palette.words[0][0]=0x1234;
    fzx_finish_menu_pending=7;
    volatile uint16_t *r=(volatile uint16_t *)0x25f80000u;
    r[0x0e/2]=0x031b;r[0xe0/2]=0x1520;r[0xf0/2]=0x0506;r[0xe6/2]=0;
    *(volatile uint16_t *)0x25f00200u=0;
}
int main(void){
    const uintptr_t addresses[]={0x00200000u,0x04000000u,0x25f00000u,0x25f80000u};
    for(unsigned i=0;i<4;++i)CHECK(mmap((void *)addresses[i],i?4096u:0x100000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    seed();fzx_results_state_t r={0};r.phase=FZX_RESULTS_RECORDS_REQUEST;
    for(current_frame=100;current_frame<104;++current_frame){
        CHECK(fzx_records_tick(&r,current_frame,current_frame!=102));
        CHECK(menu_begins==1&&record_steps==1&&menu_steps==current_frame-99&&menu_inputs==menu_steps);
        CHECK(menu_commits==menu_steps&&menu_draws==menu_steps);
        CHECK(scene->menu_snapshot.selection==(int32_t)menu_steps);
        CHECK(scene->menu_draw.frame==current_frame&&scene->draw.frame==current_frame);
        CHECK(scene->snapshot.frame==100&&scene->state.snapshot.frame==100);
        CHECK(scene->menu_snapshot.input.connected==(current_frame!=102));
    }
    request_action=true;CHECK(fzx_records_tick(&r,current_frame,true));
    CHECK(scene->menu_snapshot.action==FZX_FINISH_MENU_RETRY&&menu_commits==5);
    const fzx_finish_menu_snapshot_t held=scene->menu_snapshot;
    for(++current_frame;current_frame<120;++current_frame){
        CHECK(fzx_records_tick(&r,current_frame,true));
        CHECK(!memcmp(&held,&scene->menu_snapshot,sizeof(held))&&menu_steps==5&&menu_commits==5&&menu_draws==5);
        CHECK(scene->draw.frame==current_frame&&scene->snapshot.frame==100);
    }
    pending();scene->menu_borrowed=true;fzx_finish_menu_command_count=4;fzx_finish_menu_command_start=8;
    CHECK(fzx_records_restart());CHECK(!scene->menu_active&&!scene->menu_borrowed&&!fzx_finish_menu_pending);
    CHECK(!scene->menu_state.initialized&&!scene->menu_snapshot.tick&&!scene->menu_draw.tick);
    CHECK(!fzx_finish_menu_command_count&&!fzx_finish_menu_command_start);
    fzx_records_finish_menu_present();CHECK(!uploads&&!fzx_finish_menu_palette_commits);
    seed();ghost_absent=false;CHECK(!fzx_records_tick(&r,current_frame,true));
    CHECK(fzx_records_status==FZX_RECORDS_STATE&&!menu_inputs&&!menu_commits);
    seed();pending();fzx_records_finish_menu_present();
    CHECK(fzx_records_status==FZX_RECORDS_READY&&uploads==1&&fzx_finish_menu_presented==7);
    CHECK(!fzx_finish_menu_pending&&fzx_finish_menu_palette_commits==1&&fzx_finish_menu_blanking==3);
    CHECK(*(volatile uint16_t *)0x25f00200u==0x1234);
    fzx_records_finish_menu_present();CHECK(uploads==1&&fzx_finish_menu_palette_commits==1);
    for(unsigned bad=0;bad<9;++bad){
        seed();pending();volatile uint16_t *regs=(volatile uint16_t *)0x25f80000u;
        switch(bad){
        case 0:fixture_blank=false;break;
        case 1:scene->menu_palette.tick=6;break;
        case 2:regs[0x0e/2]=0x1000;break;
        case 3:regs[0xe0/2]=0;break;
        case 4:regs[0xf0/2]=0;break;
        case 5:regs[0xe6/2]=0x10;break;
        case 6:scene->menu_active=false;break;
        case 7:*(uint32_t *)(uintptr_t)fzx_records_low_end=0;break;
        case 8:upload_ok=false;break;
        }
        fzx_records_finish_menu_present();
        CHECK(fzx_records_status==FZX_RECORDS_DRAW&&!fzx_finish_menu_pending&&!fzx_finish_menu_palette_commits);
        CHECK(uploads==(bad==8?1u:0u)&&!fzx_finish_menu_presented);
        CHECK(!*(volatile uint16_t *)0x25f00200u);
    }
    seed();pending();leave_blank=true;fzx_records_finish_menu_present();
    CHECK(fzx_records_status==FZX_RECORDS_DRAW&&fzx_finish_menu_blanking==1&&uploads==1);
    puts("FINISH_MENU_SCENE_HOST_PASS same-frame navigation dual-RNG freeze backdrop restart palette MMIO rejection");return 0;
}
