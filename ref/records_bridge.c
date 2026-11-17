#include "global.h"
#include "fzx_game.h"
#include "controller.h"
#include "fzx_records_bridge.h"

extern s32 gNumPlayers,gGameMode,gTotalLapCount,gCourseIndex;
extern s32 gRandSeed1;
extern u32 gRandMask1;

static bool source_scope(void) {
    return gNumPlayers==1 && gGameMode==GAMEMODE_TIME_ATTACK &&
        gTotalLapCount==3 && gCourseIndex==0 && gPlayerControlPorts[0]>=0 &&
        gPlayerControlPorts[0]<4;
}

bool ref_records_input(fzx_records_input_t *out,uint32_t frame,bool connected) {
    if(!out || !source_scope())return false;
    const Controller *pad=&gControllers[gPlayerControlPorts[0]];
    fzx_records_input_t next={0};
    next.frame=frame;next.rng_seed1=(uint32_t)gRandSeed1;next.rng_mask1=gRandMask1;
    next.connected=connected;
    if(connected) {
        next.button_pressed=pad->buttonPressed;next.button_current=pad->buttonCurrent;
        next.stick_pressed=pad->stickPressed;next.stick_current=pad->stickCurrent;
        next.retrigger=pad->retriggerCurrentButtonPress!=0;
    }
    *out=next;return true;
}

bool ref_records_commit_rng(const fzx_records_input_t *input,const fzx_records_snapshot_t *frame) {
    if(!input || !frame || !source_scope() || !frame->rng_calls ||
       frame->frame!=input->frame || frame->input.frame!=input->frame ||
       frame->input.rng_seed1!=input->rng_seed1 || frame->input.rng_mask1!=input->rng_mask1 ||
       frame->input.button_pressed!=input->button_pressed || frame->input.button_current!=input->button_current ||
       frame->input.stick_pressed!=input->stick_pressed || frame->input.stick_current!=input->stick_current ||
       frame->input.retrigger!=input->retrigger || frame->input.connected!=input->connected ||
       (uint32_t)gRandSeed1!=input->rng_seed1 || gRandMask1!=input->rng_mask1)return false;
    /* Preserve the signed source seed's bit pattern without an out-of-range
     * unsigned-to-signed conversion. Both ABIs use the original 32-bit word. */
    _Static_assert(sizeof(gRandSeed1)==sizeof(frame->rng_seed1),"source RNG1 seed ABI");
    memcpy(&gRandSeed1,&frame->rng_seed1,sizeof(gRandSeed1));
    gRandMask1=frame->rng_mask1;return true;
}
