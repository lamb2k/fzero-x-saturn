/* Duel reference for the attacks lane (claude/attacks): the original race code with two racers,
 * racer 0 the one human player and racer 1 an opponent read from controller 1 (not the CPU driver),
 * so both are pad-driven and the original's contact, attack, damage, spin-out, retirement and KO
 * rules run between them unchanged. HOST reference role only; decomp headers only (the host driver
 * is ref/duel_main.c). MIT; see ../LICENSE. */
#include <string.h>
#include "global.h"
#include "fzx_racer.h"
#include "fzx_course.h"
#include "fzx_game.h"
#include "controller.h"
#include "duel_hooks.h"

extern s32 gGameMode, gNumPlayers, gTotalRacers;
extern s16 gRacersKOd;
extern s16 gSettingVsCom;
extern s16 gPlayerCharacters[];
extern s16 gPlayerMachineSkins[];
extern f32 gPlayerEngine[];
extern s32 gPlayerControlPorts[];
extern Controller gControllers[];
extern Racer gRacers[];
extern RacerPairInfo sRacerPairInfo[];
extern unsigned short ref_pad_button;
extern signed char ref_pad_stick_x, ref_pad_stick_y;
void func_8008969C(void);
void func_80089724(void);
void ref_race_restart(void);
void ref_handling_race_features(void);
void Controller_UpdateInputs(void);
void Racer_Update(void);

static u16 sPrev1;

/* After ref_race_init: a two-racer race (the VS set-up), racer 1 as stock machine character1 at
 * engine1 with the human tuning, then one human player: racer 1 is an opponent for the KO and the
 * attack-damage rules (id >= gNumPlayers), driven by controller 1. Returns the racer count. */
int duel_setup(int character0, float engine0, int character1, float engine1) {
    gGameMode = GAMEMODE_VS_2P;
    gNumPlayers = 2;
    gSettingVsCom = 0;
    gPlayerCharacters[0] = (s16) character0; gPlayerMachineSkins[0] = 0; gPlayerEngine[0] = engine0;
    gPlayerCharacters[1] = (s16) character1; gPlayerMachineSkins[1] = 0; gPlayerEngine[1] = engine1;
    gPlayerControlPorts[0] = 0;
    gPlayerControlPorts[1] = 1;
    func_8008969C();
    func_80089724();
    ref_race_restart();
    ref_handling_race_features();
    gNumPlayers = 1;
    sPrev1 = 0;
    return gTotalRacers;
}

void duel_tick(unsigned short b0, signed char x0, signed char y0, unsigned short b1, signed char x1, signed char y1) {
    Controller* c;
    ref_pad_button = b0;
    ref_pad_stick_x = x0;
    ref_pad_stick_y = y0;
    Controller_UpdateInputs();
    /* Pad 1 is unplugged in the harness's hooks, which disconnects port 1: keep racer 1 on it and
     * write its controller after the update. */
    gPlayerControlPorts[1] = 1;
    c = &gControllers[1];
    c->buttonPressed = (u16) (b1 & ~sPrev1);
    c->buttonReleased = (u16) (sPrev1 & ~b1);
    c->buttonCurrent = b1;
    c->stickX = x1;
    c->stickY = y1;
    sPrev1 = b1;
    Racer_Update();
}

/* Racer k after a tick: pos[3], vel[3], speed, energy, lateral (+ left, as the native body), heading
 * term (body forward . road left), lap distance, spin-out steer; flags, attack state, spin-out timer, KOs so far,
 * ticks since the last human contact (unk_288), last toucher id (-1 none), lap, race time. */
void duel_state(int k, float f[12], int n[8]) {
    const Racer* r = &gRacers[k];
    const RacerSegmentPositionInfo* p = &r->segmentPositionInfo;
    f[0] = p->pos.x; f[1] = p->pos.y; f[2] = p->pos.z;
    f[3] = r->velocity.x; f[4] = r->velocity.y; f[5] = r->velocity.z;
    f[6] = r->speed; f[7] = r->energy;
    f[8] = r->segmentBasis.z.x * p->segmentDisplacement.x + r->segmentBasis.z.y * p->segmentDisplacement.y +
           r->segmentBasis.z.z * p->segmentDisplacement.z;
    f[9] = r->trueBasis.x.x * r->segmentBasis.z.x + r->trueBasis.x.y * r->segmentBasis.z.y +
           r->trueBasis.x.z * r->segmentBasis.z.z;
    f[10] = r->lapDistance;
    f[11] = r->unk_238;   /* spin-out steer (its side is a random draw in Racer_ReceiveDamage) */
    n[0] = (int) r->stateFlags; n[1] = r->attackState; n[2] = r->spinOutTimer; n[3] = gRacersKOd;
    n[4] = r->unk_288; n[5] = r->unk_28C ? r->unk_28C->id : -1; n[6] = r->lap; n[7] = r->raceTime;
}

