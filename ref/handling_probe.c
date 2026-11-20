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
    h->v_lateral = dot3(&r->velocity, &r->segmentBasis.z);
}

/* Race_Init also builds the per-segment interaction data for surface effects
 * (pit, dash, dirt, ice), jumps and landmines. The legacy bridge leaves these
 * out, so its races have no such zones; the fixtures need them. Call once
 * after ref_race_init. */
void Course_LandminesViewInteractDataInit(void);
void Course_JumpsViewInteractDataInit(void);
void Course_EffectsViewInteractDataInit(bool arg0);

/* Re-initialise racer 0 as another stock machine at another engine setting, the way
 * leaving the machine settings does (func_8008969C / func_80089724), then Racer_Init
 * through ref_race_restart. Call after ref_race_init, before the zone set-up. */
extern s16 gPlayerCharacters[];
extern s16 gPlayerMachineSkins[];
extern f32 gPlayerEngine[];
void func_8008969C(void);
void func_80089724(void);
void ref_race_restart(void);

int ref_handling_select_machine(int character, float engine) {
    if (character < 0 || character >= 30 || !(engine >= 0.0f && engine <= 1.0f)) return 1;
    gPlayerCharacters[0] = (s16) character;
    gPlayerMachineSkins[0] = 0;
    gPlayerEngine[0] = engine;
    func_8008969C();
    func_80089724();
    ref_race_restart();
    return 0;
}

void ref_handling_race_features(void) {
    Course_LandminesViewInteractDataInit();
    Course_JumpsViewInteractDataInit();
    Course_EffectsViewInteractDataInit(false);
}

/* Physics pack source: the course spline, zones, jumps, mines, the machine's derived
 * constants and racer 0's state, after ref_race_init and ref_handling_race_features and
 * before the first tick, as records handed to emit (key, two integers, n floats).
 * The decomp headers cannot sit beside stdio, so the driver does the printing. The
 * output is derived from the course record and the ROM: local only. */
extern CourseInfo* gCurrentCourseInfo;
extern f32 D_800CF174[];
extern f32 D_800F80A4;

static void emit_vec(RefBakeEmit emit, const char* key, const Vec3f* v) {
    float w[3] = { v->x, v->y, v->z };
    emit(key, 0, 0, w, 3);
}

static void emit_mtx(RefBakeEmit emit, const char* key, const Mtx3F* m) {
    float w[9] = { m->x.x, m->x.y, m->x.z, m->y.x, m->y.y, m->y.z, m->z.x, m->z.y, m->z.z };
    emit(key, 0, 0, w, 9);
}

