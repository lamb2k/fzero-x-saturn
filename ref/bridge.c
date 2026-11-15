/* Reference harness bridge: sets up and steps the original race code.
 * MIT; see ../LICENSE. Compiled with the decomp's headers only. */
#include "global.h"
#include "fzx_course.h"
#include "fzx_racer.h"
#include "fzx_game.h"
#include "controller.h"
#include "ref.h"

int ref_course_ready;
int ref_course_loads;
unsigned char ref_course_bytes[REF_COURSE_RECORD_SIZE];
unsigned short ref_pad_button;
signed char ref_pad_stick_x, ref_pad_stick_y;

extern Racer gRacers[];
extern s32 gGameMode;
extern s32 gTotalLapCount;
extern s16* D_i3_8013DBE8;
/* The CPU driver's per-segment speed line. func_i3_80116C4C takes it from the arena, which is a
 * stub here (NULL); the original fills it from ROM in Cpu_InitRacer, a read the hooks skip, so it
 * stays zero. Only the CPU driver reads it, and racer 0 becomes CPU-driven when it finishes (the
 * original's run-out lap): without storage that lap dereferences NULL. */
static s16 sCpuLine[0x200];
extern s32 gNumPlayers;
extern s32 gCourseIndex;
extern s16 gPlayerCharacters[];
extern f32 gPlayerEngine[];
extern s32 gPlayerControlPorts[];
extern s32 gControllersConnected;
extern s32 gRaceIntroTimer;
extern s32 gRandSeed1, gRandSeed2;
extern u32 gRandMask1, gRandMask2;
/* The generators as Course_Init left them, before the racer steps: a restart puts them back, so a restarted race
 * replays exactly as a fresh one (explosion jitter draws on them). */
static s32 sRaceSeed1, sRaceSeed2;
static u32 sRaceMask1, sRaceMask2;
extern OSPiHandle* gCartRomHandle;

extern GfxPool* gGfxPool;
extern CourseFeaturesInfo gCourseFeaturesInfo;
extern CourseFeature gCourseFeatures[];
extern CourseEffectsInfo gCourseEffectsInfo;
extern CourseEffect gCourseEffects[];

static GfxPool sGfxPool;    /* The frame loop's graphics buffer: the intro camera writes matrices to it. */
static OSPiHandle sCartRom; /* The boot code opens the cartridge; the harness only needs a handle. */

