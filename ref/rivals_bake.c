/* Rival line bake, game side: reads the loaded course's spline through the
 * original course functions (decomp headers). HOST tool only, never part of
 * the Saturn image. The host driver (rivals_main.c) sees plain C types. */
#include "global.h"
#include "fzx_racer.h"
#include "fzx_course.h"
#include "fzx_game.h"

extern CourseInfo* gCurrentCourseInfo;
extern Racer gRacers[];
extern f32 Course_SplineGetLengthInfo(CourseSegment* segment, f32 targetT, f32* lengthFromStart);
extern void Course_SplineGetPosition(CourseSegment* segment, f32 t, Vec3f* pos);
extern f32 Course_SplineGetBasis(CourseSegment* segment, f32 t, Mtx3F* basis, f32 lengthProportionAlongSegment);

int rb_segment_count(void) { return gCurrentCourseInfo->segmentCount; }
float rb_course_length(void) { return gCurrentCourseInfo->length; }

void rb_segment(int index, float out[5]) {
    CourseSegment* s = &gCurrentCourseInfo->courseSegments[index];
    out[0] = s->lengthFromStart;
    out[1] = s->length;
    out[2] = s->radiusLeft;
    out[3] = s->radiusRight;
    out[4] = (float)(s->trackSegmentInfo & TRACK_SHAPE_MASK);
}

/* Position, basis rows (forward, up, left) and absolute course distance at t. */
void rb_sample(int index, float t, float out[13]) {
    CourseSegment* s = &gCurrentCourseInfo->courseSegments[index];
    Vec3f p;
    Mtx3F b;
    f32 dist = 0.0f;
    Course_SplineGetPosition(s, t, &p);
    (void)Course_SplineGetLengthInfo(s, t, &dist);
    (void)Course_SplineGetBasis(s, t, &b, t);
    out[0] = p.x; out[1] = p.y; out[2] = p.z;
    out[3] = b.x.x; out[4] = b.x.y; out[5] = b.x.z;
    out[6] = b.y.x; out[7] = b.y.y; out[8] = b.y.z;
    out[9] = b.z.x; out[10] = b.z.y; out[11] = b.z.z;
    out[12] = dist;
}

/* Player 0 in line space: race distance, lateral (+ right, as unk_33C), speed. */
void rb_player(float out[3]) {
    const Racer* r = &gRacers[0];
    out[0] = r->raceDistance;
    out[1] = -(r->segmentBasis.z.x * r->segmentPositionInfo.segmentDisplacement.x +
               r->segmentBasis.z.y * r->segmentPositionInfo.segmentDisplacement.y +
               r->segmentBasis.z.z * r->segmentPositionInfo.segmentDisplacement.z);
    out[2] = r->speed;
}

/* ---- Reference Grand Prix (HOST reference role only; never on the Saturn) ---- */
extern s32 gGameMode, gNumPlayers, gDifficulty, gTotalRacers, gTotalLapCount;
extern s32 D_800E5FF0;
extern s32 gRandSeed1, gRandSeed2;
extern u32 gRandMask1, gRandMask2;
extern s16* D_i3_8013DBE8;
extern void Racer_Init(void);
extern void func_i3_80116C4C(void);

/* The CPU tuning tables (cpu.c) are indexed by gCourseIndex, which the bridge leaves at the
 * first course while the served record fills that slot. For another course (its original
 * index n, gp_ref_main.c --tuning-course), its rows are copied into slot 0 before rb_gp_init. */
extern f32 D_i3_8013DC18[], D_i3_8013DFA0[];
extern u32 D_i3_8013E328[], D_i3_8013E4A8[], D_i3_8013E628[];
extern s16 D_i3_8013E94C[];
int rb_gp_tuning_course(int n) {
    int d;
    if (n < 0 || n >= 24) return -1;
    for (d = 0; d < 4 && n > 0; ++d) {
        D_i3_8013DC18[d] = D_i3_8013DC18[n * 4 + d];
        D_i3_8013DFA0[d] = D_i3_8013DFA0[n * 4 + d];
        D_i3_8013E328[d] = D_i3_8013E328[n * 4 + d];
        D_i3_8013E4A8[d] = D_i3_8013E4A8[n * 4 + d];
        D_i3_8013E628[d] = D_i3_8013E628[n * 4 + d];
    }
    D_i3_8013E94C[0] = D_i3_8013E94C[n];
    return 0;
}

/* After ref_race_init(): re-run the race set-up as a 30-racer Grand Prix race.
 * cpu_line is the course's CPU line in host order (0x200 s16); the hooks skip the ROM read. */