/* C-092 logging (ref/duel_hooks.h): records drained by the host driver after every tick. */
#define DUEL_LOG_MAX 64
typedef struct { s32 kind, tick, a, b; f32 f[10]; } DuelRec;
static DuelRec sLog[DUEL_LOG_MAX];
static s32 sLogN, sTick = -1, sForceTick = -1, sForceValue = -1;
int ref_duel_ctx;

static DuelRec* duel_rec(s32 kind, s32 a, s32 b) {
    DuelRec* r;
    if (sLogN >= DUEL_LOG_MAX) {
        return &sLog[DUEL_LOG_MAX - 1];   /* never reached by two racers; the last record is overwritten */
    }
    r = &sLog[sLogN++];
    r->kind = kind; r->tick = sTick; r->a = a; r->b = b;
    { s32 i; for (i = 0; i < 10; i++) r->f[i] = 0.0f; }
    return r;
}

void ref_duel_ko(struct Racer* victim, struct Racer* attacker, int window) {
    DuelRec* r = duel_rec('K', ((Racer*) victim)->id, ((Racer*) attacker)->id);
    r->f[0] = (f32) window;
}

void ref_duel_damage(struct Racer* racer, float amount) {
    const Racer* x = (const Racer*) racer;
    DuelRec* r = duel_rec('D', x->id, ref_duel_ctx);
    r->f[0] = amount;
    r->f[1] = x->energy;   /* before the call */
    r->f[2] = (f32) (s32) x->stateFlags;
}

void ref_duel_touch(struct Racer* racer, struct Racer* racer2, int was_colliding, float d, float impact, float share_racer,
                    float share_racer2) {
    DuelRec* r = duel_rec('T', ((Racer*) racer)->id, ((Racer*) racer2)->id);
    r->f[0] = (f32) was_colliding; r->f[1] = d; r->f[2] = impact; r->f[3] = share_racer; r->f[4] = share_racer2;
    r->f[5] = (f32) ((Racer*) racer)->attackState; r->f[6] = (f32) ((Racer*) racer2)->attackState;
}

int ref_duel_closing(struct Racer* racer, struct Racer* racer2, float dx, float dy, float dz, float dvx, float dvy, float dvz,
                     float closing) {
    const int natural = closing <= 0.0f;
    const int taken = (sTick == sForceTick && sForceValue >= 0) ? sForceValue : natural;
    DuelRec* r = duel_rec('C', ((Racer*) racer)->id, ((Racer*) racer2)->id);
    r->f[0] = closing; r->f[1] = (f32) natural; r->f[2] = (f32) taken;
    r->f[3] = dx; r->f[4] = dy; r->f[5] = dz; r->f[6] = dvx; r->f[7] = dvy; r->f[8] = dvz;
    return taken;
}

void ref_duel_push(struct Racer* racer, struct Racer* racer2, float k_racer, float k_racer2, float px, float py, float pz) {
    DuelRec* r = duel_rec('H', ((Racer*) racer)->id, ((Racer*) racer2)->id);
    r->f[0] = k_racer; r->f[1] = k_racer2; r->f[2] = px; r->f[3] = py; r->f[4] = pz;
}

void duel_log_tick(int tick) { sTick = tick; sLogN = 0; ref_duel_ctx = 0; }
void duel_force(int tick, int value) { sForceTick = tick; sForceValue = value; }
int duel_log_count(void) { return sLogN; }
void duel_log_get(int i, int* kind, int* tick, int* a, int* b, float f[10]) {
    s32 k;
    *kind = sLog[i].kind; *tick = sLog[i].tick; *a = sLog[i].a; *b = sLog[i].b;
    for (k = 0; k < 10; k++) f[k] = sLog[i].f[k];
}

/* The pair's state as the next tick's contact test reads it: along gap and the colliding bit. */
void duel_pair(float* trail, int* colliding) {
    const RacerPairInfo* p = &sRacerPairInfo[0];
    *trail = p->trailToLeadDistance;
    *colliding = p->areColliding ? 1 : 0;
}

/* Reference-only settings before a tick (the falls card's boundary tests, C-096 item 5): racer k's age since the
 * last human contact (unk_288), and its toucher (unk_28C: -1 none, else that racer). */
void duel_set_age(int k, int age) { gRacers[k].unk_288 = age; }
void duel_set_toucher(int k, int who) { gRacers[k].unk_28C = who < 0 ? NULL : &gRacers[who]; }

/* Reference-only perturbation before a tick: racer k's position (field 0) or velocity (field 1), one axis. */
void duel_perturb(int k, int field, int axis, float delta) {
    Racer* r = &gRacers[k];
    f32* v = field == 0 ? &r->segmentPositionInfo.pos.x : &r->velocity.x;
    v[axis] += delta;
}


/* Reference-only state load (attacks-3, the matched-start diagnostic): the inverse of duel_bake's state keys. Racer k's baked
 * state fields take the values of v (a source line's floats, already rounded by the caller to the native entry state's
 * format); integer-valued fields and every field the pack does not hold are left as the original run has them. Returns 1 if
 * the key was a state key, 0 otherwise (course and machine keys are ignored). */
