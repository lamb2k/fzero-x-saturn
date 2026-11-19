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

/* After ref_race_init(): re-run the race set-up as a 30-racer Grand Prix race.
 * cpu_line is the course's CPU line in host order (0x200 s16); the hooks skip the ROM read. */
int rb_gp_init(int difficulty, unsigned seed, const short* cpu_line) {
    int i;
    static s16 line[0x200];
    for (i = 0; i < 0x200; ++i) line[i] = cpu_line[i];
    gGameMode = GAMEMODE_GP_RACE;
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

/* Racer i: {raceDistance, lateral (+right), speed, energy, maxEnergy}, {lap, position, stateFlags, raceTime, character, unk_39C}. */
void rb_racer(int i, float f[5], int n[6]) {
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
}

/* The game loop's frame counter: CPU racers think on (id & 3) == (frame % 4). Call once per tick. */
extern u32 gGameFrameCount;
void rb_gp_frame(void) { gGameFrameCount++; }