/* The game mode the race runs as (claude/modes, HOST only): 0 = Grand Prix, else the GAMEMODE_ value (Practice, Death Race). */
static int rb_mode_override;
void rb_gp_mode(int mode) { rb_mode_override = mode; }
int rb_gp_init(int difficulty, unsigned seed, const short* cpu_line) {
    int i;
    static s16 line[0x200];
    for (i = 0; i < 0x200; ++i) line[i] = cpu_line[i];
    gGameMode = rb_mode_override ? rb_mode_override : GAMEMODE_GP_RACE;
    gDifficulty = difficulty;
    gNumPlayers = 1;
    gTotalLapCount = 3;
    D_800E5FF0 = 0;                    /* first race of a cup: deals the CPU characters */
    gRandSeed1 += (s32)(seed * 2654435761u);
    gRandSeed2 ^= (s32)(seed * 40503u + 977u);
    D_i3_8013DBE8 = line;
    Racer_Init();
    D_i3_8013DBE8 = line;
    for (i = 0; i < 0x200; ++i) line[i] = cpu_line[i];
    return gTotalRacers;
}

/* Racer i: {raceDistance, lateral (+right), speed, energy, maxEnergy},
 * {lap, position, stateFlags, raceTime, character, unk_39C (lap target ms), unk_352 (chase target, 255 none), unk_368 (pace rank)}. */
void rb_racer(int i, float f[5], int n[8]) {
    const Racer* r = &gRacers[i];
    f[0] = r->raceDistance;
    f[1] = -(r->segmentBasis.z.x * r->segmentPositionInfo.segmentDisplacement.x +
             r->segmentBasis.z.y * r->segmentPositionInfo.segmentDisplacement.y +
             r->segmentBasis.z.z * r->segmentPositionInfo.segmentDisplacement.z);
    f[2] = r->speed;
    f[3] = r->energy;
    f[4] = r->maxEnergy;
    n[0] = r->lap;
    n[1] = r->position;
    n[2] = r->stateFlags;
    n[3] = r->raceTime;
    n[4] = r->character;
    n[5] = r->unk_39C;
    n[6] = r->unk_352;
    n[7] = (int)r->unk_368;
}

/* Racer i's displacement from the course line along the segment's up axis (the other component
 * of rb_racer's lateral): with the lateral, the angle round a pipe or cylinder (claude/jack-gp). */
float rb_racer_up(int i) {
    const Racer* r = &gRacers[i];
    return r->segmentBasis.y.x * r->segmentPositionInfo.segmentDisplacement.x +
           r->segmentBasis.y.y * r->segmentPositionInfo.segmentDisplacement.y +
           r->segmentBasis.y.z * r->segmentPositionInfo.segmentDisplacement.z;
}

/* C-118 replay input: racer i's physical pose on the course, which the game keeps tracking after the finish (its
 * official raceDistance stops there). f: lapDistance (the segment walk's distance from the lap line, course.c
 * func_8009DFA0), lateral and up (as rb_racer and rb_racer_up), world position x y z; n: segment index (-1 none),
 * stateFlags, lap. */
void rb_racer_phys(int i, float f[6], int n[3]) {
    const Racer* r = &gRacers[i];
    f[0] = r->lapDistance;
    f[1] = -(r->segmentBasis.z.x * r->segmentPositionInfo.segmentDisplacement.x +
             r->segmentBasis.z.y * r->segmentPositionInfo.segmentDisplacement.y +
             r->segmentBasis.z.z * r->segmentPositionInfo.segmentDisplacement.z);
    f[2] = r->segmentBasis.y.x * r->segmentPositionInfo.segmentDisplacement.x +
           r->segmentBasis.y.y * r->segmentPositionInfo.segmentDisplacement.y +
           r->segmentBasis.y.z * r->segmentPositionInfo.segmentDisplacement.z;
    f[3] = r->segmentPositionInfo.pos.x; f[4] = r->segmentPositionInfo.pos.y; f[5] = r->segmentPositionInfo.pos.z;
    n[0] = r->segmentPositionInfo.courseSegment ? r->segmentPositionInfo.courseSegment->segmentIndex : -1;
    n[1] = r->stateFlags;
    n[2] = r->lap;
}

/* Diagnostic: racer i's displacement from the course line, world axes (y up). */
void rb_racer_disp(int i, float d[3]) {
    const Racer* r = &gRacers[i];
    d[0] = r->segmentPositionInfo.segmentDisplacement.x; d[1] = r->segmentPositionInfo.segmentDisplacement.y;
    d[2] = r->segmentPositionInfo.segmentDisplacement.z;
}