static void load_vec(Vec3f* d, const float* v) { d->x = v[0]; d->y = v[1]; d->z = v[2]; }
static void load_mtx(Mtx3F* m, const float* v) {
    m->x.x = v[0]; m->x.y = v[1]; m->x.z = v[2]; m->y.x = v[3]; m->y.y = v[4]; m->y.z = v[5]; m->z.x = v[6]; m->z.y = v[7]; m->z.z = v[8];
}
int duel_load(int k, const char* key, const float* v, int n) {
    Racer* r = &gRacers[k];
    RacerSegmentPositionInfo* p = &r->segmentPositionInfo;
    if (!strcmp(key, "pos") && n == 3) load_vec(&p->pos, v);
    else if (!strcmp(key, "vel") && n == 3) load_vec(&r->velocity, v);
    else if (!strcmp(key, "body") && n == 9) load_mtx(&r->trueBasis, v);
    else if (!strcmp(key, "road") && n == 9) load_mtx(&r->segmentBasis, v);
    else if (!strcmp(key, "gravity_up") && n == 3) load_vec(&r->gravityUp, v);
    else if (!strcmp(key, "tilt_up") && n == 3) load_vec(&r->tiltUp, v);
    else if (!strcmp(key, "ground_up") && n == 3) load_vec(&r->upFromGround, v);
    else if (!strcmp(key, "fwd") && n == 3) load_vec(&p->segmentForward, v);
    else if (!strcmp(key, "spos") && n == 3) load_vec(&p->segmentPos, v);
    else if (!strcmp(key, "disp") && n == 3) load_vec(&p->segmentDisplacement, v);
    else if (!strcmp(key, "ground") && n == 3) load_vec(&p->lastGroundedPos, v);
    else if (!strcmp(key, "bvel") && n == 3) load_vec(&r->unk_5C, v);
    else if (!strcmp(key, "scalars") && n == 14) {
        p->segmentTValue = v[0]; p->segmentLengthProportion = v[1]; p->segmentForwardMagnitude = v[2]; r->heightAboveGround = v[3];
        r->currentRadiusLeft = v[4]; r->currentRadiusRight = v[5]; r->lapDistance = v[6]; r->lapsCompletedDistance = v[7];
        r->raceDistance = v[8]; r->speed = v[9]; r->energy = v[10]; r->unk_17C = v[11]; r->accelerationForce = v[12]; r->unk_238 = v[13];
    } else if (!strcmp(key, "dynamics") && n == 21) {
        r->unk_200 = v[15]; r->driftAttackForce = v[16]; r->jumpBoost = v[17]; r->unk_198 = v[18]; r->unk_234 = v[19]; r->tiltUpInput = v[20];
    } else return 0;
    return 1;
}

/* Physics pack source of racer k (as ref_handling_bake does for racer 0). */
typedef void (*DuelEmit)(const char* key, int a, int b, const float* v, int n);
extern CourseInfo* gCurrentCourseInfo;
extern f32 D_800CF174[];
extern f32 D_800F80A4;
extern s32 gRaceIntroTimer;

static void emit_vec(DuelEmit emit, const char* key, const Vec3f* v) {
    float w[3] = { v->x, v->y, v->z };
    emit(key, 0, 0, w, 3);
}

static void emit_mtx(DuelEmit emit, const char* key, const Mtx3F* m) {
    float w[9] = { m->x.x, m->x.y, m->x.z, m->y.x, m->y.y, m->y.z, m->z.x, m->z.y, m->z.z };
    emit(key, 0, 0, w, 9);
}

void duel_bake(int k, DuelEmit emit) {
    const CourseInfo* c = gCurrentCourseInfo;
    const Racer* r = &gRacers[k];
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
        /* Tube spans carry their joins (as ref_handling_bake). */
        if ((s->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_PIPE ||
            (s->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_CYLINDER ||
            (s->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_HALF_PIPE) {
            float jv[3] = { s->previousJoinEndTValue, s->nextJoinStartTValue, s->joinScale };
            emit("join", i, 0, jv, 3);
        }
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
        float w[] = { (float) r->spinOutTimer, (float) r->attackState, (float) r->driftAttackDirection,
                      (float) r->unk_27C, (float) r->unk_280, (float) r->zButtonTimer, (float) r->rButtonTimer,
                      (float) r->unk_204, (float) r->unk_208, (float) r->unk_20C, (float) r->unk_214,
                      (float) r->lapTimes[0], (float) r->lapTimes[1], (float) r->lapTimes[2],
                      (float) r->completedLapsTime, r->unk_200, r->driftAttackForce, r->jumpBoost, r->unk_198,
                      r->unk_234, r->tiltUpInput };
        emit("dynamics", 0, 0, w, (int) (sizeof(w) / sizeof(w[0])));
    }
}
