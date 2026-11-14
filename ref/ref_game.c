/* Game side of the float reference (card G1.2). MIT; see ../LICENSE.
 * Sees only decomp headers. ref_main.c (libc side) drives it through ref.h. */
#include "global.h"
#include "controller.h"
#include "fzx_game.h"
#include "fzx_racer.h"
#include "fzx_course.h"
#include "fzx_camera.h"
#include "sys.h"

#include "ref.h"

extern s32 gNumPlayers, gTotalRacers, gGameMode, gCourseIndex, gTotalLapCount, gCurrentGhostType;
extern s8 gTitleDemoState;
extern CourseInfo* gCurrentCourseInfo;
extern void func_8008D33C(void);
extern void Math_SinTableInit(void);
extern void Course_Init(void);
extern void Racer_Init(void);
extern void Camera_Init(void);
extern void Racer_Update(void);
extern void Camera_Update(void);
extern void Course_Update(void);
extern void func_80074844(void);

static u16 sPrevious;

/* Racer_Update and the camera write RSP matrices into the frame's graphics pool. That is
 * drawing output; the reference gives it real storage and never reads it. */
static GfxPool sRefGfxPool;
GfxPool* gGfxPool = &sRefGfxPool;

/* func_i3_80115E74 (src/overlays/ovl_i3/cpu.c:454) lays out the starting grid. With one
 * racer both of its branches reduce to this line (cpu.c:658 and :692); the loops over
 * racers 1.. do nothing. Called from Racer_SetupStartPositions. */
void func_i3_80115E74(void) {
    gRacers[0].position = gTotalRacers;
}

/* ROM DMA. Course_Load reads one CourseDataDisk from the course_data segment through
 * Dma_LoadAssetsAsync -> Dma_RomCopyAsync -> osEPiStartDma. Reads inside that segment
 * are served from the harness's record (already in host order); any other ROM read is
 * asset loading for drawing (OUT), counted and left untouched. */
extern u8 course_data_ROM_START[];
static u8 sPiHandle[0x100];
OSPiHandle* gCartRomHandle = (OSPiHandle*) sPiHandle;

s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* mb, s32 direction) {
    unsigned int offset = mb->devAddr - (unsigned int) (uintptr_t) course_data_ROM_START;
    (void) handle;
    (void) direction;
    if (offset < 64u * REF_COURSE_RECORD_SIZE) {
        unsigned int in_record = offset % REF_COURSE_RECORD_SIZE;
        if (in_record + mb->size <= REF_COURSE_RECORD_SIZE) {
            bcopy(ref_course_record + in_record, mb->dramAddr, mb->size);
            ++ref_dma_course_loads;
            return 0;
        }
    }
    ++ref_dma_other_loads;
    return 0;
}

/* Boot and menu state for a one-player time attack on course slot 0, set as the menus
 * would, then Race_Init minus OUT work (effects, backdrop, HUD, minimap, menus, CPU). */
void ref_game_init(int machine) {
    /* Boot: sys_gfx.c:682 fills the sine table; racer.c copies the default machines.
     * The RNG keeps math.c's initial seeds (gRandSeed1/2). */
    Math_SinTableInit();
    func_8008D33C();
    gGameMode = GAMEMODE_TIME_ATTACK;
    gNumPlayers = 1;
    gTotalRacers = 1;
    gCourseIndex = COURSE_MUTE_CITY;
    gTotalLapCount = 3;
    gTitleDemoState = TITLE_DEMO_INACTIVE;
    gCurrentGhostType = 0;
    gPlayerControlPorts[0] = 0;
    gRacers[0].character = machine;
    Course_Init();
    Racer_Init();
    Camera_Init();
}

/* One 60 Hz tick: the input latch as Controller_UpdateInputs leaves it, then Race_Update's
 * order minus OUT steps (menus, effects, background). */
void ref_game_tick(unsigned short buttons, signed char stick_x, signed char stick_y) {
    Controller* pad = &gControllers[0];
    pad->buttonCurrent = buttons;
    pad->buttonPressed = buttons & (u16) ~sPrevious;
    pad->buttonReleased = sPrevious & (u16) ~buttons;
    pad->stickX = stick_x;
    pad->stickY = stick_y;
    sPrevious = buttons;
    Racer_Update();
    Camera_Update();
    Course_Update();
    func_80074844();
}

void ref_game_snapshot(RefSnapshot* out) {
    Racer copy = gRacers[0];
    const CourseSegment* seg = copy.segmentPositionInfo.courseSegment;
    out->segment = seg ? (int) (seg - gCurrentCourseInfo->courseSegments) : -1;
    /* Pointer members are replaced by stable values before hashing. */
    copy.segmentPositionInfo.courseSegment = NULL;
    copy.unk_28C = copy.racerAhead = copy.racerBehind = NULL;
    out->racer_bytes = sizeof(copy);
    bcopy(&copy, out->racer, sizeof(copy));
    out->state_flags = (unsigned int) copy.stateFlags;
    out->t = copy.segmentPositionInfo.segmentTValue;
    out->pos[0] = copy.segmentPositionInfo.pos.x;
    out->pos[1] = copy.segmentPositionInfo.pos.y;
    out->pos[2] = copy.segmentPositionInfo.pos.z;
    out->vel[0] = copy.velocity.x;
    out->vel[1] = copy.velocity.y;
    out->vel[2] = copy.velocity.z;
    out->speed = copy.speed;
    out->energy = copy.energy;
    out->lap = copy.lap;
    out->laps_completed = copy.lapsCompleted;
    out->race_distance = copy.raceDistancePosition;
}

/* Baked course values the tick reads: per-segment tension and length (OFFLINE outputs). */
int ref_game_course_values(float* tension, float* length, int max) {
    int n = 0;
    CourseSegment* seg = gCurrentCourseInfo->courseSegments;
    do {
        if (n < max) {
            tension[n] = seg->tension;
            length[n] = seg->length;
        }
        ++n;
        seg = seg->next;
    } while (seg != gCurrentCourseInfo->courseSegments);
    return n;
}
