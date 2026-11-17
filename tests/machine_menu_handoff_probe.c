/* Read original racer fields for the HOST menu-to-race gate. */
#include "global.h"
#include "fzx_racer.h"
#include "ref.h"
extern Racer gRacers[];
extern Machine gMachines[];
extern f32 gPlayerEngine[];
extern s16 gPlayerMachineSkins[];
extern s16 gPlayerCharacters[];
extern s32 gNumPlayers;
extern f32 func_8008960C(f32);

void machine_handoff_reference(void) {
    /* Independently reproduce original settings entry and confirmation before
     * the legacy flag-off race bridge; this is not the new bridge setter. */
    gNumPlayers=1;gPlayerCharacters[0]=0;gPlayerMachineSkins[0]=0;gPlayerEngine[0]=0.5f;
    func_8008969C();func_80089724();
}

int machine_handoff_probe(unsigned int skin,float engine,int source_expected,unsigned int out[27]) {
    const Racer *r=&gRacers[0];
    const Machine *m=&gMachines[0];
    const float stats[20]={r->unk_1A8,r->boostEnergyUsage,r->unk_1B0,r->unk_1B4,
        r->unk_1B8,r->unk_1BC,r->unk_1C0,r->unk_1C4,r->unk_1C8,r->unk_1CC,
        r->unk_1D0,r->accelerationForce,r->driftAttackForce,r->jumpBoost,
        r->unk_1E0,r->unk_1E4,r->unk_1E8,r->unk_1EC,r->unk_1F8,r->unk_1FC};
    if(r->character!=0)return 1;
    if(r->machineSkinIndex!=skin)return 2;
    if(gPlayerMachineSkins[0]!=skin)return 3;
    if(gPlayerEngine[0]!=engine)return 4;
    if(source_expected && r->unk_1A8!=func_8008960C(engine))return 5;
    if(r->bodyR!=m->red[skin] || r->bodyG!=m->green[skin] || r->bodyB!=m->blue[skin])return 6;
    out[0]=r->character;out[1]=r->machineSkinIndex;
    out[2]=r->bodyR;out[3]=r->bodyG;out[4]=r->bodyB;
    memcpy(&out[5],&gPlayerEngine[0],4);
    memcpy(&out[6],stats,sizeof(stats));out[26]=0;
    return 0;
}
