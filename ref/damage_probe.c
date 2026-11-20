/* Damage-source probe for the reference Grand Prix (attacks lane): per racer, the state a host driver
 * needs to attribute each energy loss (position, attack state, flags, energy, boost timer).
 * HOST reference role only; decomp headers only. MIT; see ../LICENSE. */
#include "global.h"
#include "fzx_racer.h"

extern Racer gRacers[];

/* Racer i: pos[3], energy, max energy; attack state, state flags, boost timer, spin-out timer. */
void dp_racer(int i, float f[5], int n[4]) {
    const Racer* r = &gRacers[i];
    f[0] = r->segmentPositionInfo.pos.x;
    f[1] = r->segmentPositionInfo.pos.y;
    f[2] = r->segmentPositionInfo.pos.z;
    f[3] = r->energy;
    f[4] = r->maxEnergy;
    n[0] = r->attackState;
    n[1] = (int) r->stateFlags;
    n[2] = r->boostTimer;
    n[3] = r->spinOutTimer;
}
