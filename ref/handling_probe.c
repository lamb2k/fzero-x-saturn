/* E1 handling probe: reads racer 0 for the plays-right fixtures. HOST only,
 * built by ref/handling_build.sh. ref_handling reads state and never writes it. MIT; see ../LICENSE. */
#include "global.h"
#include "fzx_course.h"
#include "fzx_racer.h"
#include "fzx_game.h"
#include "handling.h"

extern Racer gRacers[];
extern s32 gRaceIntroTimer;

static float dot3(const Vec3f* a, const Vec3f* b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

void ref_handling(RefHandling* h) {
    const Racer* r = &gRacers[0];
    s32 i;

    h->lateral = dot3(&r->segmentPositionInfo.segmentDisplacement, &r->segmentBasis.z);
    h->radius_left = r->currentRadiusLeft;
    h->radius_right = r->currentRadiusRight;
    h->height = r->heightAboveGround;
    h->lap_distance = r->lapDistance;
    h->race_distance = r->raceDistance;
    h->v_side = r->unk_5C.x;
    h->v_up = r->unk_5C.y;
    h->v_forward = r->unk_5C.z;
    h->speed = r->speed;
    h->heading_side = dot3(&r->trueBasis.x, &r->segmentBasis.z);
    h->heading_forward = dot3(&r->trueBasis.x, &r->segmentBasis.x);
    h->forward[0] = r->trueBasis.x.x;
    h->forward[1] = r->trueBasis.x.y;
    h->forward[2] = r->trueBasis.x.z;
    h->pos[0] = r->segmentPositionInfo.pos.x;
    h->pos[1] = r->segmentPositionInfo.pos.y;
    h->pos[2] = r->segmentPositionInfo.pos.z;
    h->accel_force = r->accelerationForce;
    h->drift_force = r->driftAttackForce;
    h->slide_boost = r->unk_200;
    h->energy = r->energy;
    h->max_energy = r->maxEnergy;
    h->flags = r->stateFlags;
    h->boost_timer = r->boostTimer;
    h->spin_timer = r->spinOutTimer;
    h->attack_state = r->attackState;
    h->grip_a = r->unk_204;
    h->grip_b = r->unk_208;
    h->slide_count = r->unk_20C;
    h->a_tap = r->unk_214;
    h->segment = r->segmentPositionInfo.courseSegment ? r->segmentPositionInfo.courseSegment->segmentIndex : -1;
    h->lap = r->lap;
    h->laps_completed = r->lapsCompleted;
    h->race_time = r->raceTime;
    for (i = 0; i < 3; i++) h->lap_times[i] = r->lapTimes[i];
    h->intro = gRaceIntroTimer;
}

/* Race_Init also builds the per-segment interaction data for surface effects
 * (pit, dash, dirt, ice), jumps and landmines. The legacy bridge leaves these
 * out, so its races have no such zones; the fixtures need them. Call once
 * after ref_race_init. */
void Course_LandminesViewInteractDataInit(void);
void Course_JumpsViewInteractDataInit(void);
void Course_EffectsViewInteractDataInit(bool arg0);

void ref_handling_race_features(void) {
    Course_LandminesViewInteractDataInit();
    Course_JumpsViewInteractDataInit();
    Course_EffectsViewInteractDataInit(false);
}