void ref_handling_bake(RefBakeEmit emit) {
    const CourseInfo* c = gCurrentCourseInfo;
    const Racer* r = &gRacers[0];
    const RacerSegmentPositionInfo* p = &r->segmentPositionInfo;
    s32 i;

    {
        float w[1] = { c->length };
        emit("course", c->segmentCount, 0, w, 1);
    }
    for (i = 0; i < c->segmentCount; i++) {
        const CourseSegment* s = &c->courseSegments[i];
        const Effect* e;
        const Jump* j;
        const Landmine* m;
        float w[23] = { s->pos.x, s->pos.y, s->pos.z, s->up.x, s->up.y, s->up.z, s->radiusLeft, s->radiusRight,
                        s->tension, s->length, s->lengthFromStart, s->quarterMarkPos.x, s->quarterMarkPos.y,
                        s->quarterMarkPos.z, s->halfMarkPos.x, s->halfMarkPos.y, s->halfMarkPos.z,
                        s->threeQuarterMarkPos.x, s->threeQuarterMarkPos.y, s->threeQuarterMarkPos.z,
                        s->quarterMarkLength, s->halfMarkLength, s->threeQuarterMarkLength };
        emit("seg", i, s->trackSegmentInfo, w, 23);
        /* Tube spans (pipe, cylinder, half-pipe) also carry their joins: the t where the join
         * with the previous span ends, where the one with the next starts, and the join scale
         * (racer.c func_8008EC98 / func_8008F550). Road courses keep their source unchanged. */
        if ((s->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_PIPE ||
            (s->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_CYLINDER ||
            (s->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_HALF_PIPE) {
            float jv[3] = { s->previousJoinEndTValue, s->nextJoinStartTValue, s->joinScale };
            emit("join", i, 0, jv, 3);
        }
        if (s->segmentIndex != i || s->next != &c->courseSegments[(i + 1) % c->segmentCount] ||
            s->prev != &c->courseSegments[(i + c->segmentCount - 1) % c->segmentCount])
            emit("error-unlinked", i, 0, w, 0);
        for (e = s->effectsStart; e != NULL && e != s->effectsEnd; e++) {
            float v[8] = { e->segmentTValueStart, e->segmentTValueEnd, e->unk_0C.x, e->unk_0C.y, e->unk_0C.z,
                           e->unk_18.x, e->unk_18.y, e->unk_18.z };
            emit("zone", i, e->effectType, v, 8);
        }
        for (j = s->jumpsStart; j != NULL && j != s->jumpsEnd; j++) {
            float v[15] = { j->pos.x, j->pos.y, j->pos.z, j->basis.x.x, j->basis.x.y, j->basis.x.z, j->basis.y.x,
                            j->basis.y.y, j->basis.y.z, j->basis.z.x, j->basis.z.y, j->basis.z.z,
                            j->dimensions.x, j->dimensions.y, j->dimensions.z };
            emit("jump", i, 0, v, 15);
        }
        for (m = s->landminesStart; m != NULL && m != s->landminesEnd; m++) {
            float v[3] = { m->pos.x, m->pos.y, m->pos.z };
            emit("mine", i, 0, v, 3);
        }
    }
    {
        float w[] = { r->unk_1B0, r->unk_1B4, r->unk_1B8, r->unk_1BC, r->unk_1C0, r->unk_1C4, r->unk_1C8, r->unk_1CC,
                      r->unk_1D0, r->unk_1E0, r->unk_1E4, r->unk_1E8, r->unk_1EC, r->unk_1F8, r->unk_1FC, r->unk_234,
                      r->unk_31C, r->unk_320, r->unk_324, r->unk_328, r->maxEnergy, r->boostEnergyUsage,
                      r->energyIncrease, D_800F80A4, D_800CF174[BOOST_E] };
        emit("machine", 0, 0, w, (int) (sizeof(w) / sizeof(w[0])));
    }
    {
        float w[6] = { (float) r->lap, (float) r->lapsCompleted, (float) r->unk_2A4, (float) r->raceTime,
                       (float) gRaceIntroTimer, (float) r->boostTimer };
        emit("state", r->stateFlags, p->courseSegment->segmentIndex, w, 6);
    }
    emit_vec(emit, "pos", &p->pos);
    emit_vec(emit, "vel", &r->velocity);
    emit_mtx(emit, "body", &r->trueBasis);
    emit_mtx(emit, "road", &r->segmentBasis);
    emit_vec(emit, "gravity_up", &r->gravityUp);
    emit_vec(emit, "tilt_up", &r->tiltUp);
    emit_vec(emit, "ground_up", &r->upFromGround);
    emit_vec(emit, "fwd", &p->segmentForward);
    emit_vec(emit, "spos", &p->segmentPos);
    emit_vec(emit, "disp", &p->segmentDisplacement);
    emit_vec(emit, "ground", &p->lastGroundedPos);
    emit_vec(emit, "bvel", &r->unk_5C);
    {
        float w[] = { p->segmentTValue, p->segmentLengthProportion, p->segmentForwardMagnitude, r->heightAboveGround,
                      r->currentRadiusLeft, r->currentRadiusRight, r->lapDistance, r->lapsCompletedDistance,
                      r->raceDistance, r->speed, r->energy, r->unk_17C, r->accelerationForce, r->unk_238 };
        emit("scalars", 0, 0, w, (int) (sizeof(w) / sizeof(w[0])));
    }
    {
        /* Everything else the step carries from tick to tick, so a native run can start
         * from any reference tick (state resync). Integers are exact as floats. */
        float w[] = { (float) r->spinOutTimer, (float) r->attackState, (float) r->driftAttackDirection,
                      (float) r->unk_27C, (float) r->unk_280, (float) r->zButtonTimer, (float) r->rButtonTimer,
                      (float) r->unk_204, (float) r->unk_208, (float) r->unk_20C, (float) r->unk_214,
                      (float) r->lapTimes[0], (float) r->lapTimes[1], (float) r->lapTimes[2],
                      (float) r->completedLapsTime, r->unk_200, r->driftAttackForce, r->jumpBoost, r->unk_198,
                      r->unk_234, r->tiltUpInput };
        emit("dynamics", 0, 0, w, (int) (sizeof(w) / sizeof(w[0])));
    }
}
