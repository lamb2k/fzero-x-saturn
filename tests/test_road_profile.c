#include "fzx_road_profile.h"
#include <assert.h>
static uint16_t clock_value;
uint16_t fzx_road_profile_clock(void){return clock_value;}
int main(void) {
    clock_value=65530;fzx_road_profile_begin();clock_value=4;
    unsigned a=fzx_road_profile_enter(FZX_RP_TRANSFORM);clock_value=14;
    unsigned b=fzx_road_profile_enter(FZX_RP_PROJECT);clock_value=34;
    fzx_road_profile_leave(b);clock_value=44;fzx_road_profile_leave(a);
    clock_value=54;fzx_road_profile_end();
    assert(fzx_road_profile.total==60 && !fzx_road_profile.faults);
    assert(fzx_road_profile.ticks[FZX_RP_BASE]==20);
    assert(fzx_road_profile.ticks[FZX_RP_TRANSFORM]==20);
    assert(fzx_road_profile.ticks[FZX_RP_PROJECT]==20);
    assert(fzx_road_profile.transitions==5 && fzx_road_profile.max_interval==20);
    fzx_road_profile_begin();int once=0;
    assert(RP_CALL(FZX_RP_HASH,++once)==1 && once==1);
    fzx_road_profile_end();assert(!fzx_road_profile.faults);
    fzx_road_profile_begin();fzx_road_profile_enter(FZX_RP_COUNT);
    assert(fzx_road_profile.faults);
    fzx_road_profile_begin();fzx_road_profile_enter(FZX_RP_CLIP);
    fzx_road_profile_end();assert(fzx_road_profile.faults);
    fzx_road_profile_begin();fzx_road_profile_enter(FZX_RP_SPLIT);
    fzx_road_profile_leave(FZX_RP_HASH);assert(fzx_road_profile.faults);
    fzx_road_profile_begin();fzx_road_profile_leave(FZX_RP_BASE);
    assert(fzx_road_profile.faults);
    return 0;
}
