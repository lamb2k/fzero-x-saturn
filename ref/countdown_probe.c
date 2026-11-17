/* ROM-free source layout and nonmutation test. Never linked into the game. */
#include "global.h"
#include "fzx_game.h"
#include "countdown_state.h"
#define CHECK(x) do {if(!(x))return __LINE__;}while(0)
static GfxPool pool,saved;
GfxPool *gGfxPool=&pool;
s32 gRaceIntroTimer,gNumPlayers=1,gGameMode=GAMEMODE_TIME_ATTACK;
int main(void) {
    unsigned i;RefCountdown result,again;
    for(i=0;i<16;++i) {
        pool.unk_21988[0].u.i[i/4][i%4]=(s16)(i*43-355);
        pool.unk_21988[0].u.f[i/4][i%4]=(u16)(65535-i*3117);
    }
    memcpy(&saved,&pool,sizeof(pool));gRaceIntroTimer=219;
    CHECK(ref_countdown(&result));CHECK(ref_countdown(&again));
    CHECK(result.timer==219&&!result.faults&&!memcmp(&result,&again,sizeof(result)));
    for(i=0;i<16;++i) {
        const s32 expected=((s32)i*43-355)*65536+(s32)(65535-i*3117);
        CHECK(result.matrix[i]==expected);
    }
    CHECK(!memcmp(&pool,&saved,sizeof(pool))&&gRaceIntroTimer==219);
    gRaceIntroTimer=0;CHECK(ref_countdown(&result));
    for(i=0;i<16;++i)CHECK(!result.matrix[i]);
    gRaceIntroTimer=-1;CHECK(!ref_countdown(&result)&&result.faults);
    gRaceIntroTimer=461;CHECK(!ref_countdown(&result)&&result.faults);
    gRaceIntroTimer=1;gNumPlayers=2;CHECK(!ref_countdown(&result));gNumPlayers=1;
    gGameMode=GAMEMODE_GP_RACE;CHECK(!ref_countdown(&result));
    gGameMode=GAMEMODE_TIME_ATTACK;gGfxPool=NULL;CHECK(!ref_countdown(&result));
    CHECK(!ref_countdown(NULL));return 0;
}