/* Race_Init's surface set-up (pit, dash, dirt, ice), jumps and landmines, as handling_probe.c's
 * ref_handling_race_features: the bridge's race leaves them out. --zones 1 in fzx-gp-ref: every
 * racer, CPU racers included, then meets the course's zones as in the game. */
void Course_LandminesViewInteractDataInit(void);
void Course_JumpsViewInteractDataInit(void);
void Course_EffectsViewInteractDataInit(bool arg0);
void rb_gp_zones(void) {
    Course_LandminesViewInteractDataInit();
    Course_JumpsViewInteractDataInit();
    Course_EffectsViewInteractDataInit(false);
}

/* The course's surface zones after rb_gp_zones, one row per effect: segment, type, t0, t1, the two
 * bound vectors and whether the segment bounds them by side offset (else by edge vectors). */
int rb_zones(float out[][11], int max) {
    const CourseInfo* c = gCurrentCourseInfo;
    int n = 0, i;
    for (i = 0; i < c->segmentCount; ++i) {
        const CourseSegment* s = &c->courseSegments[i];
        const Effect* e;
        for (e = s->effectsStart; e != NULL && e != s->effectsEnd; e++) {
            if (n < max) {
                float* o = out[n];
                o[0] = (float)i; o[1] = (float)(e->effectType & COURSE_EFFECT_MASK);
                o[2] = e->segmentTValueStart; o[3] = e->segmentTValueEnd;
                o[4] = e->unk_0C.x; o[5] = e->unk_0C.y; o[6] = e->unk_0C.z;
                o[7] = e->unk_18.x; o[8] = e->unk_18.y; o[9] = e->unk_18.z;
                o[10] = (s->trackSegmentInfo & 0x8000000) ? 1.0f : 0.0f;
            }
            ++n;
        }
    }
    return n;
}

/* The game loop's frame counter: CPU racers think on (id & 3) == (frame % 4). Call once per tick. */
extern u32 gGameFrameCount;
void rb_gp_frame(void) { gGameFrameCount++; }

/* CPU racer diagnostics (claude/line-field-5, HOST reference only): the state behind the rivals' tube,
 * wall, fall and knock-out behaviour. f: {unk_370 (tube steer bias timer), lastStickX, energy, speed,
 * segment length proportion}; n: {stateFlags, shape of the current segment, unk_288 (ticks since a
 * player's contact), attacker id (unk_28C, -1 none), segment index, spinOutTimer, attackState, lap}. */
void rb_cpu_diag(int i, float f[5], int n[8]) {
    const Racer* r = &gRacers[i];
    f[0] = (float)r->unk_370;
    f[1] = (float)r->lastStickX;
    f[2] = r->energy;
    f[3] = r->speed;
    f[4] = r->segmentPositionInfo.segmentLengthProportion;
    n[0] = (int)r->stateFlags;
    n[1] = r->segmentPositionInfo.courseSegment ? (int)(r->segmentPositionInfo.courseSegment->trackSegmentInfo & TRACK_SHAPE_MASK) : -1;
    n[2] = r->unk_288;
    n[3] = r->unk_28C ? r->unk_28C->id : -1;
    n[4] = r->segmentPositionInfo.courseSegment ? r->segmentPositionInfo.courseSegment->segmentIndex : -1;
    n[5] = r->spinOutTimer;
    n[6] = r->attackState;
    n[7] = r->lap;
}

/* More of the same racer: {unk_33C (lateral target), pos x, y, z, currentRadiusLeft, currentRadiusRight};
 * n: {boostTimer, machineLod}. */
void rb_cpu_diag2(int i, float f[6], int n[2]) {
    const Racer* r = &gRacers[i];
    f[0] = r->unk_33C;
    f[1] = r->segmentPositionInfo.pos.x; f[2] = r->segmentPositionInfo.pos.y; f[3] = r->segmentPositionInfo.pos.z;
    f[4] = r->currentRadiusLeft; f[5] = r->currentRadiusRight;
    n[0] = r->boostTimer; n[1] = r->machineLod;
}

/* KO-credit probe (claude/line-field-5): set racer i's ticks since a player's contact (unk_288) and its
 * attacker (unk_28C, racer `by`, or none when by < 0); read the KO count and racer i's pending regain. */
extern s16 gRacersKOd;
void rb_set_ko_age(int i, int age, int by) {
    gRacers[i].unk_288 = age;
    gRacers[i].unk_28C = by >= 0 ? &gRacers[by] : NULL;
}
int rb_ko_count(void) { return gRacersKOd; }
float rb_regain(int i) { return gRacers[i].energyRegain; }
