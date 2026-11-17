#include "global.h"
#include "fzx_game.h"
#include "fzx_racer.h"
#include "controller.h"
#include "fzx_finish_menu_bridge.h"

extern s32 gNumPlayers,gGameMode,gTotalLapCount,gCourseIndex;
extern s32 gRandSeed1,gRandSeed2;
extern u32 gRandMask1,gRandMask2;
extern Ghost *gFastestGhost;

static bool source_scope(void) {
    return gNumPlayers==1&&gGameMode==GAMEMODE_TIME_ATTACK&&gTotalLapCount==3&&
        gCourseIndex==0&&gPlayerControlPorts[0]>=0&&gPlayerControlPorts[0]<4;
}
bool ref_finish_menu_no_recorded_ghost(void) {
    return source_scope()&&gFastestGhost==NULL;
}
bool ref_finish_menu_input(fzx_finish_menu_input_t *out,uint32_t frame,bool connected) {
    if(!out||!ref_finish_menu_no_recorded_ghost())return false;
    const Controller *pad=&gControllers[gPlayerControlPorts[0]];
    fzx_finish_menu_input_t next={0};next.frame=frame;next.connected=connected;
    next.seed1=(uint32_t)gRandSeed1;next.mask1=gRandMask1;
    next.seed2=(uint32_t)gRandSeed2;next.mask2=gRandMask2;
    if(connected){next.pressed=pad->buttonPressed;next.stick_y=pad->stickY;}
    *out=next;return true;
}
bool ref_finish_menu_commit_rng(const fzx_finish_menu_input_t *input,const fzx_finish_menu_snapshot_t *frame) {
    if(!input||!frame||!ref_finish_menu_no_recorded_ghost()||
       frame->rng1_calls!=1||frame->rng2_calls!=1||!frame->tick||
       frame->input.frame!=input->frame||frame->input.seed1!=input->seed1||
       frame->input.mask1!=input->mask1||frame->input.seed2!=input->seed2||
       frame->input.mask2!=input->mask2||frame->input.pressed!=input->pressed||
       frame->input.stick_y!=input->stick_y||frame->input.connected!=input->connected||
       (uint32_t)gRandSeed1!=input->seed1||gRandMask1!=input->mask1||
       (uint32_t)gRandSeed2!=input->seed2||gRandMask2!=input->mask2)return false;
    _Static_assert(sizeof(gRandSeed1)==sizeof(frame->seed1),"source RNG1 seed ABI");
    _Static_assert(sizeof(gRandSeed2)==sizeof(frame->seed2),"source RNG2 seed ABI");
    memcpy(&gRandSeed1,&frame->seed1,sizeof(gRandSeed1));
    memcpy(&gRandSeed2,&frame->seed2,sizeof(gRandSeed2));
    gRandMask1=frame->mask1;gRandMask2=frame->mask2;return true;
}
