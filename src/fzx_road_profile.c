#include "fzx_road_profile.h"
#if FZX_ROAD_TILE_PROFILE
#include <string.h>
volatile fzx_road_profile_t fzx_road_profile;
static uint16_t previous;
static unsigned parents[16];
static void sample(void) {
    uint16_t now=fzx_road_profile_clock(),elapsed=(uint16_t)(now-previous);previous=now;
    fzx_road_profile.ticks[fzx_road_profile.phase]+=elapsed;
    fzx_road_profile.total+=elapsed;++fzx_road_profile.transitions;
    if(elapsed>fzx_road_profile.max_interval)fzx_road_profile.max_interval=elapsed;
}
void fzx_road_profile_begin(void) {
    memset((void *)&fzx_road_profile,0,sizeof(fzx_road_profile));
    fzx_road_profile.active=1;fzx_road_profile.calls[FZX_RP_BASE]=1;
    previous=fzx_road_profile_clock();
}
unsigned fzx_road_profile_enter(unsigned phase) {
    unsigned old=fzx_road_profile.phase;
    if(!fzx_road_profile.active || phase>=FZX_RP_COUNT || fzx_road_profile.depth>=16) {
        fzx_road_profile.faults|=1;return old;
    }
    sample();parents[fzx_road_profile.depth++]=old;
    fzx_road_profile.phase=phase;++fzx_road_profile.calls[phase];return old;
}
void fzx_road_profile_leave(unsigned parent) {
    if(!fzx_road_profile.active || !fzx_road_profile.depth ||
       parent!=parents[fzx_road_profile.depth-1]){fzx_road_profile.faults|=2;return;}
    sample();--fzx_road_profile.depth;fzx_road_profile.phase=parent;
}
void fzx_road_profile_end(void) {
    if(!fzx_road_profile.active){fzx_road_profile.faults|=4;return;}
    sample();if(fzx_road_profile.depth || fzx_road_profile.phase!=FZX_RP_BASE)fzx_road_profile.faults|=8;
    fzx_road_profile.active=0;fzx_road_profile.magic=0x52504631;
}
#endif
