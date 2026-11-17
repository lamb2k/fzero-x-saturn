#include "fzx_retry_flow.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"retry check line %d: %s\n",__LINE__,#x);return 1;} } while(0)

/* Independent expected transcript from the original per-tick contract:
 * draw T, dispatch T+1, hide T+2..25, appear T+25..46. */
static int schedule(void) {
    fzx_retry_flow_t s,before;
    memset(&s,0x5a,sizeof(s));before=s;
    CHECK(!fzx_retry_flow_begin(NULL,0));
    CHECK(!fzx_retry_flow_begin(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof(s)));
    CHECK(fzx_retry_flow_begin(&s,1000));
    before=s;CHECK(!fzx_retry_flow_step(&s,1000));CHECK(!memcmp(&s,&before,sizeof(s)));
    CHECK(!fzx_retry_flow_step(&s,1002));CHECK(!memcmp(&s,&before,sizeof(s)));
    unsigned dispatches=0,reloads=0,updates=0;
    for(unsigned tick=1;tick<=80;tick++) {
        CHECK(fzx_retry_flow_step(&s,1000+tick));
        CHECK(s.tick==tick && s.frame==1000+tick);
        CHECK(s.event_count>=1);
        CHECK(s.events[s.event_count-1]==FZX_RETRY_RACE_UPDATE_REMAINDER);
        ++updates;
        if(tick==1) {
            CHECK(s.event_count==2 && s.events[0]==FZX_RETRY_SETTLE_CONTROLLERS_LIVES);
            CHECK(s.fade.activeTransitionType==0 && s.transition_state==0);
            ++dispatches;
        } else if(tick==25) {
            static const unsigned char required[]={FZX_RETRY_AUDIO_RESTORE,FZX_RETRY_AUDIO_MODE,
                FZX_RETRY_OBJECTS_RESET,FZX_RETRY_HUD_RESET,FZX_RETRY_CONTROLLER_RESET,
                FZX_RETRY_RACER_INIT,FZX_RETRY_CAMERA_INIT,FZX_RETRY_EFFECTS_INIT,
                FZX_RETRY_GADGETS_RESET,FZX_RETRY_CLEAR_PAUSE,FZX_RETRY_MENUS_INIT,
                FZX_RETRY_LIVES_COUNTER_RESET,FZX_RETRY_PORTRAITS_INIT,FZX_RETRY_RACE_UPDATE_REMAINDER};
            CHECK(s.event_count==sizeof(required));CHECK(!memcmp(s.events,required,sizeof(required)));
            CHECK(s.reload_count==1 && s.triggered==0 && s.menu_flags==0);
            ++reloads;
        } else CHECK(s.event_count==1);
        CHECK(s.reload_count==(tick>=25));
        int expected_alpha=0;
        if(tick>=2 && tick<=22)expected_alpha=(int)(((tick-2)*255.0f)/20);
        else if(tick>=23 && tick<=25)expected_alpha=255;
        else if(tick>=26 && tick<=45)expected_alpha=(int)(((45-tick)*255.0f)/20);
        CHECK(s.fade.transitionInfo.work3==expected_alpha);
        CHECK(s.transition_state==(tick==1||tick>=46?0:tick<25?2:1));
        before=s;CHECK(!fzx_retry_flow_step(&s,1000+tick));CHECK(!memcmp(&s,&before,sizeof(s)));
        printf("%u %d %d %u %u\n",tick,s.transition_state,s.fade.transitionInfo.work3,s.reload_count,s.event_count);
    }
    CHECK(dispatches==1 && reloads==1 && updates==80);
    CHECK(fzx_retry_flow_begin(&s,UINT32_MAX-1));CHECK(fzx_retry_flow_step(&s,UINT32_MAX));
    before=s;CHECK(!fzx_retry_flow_step(&s,0));CHECK(!memcmp(&s,&before,sizeof(s)));
    CHECK(fzx_retry_flow_begin(&s,10));s.delay=1;before=s;
    CHECK(!fzx_retry_flow_step(&s,11));CHECK(!memcmp(&s,&before,sizeof(s)));
    CHECK(fzx_retry_flow_begin(&s,10));s.fade.argument=40;before=s;
    CHECK(!fzx_retry_flow_step(&s,11));CHECK(!memcmp(&s,&before,sizeof(s)));
    return 0;
}

/* Expected final words are computed independently, including modulo-2^32
 * wrapping and mask branches. No diagnostic initial seeds are available. */
static void advance(uint32_t *seed,uint32_t *mask,uint32_t increment,unsigned count) {
    for(unsigned i=0;i<count;i++) {
        *seed=*seed*0x41c64e6du+increment;
        if(*mask&1u)*mask^=0x11020u;
        *mask>>=1;
    }
}
static int init_rng(void) {
    CHECK(!fzx_retry_menus_init_rng(NULL));
    const uint32_t cases[]={0,1,0xffffffffu,0x80000000u,0x12345678u};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        fzx_retry_rng_t s={cases[i],cases[4-i],cases[(i+1)%5],cases[(i+2)%5],99,99};
        for(unsigned race=0;race<5;race++) {
            fzx_retry_rng_t expected=s;
            advance(&expected.seed1,&expected.mask1,0x3039u,1);
            advance(&expected.seed2,&expected.mask2,67890u,26);
            CHECK(fzx_retry_menus_init_rng(&s));
            CHECK(s.calls1==1 && s.calls2==26);
            CHECK(s.seed1==expected.seed1 && s.mask1==expected.mask1);
            CHECK(s.seed2==expected.seed2 && s.mask2==expected.mask2);
            printf("rng %u %u %08x %08x %08x %08x\n",i,race,s.seed1,s.mask1,s.seed2,s.mask2);
            /* Source Racer_Init and ongoing race draw on live words before
             * the next Menus_Init. These perturbations model that boundary;
             * they do not claim to execute Racer_Init itself. */
            s.seed1+=race+17;s.seed2^=0x87654321u;s.mask1^=9;s.mask2+=3;
        }
    }
    return 0;
}
int main(void){if(schedule()||init_rng())return 1;puts("retry source schedule HOST PASS");return 0;}