static u32 swap32(u32 v) {
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static u16 swap16(u16 v) {
    return (u16) ((v >> 8) | (v << 8));
}

int ref_load_course(const unsigned char* record, int big_endian) {
    CourseDataDisk* c = (CourseDataDisk*) ref_course_bytes;
    s32 i;

    _Static_assert(sizeof(CourseDataDisk) == REF_COURSE_RECORD_SIZE, "CourseDataDisk layout");
    const u16 probe = 1;
    const int host_big = *(const u8*) &probe == 0;

    memcpy(ref_course_bytes, record, REF_COURSE_RECORD_SIZE);
    if (big_endian != host_big) { /* The same fields the fork's CourseData_FromRom swaps. */
        c->checksum = swap32(c->checksum);
        for (i = 0; i < 64; i++) {
            u32* pos = (u32*) &c->controlPoint[i].pos;
            pos[0] = swap32(pos[0]);
            pos[1] = swap32(pos[1]);
            pos[2] = swap32(pos[2]);
            c->controlPoint[i].radiusLeft = (s16) swap16((u16) c->controlPoint[i].radiusLeft);
            c->controlPoint[i].radiusRight = (s16) swap16((u16) c->controlPoint[i].radiusRight);
            c->controlPoint[i].trackSegmentInfo = (s32) swap32((u32) c->controlPoint[i].trackSegmentInfo);
            c->bankAngle[i] = (s16) swap16((u16) c->bankAngle[i]);
        }
    }
    ref_course_ready = 1;
    return 0;
}

int ref_race_init(void) {
    gCartRomHandle = &sCartRom;
    gGfxPool = &sGfxPool;

    /* Boot-time set-up the race code relies on (Game init in sys_gfx.c). */
    Math_SinTableInit();
    /* func_800742FC (boot) points the feature and effect tables at their storage; a course
     * with gates, pits or jumps writes through them in Course_Init. */
    gCourseFeaturesInfo.features = gCourseFeatures;
    gCourseEffectsInfo.effects = gCourseEffects;

    /* One human racer in Time Attack, as the menus leave it. */
    func_8008D33C(); /* Default machines. */
    gGameMode = GAMEMODE_TIME_ATTACK;
    gTotalLapCount = 3; /* game.c's initial value: game.c is not in the closure, so its stand-in is 0 */
    gNumPlayers = 1;
    gCourseIndex = COURSE_MUTE_CITY;
    gPlayerCharacters[0] = CAPTAIN_FALCON; /* Blue Falcon. */
    gPlayerEngine[0] = 0.5f;               /* Engine slider in the middle, the game's default. */
    gPlayerControlPorts[0] = 0;
    gControllersConnected = 1;

    /* The Race_Init steps that set simulation state, in its order. */
    Course_Init();
    if (ref_course_loads != 1) {
        return 1; /* The original loader never asked for the course record. */
    }
    sRaceSeed1 = gRandSeed1;
    sRaceSeed2 = gRandSeed2;
    sRaceMask1 = gRandMask1;
    sRaceMask2 = gRandMask2;
    func_i3_80116C4C();
    if (D_i3_8013DBE8 == NULL) {
        D_i3_8013DBE8 = sCpuLine;
    }
    Racer_Init();
    return 0;
}

void ref_race_restart(void) {
    gRandSeed1 = sRaceSeed1;
    gRandSeed2 = sRaceSeed2;
    gRandMask1 = sRaceMask1;
    gRandMask2 = sRaceMask2;
    func_i3_80116C4C();
    if (D_i3_8013DBE8 == NULL) {
        D_i3_8013DBE8 = sCpuLine;
    }
    Racer_Init();
}

void ref_set_cpu_driver(int on) {
    if (on) {
        gRacers[0].stateFlags |= RACER_STATE_CPU_CONTROLLED;
    } else {
        gRacers[0].stateFlags &= ~RACER_STATE_CPU_CONTROLLED;
    }
}

void ref_tick(unsigned short buttons, signed char stick_x, signed char stick_y) {
    ref_pad_button = buttons;
    ref_pad_stick_x = stick_x;
    ref_pad_stick_y = stick_y;
    Controller_UpdateInputs();
    Racer_Update();
}

void ref_state(RefState* s) {
    const Racer* r = &gRacers[0];

    memset(s, 0, sizeof(*s));
    s->intro = gRaceIntroTimer;
    s->pos[0] = r->segmentPositionInfo.pos.x;
    s->pos[1] = r->segmentPositionInfo.pos.y;
    s->pos[2] = r->segmentPositionInfo.pos.z;
    s->vel[0] = r->velocity.x;
    s->vel[1] = r->velocity.y;
    s->vel[2] = r->velocity.z;
    s->speed = r->speed;
    s->energy = r->energy;
    s->lap_distance = r->lapDistance;
    s->seg_t = r->segmentPositionInfo.segmentTValue;
    s->height = r->heightAboveGround;
    s->segment = r->segmentPositionInfo.courseSegment ? r->segmentPositionInfo.courseSegment->segmentIndex : -1;
    s->lap = r->lap;
    s->laps_completed = r->lapsCompleted;
    s->race_time = r->raceTime;
    s->state_flags = r->stateFlags;
    s->boost_timer = r->boostTimer;
}

void ref_pose(RefPose* p) {
    const Racer* r = &gRacers[0];
    const Vec3f* rows[3] = { &r->modelBasis.x, &r->modelBasis.y, &r->modelBasis.z };
    s32 i;

    p->pos[0] = r->modelPos.x;
    p->pos[1] = r->modelPos.y;
    p->pos[2] = r->modelPos.z;
    for (i = 0; i < 3; i++) {
        p->basis[i][0] = rows[i]->x;
        p->basis[i][1] = rows[i]->y;
        p->basis[i][2] = rows[i]->z;
    }
    p->speed = r->speed;
    p->energy = r->energy;
    p->intro = gRaceIntroTimer;
    p->lap = r->lap;
    p->laps_completed = r->lapsCompleted;
    p->race_time = r->raceTime;
    p->state_flags = r->stateFlags;
}
