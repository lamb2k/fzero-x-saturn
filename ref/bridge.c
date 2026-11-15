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
extern s32 gNumPlayers;
extern s32 gCourseIndex;
extern s16 gPlayerCharacters[];
extern f32 gPlayerEngine[];
extern s32 gPlayerControlPorts[];
extern s32 gControllersConnected;
extern s32 gRaceIntroTimer;
extern OSPiHandle* gCartRomHandle;

extern GfxPool* gGfxPool;

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

    /* One human racer in Time Attack, as the menus leave it. */
    func_8008D33C(); /* Default machines. */
    gGameMode = GAMEMODE_TIME_ATTACK;
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
    func_i3_80116C4C();
    Racer_Init();
    return 0;
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
