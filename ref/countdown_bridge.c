/* Source owns countdown animation. This bridge only reads its completed tick. */
#include "global.h"
#include "fzx_game.h"
#include "countdown_state.h"

extern GfxPool *gGfxPool;
extern s32 gRaceIntroTimer, gNumPlayers, gGameMode;
_Static_assert(sizeof(RefCountdown)==72,"countdown snapshot ABI");

int ref_countdown(RefCountdown *out) {
    unsigned int i;
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!gGfxPool || gNumPlayers!=1 || gGameMode!=GAMEMODE_TIME_ATTACK ||
       gRaceIntroTimer<0 || gRaceIntroTimer>460) {out->faults=1;return 0;}
    out->timer=gRaceIntroTimer;
    if(!out->timer)return 1;
    for(i=0;i<16;++i) {
        const unsigned int bits=((unsigned int)(unsigned short)gGfxPool->unk_21988[0].u.i[i/4][i%4]<<16) |
                                (unsigned short)gGfxPool->unk_21988[0].u.f[i/4][i%4];
        memcpy(&out->matrix[i],&bits,4);
    }
    return 1;
}
