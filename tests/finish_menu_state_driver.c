#include "fzx_finish_menu.h"
#include "finish_menu_source_ids.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"finish menu check line %d\n",__LINE__);return 1;}}while(0)
static fzx_finish_menu_input_t input;
static uint32_t transcript=2166136261u;
static void digest(uint32_t word) {
    for(unsigned i=0;i<4;++i){transcript=(transcript^(word&255u))*16777619u;word>>=8;}
}
static void begin(fzx_finish_menu_state_t *s) {
    fzx_records_snapshot_t r={0};r.phase=FZX_RECORDS_FINISH_MENU_READY;r.frame=1000;
    memset(s,0,sizeof(*s));
    if(!fzx_finish_menu_begin(s,&r,true))return;
    input=(fzx_finish_menu_input_t){.frame=1000,.seed1=0xfedcba98u,.mask1=0x12345678u,
        .seed2=0x76543210u,.mask2=0xfedcba99u,.connected=1};
}
static int tick(fzx_finish_menu_state_t *s,unsigned pressed,int stick) {
    input.pressed=(uint16_t)pressed;input.stick_y=(int16_t)stick;
    if(!fzx_finish_menu_step(s,&input))return 0;
    const fzx_finish_menu_snapshot_t *p=&s->snapshot;
    const uint32_t words[]={p->tick,p->input.frame,p->seed1,p->mask1,p->seed2,p->mask2,
        p->rng1_calls,p->rng2_calls,(uint32_t)p->selection,(uint32_t)p->move_timer,
        (uint32_t)p->reveal,(uint32_t)p->draw_selection,(uint32_t)p->draw_reveal,
        (uint32_t)p->ghost_options_reveal,(uint32_t)p->ghost_reveal,(uint32_t)p->ghost_option_state,
        (uint32_t)p->ghost_option,(uint32_t)p->overwrite_option,(uint32_t)p->ghost_open,
        (uint32_t)p->ghost_timer,(uint32_t)p->ghost_state,(uint32_t)p->busy,
        (uint32_t)p->state_flags,p->action,p->event_count};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);++i)digest(words[i]);
    for(unsigned i=0;i<p->event_count;++i){digest(p->events[i].id);digest(p->events[i].kind);}
    input.frame++;
    input.seed1=s->snapshot.seed1;input.mask1=s->snapshot.mask1;
    input.seed2=s->snapshot.seed2;input.mask2=s->snapshot.mask2;
    return 1;
}
static int ready(fzx_finish_menu_state_t *s) {
    begin(s);
    for(unsigned i=0;i<60;++i)if(!tick(s,0,0))return 0;
    return 1;
}
static void random_step(uint32_t *seed,uint32_t *mask,uint32_t increment) {
    *seed=*seed*LCG_MULTIPLIER+increment;
    if(*mask&1u)*mask^=0x11020u;
    *mask>>=1;
}
int main(void) {
    fzx_finish_menu_state_t s,old;
    fzx_finish_menu_snapshot_t snapshot;
    begin(&s);CHECK(s.initialized&&s.snapshot.reveal==60);
    for(unsigned i=0;i<59;++i){CHECK(tick(&s,BTN_A,0));CHECK(!s.snapshot.action);
        CHECK(s.snapshot.reveal==59-(int)i&&s.snapshot.rng1_calls==0&&s.snapshot.rng2_calls==0);}
    uint32_t seed1=input.seed1,mask1=input.mask1,seed2=input.seed2,mask2=input.mask2;
    random_step(&seed1,&mask1,LCG_INCREMENT_1);random_step(&seed2,&mask2,LCG_INCREMENT_2);
    CHECK(tick(&s,0,0));CHECK(s.snapshot.reveal==0&&s.snapshot.draw_reveal==0);
    CHECK(s.snapshot.rng1_calls==1&&s.snapshot.rng2_calls==1);
    CHECK(s.snapshot.seed1==seed1&&s.snapshot.mask1==mask1&&s.snapshot.seed2==seed2&&s.snapshot.mask2==mask2);
    CHECK(tick(&s,BTN_DOWN,0));CHECK(s.snapshot.selection==2&&s.snapshot.draw_selection==0);
    CHECK(s.snapshot.event_count==1&&s.snapshot.events[0].id==NA_SE_30);
    CHECK(tick(&s,BTN_UP,0));CHECK(s.snapshot.selection==0&&s.snapshot.draw_selection==2);
    CHECK(tick(&s,BTN_UP,0));CHECK(s.snapshot.selection==5);
    CHECK(tick(&s,BTN_DOWN,0));CHECK(s.snapshot.selection==0);
    CHECK(tick(&s,0,0));CHECK(tick(&s,0,-50));CHECK(s.snapshot.selection==0&&s.snapshot.move_timer==0);
    CHECK(tick(&s,0,-51));CHECK(s.snapshot.selection==2&&s.snapshot.move_timer==1);
    for(unsigned i=2;i<60;++i){CHECK(tick(&s,0,-51));CHECK(s.snapshot.selection==2);}
    CHECK(tick(&s,0,-51));CHECK(s.snapshot.selection==3&&s.snapshot.move_timer==60);
    for(unsigned i=61;i<=75;++i)CHECK(tick(&s,0,-51));
    CHECK(s.snapshot.selection==3&&s.snapshot.move_timer==59);
    CHECK(tick(&s,0,-51));CHECK(s.snapshot.selection==4&&s.snapshot.move_timer==60);
    CHECK(tick(&s,0,0));CHECK(tick(&s,0,50));CHECK(s.snapshot.selection==4);
    CHECK(tick(&s,0,51));CHECK(s.snapshot.selection==3);
    for(unsigned choice=0;choice<5;++choice){
        const unsigned selections[]={0,2,3,4,5};
        const unsigned actions[]={FZX_FINISH_MENU_RETRY,FZX_FINISH_MENU_SETTINGS,
            FZX_FINISH_MENU_MACHINE,FZX_FINISH_MENU_COURSE,FZX_FINISH_MENU_QUIT};
        CHECK(ready(&s));
        for(unsigned j=0;j<choice;++j)CHECK(tick(&s,BTN_DOWN,0));
        CHECK(s.snapshot.selection==(int)selections[choice]);
        CHECK(tick(&s,choice&1?BTN_START:BTN_A,0));
        CHECK(s.snapshot.action==actions[choice]&&s.snapshot.busy==1&&s.snapshot.event_count==3);
        CHECK(s.snapshot.events[0].kind==FZX_FINISH_MENU_SOUND&&s.snapshot.events[0].id==NA_SE_33);
        CHECK(s.snapshot.events[1].kind==FZX_FINISH_MENU_LEVEL_FADE&&s.snapshot.events[2].kind==FZX_FINISH_MENU_AUDIO_STOP);
        old=s;CHECK(tick(&s,BTN_DOWN,0));CHECK(memcmp(&old,&s,sizeof(s))==0);
    }
    CHECK(ready(&s));CHECK(tick(&s,BTN_DOWN|BTN_A,0));
    CHECK(s.snapshot.action==FZX_FINISH_MENU_SETTINGS&&s.snapshot.event_count==4&&s.snapshot.draw_selection==0);
    CHECK(ready(&s));input.connected=0;old=s;
    CHECK(!tick(&s,BTN_A,0));CHECK(memcmp(&s,&old,sizeof(s))==0);
    CHECK(tick(&s,0,0));CHECK(s.snapshot.rng1_calls==1&&s.snapshot.rng2_calls==1);
    old=s;CHECK(!tick(&s,0,81));CHECK(memcmp(&s,&old,sizeof(s))==0);
    input.connected=1;input.frame+=1;CHECK(!tick(&s,0,0));CHECK(memcmp(&s,&old,sizeof(s))==0);
    begin(&s);old=s;input.frame++;CHECK(!tick(&s,0,0));CHECK(memcmp(&s,&old,sizeof(s))==0);
    fzx_records_snapshot_t r={0};r.phase=FZX_RECORDS_FINISH_MENU_READY;
    CHECK(!fzx_finish_menu_begin(&s,&r,false));CHECK(memcmp(&s,&old,sizeof(s))==0);
    r.phase=FZX_RECORDS_NAME_ENTRY;CHECK(!fzx_finish_menu_begin(&s,&r,true));
    CHECK(fzx_finish_menu_snapshot(&s,&snapshot));CHECK(memcmp(&snapshot,&s.snapshot,sizeof(snapshot))==0);
    s.snapshot.move_timer=75;old=s;CHECK(!tick(&s,0,0));CHECK(memcmp(&s,&old,sizeof(s))==0);
    puts("finish menu HOST reveal, source navigation, dual RNG, five action requests and fail-closed boundaries PASS");
    printf("transcript %08x\n",(unsigned)transcript);
    return 0;
}
