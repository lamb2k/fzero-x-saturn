#include "global.h"
#include "fzx_game.h"
#include "fzx_racer.h"
#include "controller.h"
#include "fzx_finish_menu_bridge.h"
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
s32 gNumPlayers=1,gGameMode=GAMEMODE_TIME_ATTACK,gTotalLapCount=3,gCourseIndex=0;
s32 gRandSeed1=-12345,gRandSeed2=-54321;
u32 gRandMask1=6789,gRandMask2=9876;
s32 gPlayerControlPorts[4];
Controller gControllers[4];
Ghost *gFastestGhost;
static Ghost ghost;
#define CHECK(c,n) do{if(!(c))return n;}while(0)
static int unchanged(void) {
    return gRandSeed1==-12345&&gRandSeed2==-54321&&gRandMask1==6789&&gRandMask2==9876;
}
int main(void) {
    fzx_finish_menu_input_t input,sentinel;
    fzx_finish_menu_snapshot_t frame={0};
    memset(&sentinel,0xa5,sizeof(sentinel));
    CHECK(ref_finish_menu_no_recorded_ghost(),1);
    for(int port=0;port<4;++port){
        memset(gControllers,0,sizeof(gControllers));gPlayerControlPorts[0]=port;
        gControllers[port].buttonPressed=BTN_START|BTN_UP;gControllers[port].stickY=-57;
        CHECK(ref_finish_menu_input(&input,123,true),2);
        CHECK(input.frame==123&&input.pressed==(BTN_START|BTN_UP)&&input.stick_y==-57&&input.connected,3);
        CHECK(input.seed1==(u32)-12345&&input.mask1==6789&&input.seed2==(u32)-54321&&input.mask2==9876,4);
        CHECK(ref_finish_menu_input(&input,124,false),5);
        CHECK(!input.pressed&&!input.stick_y&&!input.connected&&input.frame==124,6);
    }
    for(int port=-1;port<=4;port+=5){
        gPlayerControlPorts[0]=port;input=sentinel;
        CHECK(!ref_finish_menu_no_recorded_ghost()&&!ref_finish_menu_input(&input,1,true)&&
            !memcmp(&input,&sentinel,sizeof(input)),7);
    }
    gPlayerControlPorts[0]=0;
    s32 *scope[]={&gNumPlayers,&gGameMode,&gTotalLapCount,&gCourseIndex};
    for(unsigned i=0;i<4;++i){s32 v=*scope[i];*scope[i]=-1;input=sentinel;
        CHECK(!ref_finish_menu_no_recorded_ghost()&&!ref_finish_menu_input(&input,1,true)&&
            !memcmp(&input,&sentinel,sizeof(input)),8);*scope[i]=v;}
    gFastestGhost=&ghost;input=sentinel;
    CHECK(!ref_finish_menu_no_recorded_ghost()&&!ref_finish_menu_input(&input,1,true)&&
        !memcmp(&input,&sentinel,sizeof(input)),9);gFastestGhost=NULL;
    CHECK(!ref_finish_menu_input(NULL,1,true),10);
    CHECK(ref_finish_menu_input(&input,126,true),11);frame.input=input;
    frame.tick=60;frame.rng1_calls=frame.rng2_calls=1;
    frame.seed1=0x98765432u;frame.mask1=0x12345;frame.seed2=0x87654321u;frame.mask2=0x6789a;
    frame.rng1_calls=0;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),12);frame.rng1_calls=1;
    frame.rng2_calls=0;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),13);frame.rng2_calls=1;
    frame.tick=0;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),14);frame.tick=60;
    u32 *words[]={&frame.input.frame,&frame.input.seed1,&frame.input.mask1,&frame.input.seed2,&frame.input.mask2};
    for(unsigned i=0;i<5;++i){++*words[i];CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),15);--*words[i];}
    frame.input.pressed^=BTN_A;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),16);frame.input.pressed=input.pressed;
    ++frame.input.stick_y;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),17);frame.input.stick_y=input.stick_y;
    frame.input.connected^=1;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),18);frame.input.connected=input.connected;
    ++gRandSeed1;CHECK(!ref_finish_menu_commit_rng(&input,&frame),19);--gRandSeed1;CHECK(unchanged(),20);
    ++gRandSeed2;CHECK(!ref_finish_menu_commit_rng(&input,&frame),21);--gRandSeed2;CHECK(unchanged(),22);
    ++gRandMask1;CHECK(!ref_finish_menu_commit_rng(&input,&frame),23);--gRandMask1;CHECK(unchanged(),24);
    ++gRandMask2;CHECK(!ref_finish_menu_commit_rng(&input,&frame),25);--gRandMask2;CHECK(unchanged(),26);
    gFastestGhost=&ghost;CHECK(!ref_finish_menu_commit_rng(&input,&frame)&&unchanged(),27);gFastestGhost=NULL;
    CHECK(ref_finish_menu_commit_rng(&input,&frame),28);
    CHECK((u32)gRandSeed1==frame.seed1&&gRandMask1==frame.mask1&&
        (u32)gRandSeed2==frame.seed2&&gRandMask2==frame.mask2,29);
    CHECK(!ref_finish_menu_commit_rng(&input,&frame),30);
    CHECK(!ref_finish_menu_commit_rng(NULL,&frame)&&!ref_finish_menu_commit_rng(&input,NULL),31);
    return 0;
}
