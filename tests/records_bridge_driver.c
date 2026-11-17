#include "global.h"
#include "fzx_game.h"
#include "controller.h"
#include "fzx_records_bridge.h"
/* The original SDK's small libc header omits these standard HOST functions. */
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);

s32 gNumPlayers=1,gGameMode=GAMEMODE_TIME_ATTACK,gTotalLapCount=3,gCourseIndex=0;
s32 gRandSeed1=-12345;
u32 gRandMask1=6789;
s32 gPlayerControlPorts[4];
Controller gControllers[4];

#define CHECK(c,n) do { if(!(c))return n; } while(0)
int main(void) {
    fzx_records_input_t input,unchanged;
    fzx_records_snapshot_t output={0};
    memset(&unchanged,0xa5,sizeof(unchanged));
    for(int port=0;port<4;++port) {
        memset(gControllers,0,sizeof(gControllers));gPlayerControlPorts[0]=port;
        Controller *p=&gControllers[port];
        p->buttonPressed=BTN_A|BTN_RIGHT;p->buttonCurrent=BTN_A|BTN_RIGHT|BTN_R;
        p->stickPressed=STICK_UP;p->stickCurrent=STICK_UP|STICK_LEFT;
        p->retriggerCurrentButtonPress=257;
        CHECK(ref_records_input(&input,0x12345678u,true),1);
        CHECK(input.frame==0x12345678u && input.rng_seed1==(u32)-12345 && input.rng_mask1==6789,2);
        CHECK(input.button_pressed==(BTN_A|BTN_RIGHT) && input.button_current==(BTN_A|BTN_RIGHT|BTN_R),3);
        CHECK(input.stick_pressed==STICK_UP && input.stick_current==(STICK_UP|STICK_LEFT) && input.retrigger==1 && input.connected==1,4);
        CHECK(ref_records_input(&input,15,false),5);
        CHECK(!input.button_pressed && !input.button_current && !input.stick_pressed && !input.stick_current && !input.retrigger && !input.connected,6);
        CHECK(input.rng_seed1==(u32)-12345 && input.rng_mask1==6789 && input.frame==15,7);
    }
    for(int port=-1;port<=4;port+=5) {
        gPlayerControlPorts[0]=port;input=unchanged;
        CHECK(!ref_records_input(&input,1,true) && !memcmp(&input,&unchanged,sizeof(input)),8);
    }
    gPlayerControlPorts[0]=0;
    s32 *scopes[]={&gNumPlayers,&gGameMode,&gTotalLapCount,&gCourseIndex};
    for(unsigned i=0;i<4;++i) {
        s32 original=*scopes[i];*scopes[i]=original+1;input=unchanged;
        CHECK(!ref_records_input(&input,1,true) && !memcmp(&input,&unchanged,sizeof(input)),9);
        *scopes[i]=original;
    }
    CHECK(!ref_records_input(NULL,1,true),10);
    CHECK(ref_records_input(&input,99,true),11);
    output.input=input;output.frame=99;output.rng_calls=100;
    output.rng_seed1=0x87654321u;output.rng_mask1=0x10203u;
    output.frame=98;CHECK(!ref_records_commit_rng(&input,&output),12);output.frame=99;
    ++output.input.button_current;CHECK(!ref_records_commit_rng(&input,&output),13);output.input=input;
    ++gRandSeed1;CHECK(!ref_records_commit_rng(&input,&output),14);--gRandSeed1;
    ++gRandMask1;CHECK(!ref_records_commit_rng(&input,&output),15);--gRandMask1;
    output.rng_calls=0;CHECK(!ref_records_commit_rng(&input,&output),16);output.rng_calls=100;
    CHECK(gRandSeed1==-12345 && gRandMask1==6789,17);
    CHECK(ref_records_commit_rng(&input,&output),18);
    CHECK((u32)gRandSeed1==0x87654321u && gRandMask1==0x10203u,19);
    CHECK(!ref_records_commit_rng(&input,&output),20);
    CHECK(!ref_records_commit_rng(NULL,&output) && !ref_records_commit_rng(&input,NULL),21);
    return 0;
}
