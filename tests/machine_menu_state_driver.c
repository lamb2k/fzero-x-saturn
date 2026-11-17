#include "fzx_machine_menu.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"machine menu check line %u\n",(unsigned)__LINE__);return 1;} } while(0)
#define A 0x8000u
#define B 0x4000u
#define Z 0x2000u
#define START 0x1000u
#define UP 0x0800u
#define DOWN 0x0400u
#define LEFT 0x0200u
#define RIGHT 0x0100u
#define R 0x0010u
static uint32_t hash=2166136261u,snapshots;
static void record(const fzx_machine_menu_state_t *s) {
    const unsigned char *p=(const unsigned char *)&s->snapshot;
    for(unsigned i=0;i<sizeof(s->snapshot);++i)hash=(hash^p[i])*16777619u;
    ++snapshots;
}
static bool reset(fzx_machine_menu_state_t *s,const fzx_machine_menu_config_t *c,uint8_t screen) {
    const bool ok=fzx_machine_menu_reset(s,c,screen);if(ok)record(s);return ok;
}
static bool step(fzx_machine_menu_state_t *s,uint16_t buttons,int8_t x,bool connected,bool transition) {
    const bool ok=fzx_machine_menu_step(s,buttons,x,connected,transition);if(ok)record(s);return ok;
}
static int press(fzx_machine_menu_state_t *s,uint16_t buttons) {
    return step(s,0,0,true,false) && step(s,buttons,0,true,false);
}
static void config(fzx_machine_menu_config_t *c) {
    memset(c,0,sizeof(*c));c->mode=1;
    for(unsigned i=0;i<30;++i)c->last_engine[i]=0.5f;
}
static int check_draws(const fzx_machine_menu_state_t *s) {
    unsigned models=0,rects=0;
    for(unsigned i=0;i<s->snapshot.count;++i) {
        const fzx_machine_menu_draw_t *d=&s->snapshot.draws[i];
        if(d->kind==FZX_MACHINE_DRAW_MODELS)++models;
        else if(d->kind==FZX_MACHINE_DRAW_RECT)++rects;
        else if(d->kind>FZX_MACHINE_DRAW_MODELS)return 0;
    }
    return models==1 && (s->screen?rects>=1:rects==224);
}
int main(void) {
    fzx_machine_menu_state_t s,other;
    fzx_machine_menu_config_t c;
    fzx_machine_menu_snapshot_t snap;
    config(&c);CHECK(reset(&s,&c,0));
    CHECK(s.snapshot.phase==0 && s.snapshot.slot==0 && s.unlocked==6 && s.snapshot.character==0);
    CHECK(s.snapshot.object_count==10 && check_draws(&s));
    CHECK(s.snapshot.draws[0].kind==FZX_MACHINE_DRAW_RECT);
    CHECK(s.snapshot.draws[0].x==12 && s.snapshot.draws[0].y==8 && s.snapshot.draws[0].x2==307);
    CHECK(s.snapshot.draws[223].y==231);
    bool name=false;
    for(unsigned i=0;i<s.snapshot.count;++i)if(s.snapshot.draws[i].kind==FZX_MACHINE_DRAW_GLYPH && s.snapshot.draws[i].y==210)name=true;
    CHECK(name);
    const fzx_machine_menu_state_t entry=s;
    for(unsigned i=0;i<20;++i)CHECK(fzx_machine_menu_snapshot(&s,&snap));
    CHECK(!memcmp(&entry,&s,sizeof(s)));
    CHECK(reset(&other,&c,1));CHECK(press(&other,RIGHT));CHECK(!memcmp(&entry,&s,sizeof(s)));
    CHECK(press(&s,LEFT) && s.snapshot.slot==5 && s.snapshot.character==5);
    CHECK(press(&s,RIGHT) && s.snapshot.slot==0);
    CHECK(press(&s,UP) && s.snapshot.slot==0);CHECK(press(&s,DOWN) && s.snapshot.slot==0);
    CHECK(press(&s,A) && s.select_phase==2 && s.locks[0]==1 && s.cues[0]==33);
    for(unsigned i=0;i<30;++i)CHECK(step(&s,A,0,true,false) && s.select_phase==2 && !s.request);
    CHECK(press(&s,B) && s.select_phase==0 && !s.locks[0] && s.cues[0]==16);
    CHECK(press(&s,A));CHECK(press(&s,START) && s.request==FZX_MACHINE_REQUEST_SETTINGS && s.cues[0]==62);
    const fzx_machine_menu_state_t requested=s;
    CHECK(step(&s,B,0,true,false) && !memcmp(&s,&requested,sizeof(s)));
    CHECK(reset(&s,&c,0));CHECK(press(&s,B) && s.request==FZX_MACHINE_REQUEST_COURSE);
    CHECK(reset(&s,&c,0));
    for(unsigned tick=0;tick<26;++tick) {
        CHECK(step(&s,RIGHT,0,true,false));
        unsigned expected=tick<15?1:tick<20?2:tick<25?3:4;
        CHECK(s.snapshot.slot==expected);
    }
    const unsigned slot=s.snapshot.slot;
    CHECK(step(&s,LEFT,0,false,false) && s.snapshot.slot==slot);
    CHECK(step(&s,LEFT,0,true,false) && s.snapshot.slot==slot-1);
    c.everything_unlocked=1;CHECK(reset(&s,&c,0));
    CHECK(press(&s,UP) && s.snapshot.slot==24 && s.snapshot.character==13);
    CHECK(press(&s,DOWN) && s.snapshot.slot==0);
    CHECK(press(&s,LEFT) && s.snapshot.slot==5);
    for(unsigned i=0;i<4;++i)CHECK(press(&s,DOWN));
    CHECK(s.snapshot.slot==29 && s.snapshot.character==28);
    CHECK(press(&s,RIGHT) && s.snapshot.slot==24);
    for(unsigned unlock=0;unlock<=12;++unlock) {
        config(&c);c.unlock_count=(uint8_t)unlock;
        CHECK(reset(&s,&c,0) && s.unlocked==(int)((unlock/3)*6+6));
        CHECK(press(&s,UP) && s.snapshot.slot==s.unlocked-6);
    }
    config(&c);c.last_engine[0]=0.25f;
    CHECK(reset(&s,&c,1) && s.engine[0]==0.25f && s.snapshot.phase==0 && check_draws(&s));
    CHECK(press(&s,LEFT) && s.engine[0]==0.2421875f && s.last_engine[0]==s.engine[0]);
    CHECK(press(&s,RIGHT) && s.engine[0]==0.25f);
    CHECK(press(&s,Z) && s.racers[0].machineSkinIndex==3);
    CHECK(press(&s,R) && s.racers[0].machineSkinIndex==0);
    const float before=s.engine[0];
    CHECK(step(&s,0,80,true,true) && s.engine[0]==before);
    CHECK(step(&s,0,80,true,false));
    CHECK(s.engine[0]>before+0.05759f && s.engine[0]<before+0.05761f);
    for(unsigned i=0;i<30;++i)CHECK(step(&s,0,80,true,false));
    CHECK(s.engine[0]==1.0f);
    for(unsigned i=0;i<30;++i)CHECK(step(&s,0,-80,true,false));
    CHECK(s.engine[0]==0.0f);
    CHECK(press(&s,A) && s.settings_phase==2 && s.locks[0]==1);
    CHECK(step(&s,RIGHT,80,true,false) && s.engine[0]==0.0f);
    CHECK(press(&s,B) && s.settings_phase==0 && !s.locks[0]);
    CHECK(press(&s,A));CHECK(press(&s,START) && s.request==FZX_MACHINE_REQUEST_RACE);
    CHECK(reset(&s,&c,1));CHECK(press(&s,B) && s.request==FZX_MACHINE_REQUEST_SELECT);
    for(unsigned character=0;character<30;++character)for(unsigned screen=0;screen<2;++screen) {
        config(&c);c.everything_unlocked=1;c.character=(uint8_t)character;c.skin=(uint8_t)(character%4);
        c.last_engine[character]=(float)(character%9)/8.0f;
        CHECK(reset(&s,&c,(uint8_t)screen) && s.snapshot.character==character && check_draws(&s));
        if(screen)CHECK(s.snapshot.engine==c.last_engine[character]);
        for(unsigned tick=0;tick<420;++tick)CHECK(step(&s,0,0,true,false) && check_draws(&s));
    }
    config(&c);c.mode=0;CHECK(!reset(&s,&c,0));c.mode=1;c.character=30;CHECK(!reset(&s,&c,0));
    config(&c);c.last_engine[0]=-0.01f;CHECK(!reset(&s,&c,1));
    config(&c);CHECK(reset(&s,&c,0));CHECK(!step(&s,0x0020u,0,true,false));
    CHECK(!step(&s,0,81,true,false));s.indices[0]=30;CHECK(!step(&s,0,0,true,false));
    CHECK(reset(&s,&c,0));s.engine[0]=2.0f;CHECK(!fzx_machine_menu_snapshot(&s,&snap));
    printf("machine menu %u source snapshots %08x\n",snapshots,hash);
    return 0;
}
