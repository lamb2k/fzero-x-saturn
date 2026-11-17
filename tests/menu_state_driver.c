#include "fzx_menu.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"menu check line %u\n",(unsigned)__LINE__);return 1;} } while(0)
#define A 0x8000u
#define B 0x4000u
#define START 0x1000u
#define UP 0x0800u
#define DOWN 0x0400u
#define LEFT 0x0200u
#define RIGHT 0x0100u
static int press(fzx_menu_state_t *s,uint16_t key,uint8_t pads) {
    return fzx_menu_step(s,0,pads,false) && fzx_menu_step(s,key,pads,false);
}
static int idle(fzx_menu_state_t *s,unsigned ticks,uint8_t pads) {
    while(ticks--)if(!fzx_menu_step(s,0,pads,false))return 0;
    return 1;
}
static const fzx_menu_draw_t *image(const fzx_menu_snapshot_t *p,unsigned id) {
    for(unsigned i=0;i<p->count;++i)if(p->draws[i].image==id)return &p->draws[i];
    return NULL;
}
static int show(const fzx_menu_state_t *s) {
    const fzx_menu_snapshot_t *p=&s->snapshot;
    printf("%u %u %u %u %u %u",p->tick,p->phase,p->selected,p->suboption,p->request,p->count);
    for(unsigned i=0;i<p->count;++i) {
        const fzx_menu_draw_t *d=&p->draws[i];
        printf(" %u:%d,%d:%u,%u,%u,%u",d->image,d->x,d->y,d->r,d->g,d->b,d->a);
    }
    putchar('\n');return 0;
}
int main(void) {
    fzx_menu_state_t s,other;
    fzx_menu_snapshot_t p;
    CHECK(fzx_menu_reset(&s,0,0,false,0));
    CHECK(s.phase==0 && s.selected==0 && s.snapshot.count==13);
    CHECK(image(&s.snapshot,FZX_MENU_BACKGROUND)->x==8 && image(&s.snapshot,FZX_MENU_BACKGROUND)->r==75);
    CHECK(image(&s.snapshot,FZX_MENU_HEADER)->x==100 && image(&s.snapshot,FZX_MENU_HEADER)->y==13);
    CHECK(image(&s.snapshot,FZX_MENU_HEADER)->r==250 && image(&s.snapshot,FZX_MENU_HEADER)->b==0);
    CHECK(image(&s.snapshot,FZX_MENU_GP)->x==32 && image(&s.snapshot,FZX_MENU_GP)->y==38);
    CHECK(image(&s.snapshot,FZX_MENU_TIME_ATTACK)->x==96 && image(&s.snapshot,FZX_MENU_TIME_ATTACK)->a==180);
    CHECK(image(&s.snapshot,FZX_MENU_FLAMES)->x==-192 && image(&s.snapshot,FZX_MENU_FLAMES)->r==255);
    CHECK(!image(&s.snapshot,FZX_MENU_MASTER));show(&s);
    const fzx_menu_state_t initial=s;
    for(unsigned i=0;i<20;++i)CHECK(fzx_menu_snapshot(&s,&p));
    CHECK(!memcmp(&s,&initial,sizeof(s)));
    CHECK(fzx_menu_reset(&other,1,0,false,123));
    CHECK(press(&other,A,1));CHECK(!memcmp(&s,&initial,sizeof(s)));
    /* Original 4x2 grid redirects unavailable cartridge cards and 1P Versus. */
    CHECK(press(&s,DOWN,1) && s.selected==5);
    CHECK(press(&s,LEFT,1) && s.selected==5);
    CHECK(press(&s,RIGHT,1) && s.selected==6);
    CHECK(press(&s,RIGHT,1) && s.selected==6);
    CHECK(press(&s,UP,1) && s.selected==2);
    CHECK(press(&s,RIGHT,1) && s.selected==2);
    CHECK(press(&s,RIGHT,2) && s.selected==3);
    CHECK(press(&s,DOWN,2) && s.selected==6);
    CHECK(press(&s,UP,2) && s.selected==2);
    CHECK(press(&s,LEFT,1) && s.selected==1 && s.cue==30);
    /* Held input repeats after15 identical ticks, every5 thereafter. */
    CHECK(fzx_menu_reset(&s,0,0,false,0));
    CHECK(fzx_menu_step(&s,RIGHT,2,false) && s.selected==1);
    for(unsigned i=0;i<14;++i)CHECK(fzx_menu_step(&s,RIGHT,2,false) && s.selected==1);
    CHECK(fzx_menu_step(&s,RIGHT,2,false) && s.selected==2);
    for(unsigned i=0;i<4;++i)CHECK(fzx_menu_step(&s,RIGHT,2,false) && s.selected==2);
    CHECK(fzx_menu_step(&s,RIGHT,2,false) && s.selected==3);
    /* Confirmation remains edge-only even while directions repeat. */
    CHECK(fzx_menu_reset(&s,1,0,false,0));
    CHECK(fzx_menu_step(&s,A,1,false) && s.phase==1 && s.cue==33);
    CHECK(s.cue_count==2 && s.cues[0]==62 && s.cues[1]==33);
    for(unsigned i=0;i<30;++i)CHECK(fzx_menu_step(&s,A,1,false) && s.phase==1);
    CHECK(image(&s.snapshot,FZX_MENU_TIME_ATTACK_LABEL)->x==116);
    CHECK(image(&s.snapshot,FZX_MENU_TIME_ATTACK_LABEL)->y==101);
    CHECK(image(&s.snapshot,FZX_MENU_RECORDS_LABEL)->x==128);
    CHECK(press(&s,A,1) && s.phase==2);
    CHECK(image(&s.snapshot,FZX_MENU_TIME_ATTACK_LABEL)->g==255 && image(&s.snapshot,FZX_MENU_TIME_ATTACK_LABEL)->r==0);
    CHECK(press(&s,B,1) && s.phase==1 && s.cue==16);
    CHECK(press(&s,B,1) && s.phase==0);
    CHECK(press(&s,A,1) && s.phase==1);CHECK(idle(&s,12,1));
    CHECK(press(&s,A,1) && s.phase==2);CHECK(press(&s,A,1) && s.phase==3 && s.course_prepare);
    CHECK(s.objects[3].counter==1 && s.objects[12].counter==1 && !s.request);show(&s);
    CHECK(idle(&s,10,1) && s.objects[3].counter==11 && s.phase==3);
    CHECK(idle(&s,1,1) && s.objects[3].counter==12 && s.phase==3);
    CHECK(idle(&s,1,1) && s.phase==4 && !s.request);
    CHECK(idle(&s,1,1) && s.request==FZX_MENU_REQUEST_COURSE);show(&s);
    const fzx_menu_state_t done=s;
    CHECK(fzx_menu_step(&s,B,1,false) && !memcmp(&s,&done,sizeof(s)));
    /* GP and Practice class boundaries and source course-select handoff. */
    for(unsigned mode=0;mode<=5;mode+=5) {
        CHECK(fzx_menu_reset(&s,(uint8_t)mode,0,false,0));CHECK(press(&s,A,1));
        for(unsigned i=0;i<5;++i)CHECK(press(&s,DOWN,1));
        CHECK(s.suboptions[mode]==2);CHECK(press(&s,A,1) && s.difficulty==2 && s.players==1);
        CHECK(press(&s,A,1));CHECK(idle(&s,14,1) && s.request==FZX_MENU_REQUEST_COURSE);
    }
    CHECK(fzx_menu_reset(&s,0,2,false,0));CHECK(press(&s,A,1));CHECK(idle(&s,12,1));
    CHECK(image(&s.snapshot,FZX_MENU_MASTER) && image(&s.snapshot,FZX_MENU_NOVICE)->y==87);
    for(unsigned i=0;i<5;++i)CHECK(press(&s,DOWN,1));
    CHECK(s.suboptions[0]==3);CHECK(press(&s,UP,1) && s.suboptions[0]==2);
    CHECK(fzx_menu_reset(&s,5,0,true,0));CHECK(press(&s,A,1));
    for(unsigned i=0;i<5;++i)CHECK(press(&s,DOWN,1));
    CHECK(s.suboptions[5]==3);
    /* Versus rows reflect actual attached pads and unwind on disconnect. */
    CHECK(fzx_menu_reset(&s,3,0,false,0));CHECK(press(&s,A,4));CHECK(idle(&s,12,4));
    CHECK(image(&s.snapshot,FZX_MENU_4PLAYERS)->a==255);
    for(unsigned i=0;i<4;++i)CHECK(press(&s,DOWN,4));
    CHECK(s.suboptions[3]==2);
    CHECK(fzx_menu_step(&s,0,2,false) && s.suboptions[3]==0);
    CHECK(image(&s.snapshot,FZX_MENU_4PLAYERS)->a==128);
    CHECK(fzx_menu_step(&s,0,1,false) && s.phase==0 && s.cue==16);
    CHECK(fzx_menu_reset(&s,3,0,false,0));CHECK(press(&s,A,3));CHECK(press(&s,DOWN,3));
    CHECK(press(&s,A,3) && s.players==3);CHECK(press(&s,A,3));
    CHECK(idle(&s,14,3) && !s.request); /* Early confirm still waits for the sliding popup. */
    CHECK(idle(&s,14,3) && s.request==FZX_MENU_REQUEST_COURSE);
    /* All other source exits stay distinct, never redirected into Time Attack. */
    CHECK(fzx_menu_reset(&s,1,0,false,0));CHECK(press(&s,A,1));CHECK(press(&s,DOWN,1));
    CHECK(press(&s,A,1) && s.request==FZX_MENU_REQUEST_RECORDS && s.phase==6);show(&s);
    CHECK(fzx_menu_reset(&s,6,0,false,0));CHECK(press(&s,A,1) && s.request==FZX_MENU_REQUEST_OPTIONS);
    CHECK(fzx_menu_reset(&s,2,0,false,0));CHECK(press(&s,A,1) && s.request==FZX_MENU_REQUEST_MACHINE);
    CHECK(s.course==54 && s.difficulty==3 && s.players==1);
    CHECK(fzx_menu_reset(&s,0,0,false,0));CHECK(press(&s,B,1));
    CHECK(s.request==FZX_MENU_REQUEST_TITLE && s.stop_music && s.cue==16);
    /* Transition gate consumes edges but blocks source state changes. */
    CHECK(fzx_menu_reset(&s,0,0,false,0));
    CHECK(fzx_menu_step(&s,A,1,true) && s.phase==0 && !s.cue);
    CHECK(fzx_menu_step(&s,A,1,false) && s.phase==0);
    CHECK(press(&s,A,1) && s.phase==1);
    CHECK(fzx_menu_reset(&s,1,0,false,0));
    CHECK(fzx_menu_step(&s,A,0,false) && s.phase==0 && !s.cue);
    CHECK(fzx_menu_step(&s,START,1,false) && s.phase==1);
    CHECK(press(&s,A,1) && s.phase==2);CHECK(press(&s,A,1) && s.phase==3);
    CHECK(press(&s,B,1) && s.phase==1 && !s.request);
    /* Bounds and rejected calls are atomic; wrapping frame is defined unsigned. */
    CHECK(fzx_menu_reset(&s,0,0,false,UINT32_MAX));CHECK(idle(&s,1,1) && s.frame==0);
    const fzx_menu_state_t before=s;
    CHECK(!fzx_menu_step(&s,0,5,false) && !memcmp(&s,&before,sizeof(s)));
    CHECK(!fzx_menu_reset(&s,4,0,false,0) && !memcmp(&s,&before,sizeof(s)));
    s.objects[3].counter=INT32_MAX;const fzx_menu_state_t bad=s;
    CHECK(!fzx_menu_step(&s,0,1,false) && !memcmp(&s,&bad,sizeof(s)));
    CHECK(!fzx_menu_snapshot(&s,&p));CHECK(!fzx_menu_step(NULL,0,1,false));
    CHECK(!fzx_menu_snapshot(NULL,&p));CHECK(!fzx_menu_reset(NULL,0,0,false,0));
    puts("source menu: navigation, six exits, repeats, draw timing, snapshots and bounds pass");
    return 0;
}
