/* Reference harness bridge: sets up and steps the original race code.
 * MIT; see ../LICENSE. Compiled with the decomp's headers only. */
#include "global.h"
#include "fzx_course.h"
#include "fzx_racer.h"
#include "fzx_game.h"
#include "controller.h"
#include "ref.h"
#if FZX_SOURCE_MACHINE
#include "fzx_camera.h"
#include "fzx_machine.h"
#endif

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

#if !FZX_SOURCE_MACHINE_MENU || !FZX_RACE_GFX_EXTERNAL
static GfxPool sGfxPool;    /* HOST/default storage; native menus use guarded cartridge storage. */
#endif
#if FZX_SOURCE_MACHINE_MENU
#if FZX_RACE_GFX_EXTERNAL
static GfxPool *sRaceGfxPool;
#else
static GfxPool *sRaceGfxPool = &sGfxPool;
#endif
unsigned int ref_race_gfx_bytes(void) { return sizeof(GfxPool); }
int ref_race_gfx_bind(void *memory,unsigned int bytes) {
    if (!memory || ((unsigned long)memory & 31u) || bytes < sizeof(GfxPool)) return 1;
    sRaceGfxPool = (GfxPool *)memory;
    return 0;
}
#endif
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

#if FZX_SOURCE_MACHINE_MENU
static float sSelectedEngine = 0.5f;
static unsigned int sSelectedSkin;
extern s16 gPlayerMachineSkins[];
int ref_race_set_selection(unsigned int character,unsigned int skin,float engine) {
    if (character != CAPTAIN_FALCON || skin >= 4 || !(engine >= 0.0f && engine <= 1.0f)) {
        return 1;
    }
    sSelectedEngine = engine;
    sSelectedSkin = skin;
    return 0;
}
#endif

int ref_race_init(void) {
    gCartRomHandle = &sCartRom;
#if FZX_SOURCE_MACHINE_MENU
    if (!sRaceGfxPool) return 1;
    gGfxPool = sRaceGfxPool;
#else
    gGfxPool = &sGfxPool;
#endif

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
#if FZX_SOURCE_MACHINE_MENU
    gPlayerEngine[0] = sSelectedEngine;
    gPlayerMachineSkins[0] = (s16)sSelectedSkin;
    /* Original settings entry copies character/skin into the racers; leaving
     * settings converts the engine slider into Racer.unk_1A8. Racer_Init uses
     * those racer fields directly, so setting only the player arrays is not
     * a complete handoff. Keep the legacy flag-off race path unchanged. */
    func_8008969C();
    func_80089724();
#else
    gPlayerEngine[0] = 0.5f;               /* Engine slider in the middle, the game's default. */
#endif
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
#if FZX_SOURCE_CAMERA
    ref_camera_init(1);
#endif
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
#if FZX_SOURCE_CAMERA
    ref_camera_init(0);
#endif
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
#if FZX_SOURCE_CAMERA
    ref_camera_tick();
#endif
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

void ref_hud(RefHud* out) {
    const Racer* r = &gRacers[0];
    out->lap = r->lap;
    out->race_time = r->raceTime;
    for (s32 i = 0; i < 3; i++) out->lap_times[i] = r->lapTimes[i];
}

#if FZX_SOURCE_MACHINE
/* Racer_Draw's stock presentation expressions only. Never call Racer_Draw or
 * write its cached matrices/LOD: simulation also reads machineLod during crashes. */
#if defined(PORT) || defined(EXPANSION_KIT)
#error Source machine snapshot requires stock source conditionals
#endif
extern Vec3s* sPipeFogColors;
extern Vec3s* sTunnelFogColors;
extern f32 D_800CE748, D_800CE74C, D_800CE750;
_Static_assert(sizeof(RefMachine) == 112, "machine snapshot ABI");

static int machine_finite(float value) {
    unsigned int bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
static int machine_float_array(const float* values, unsigned int count) {
    unsigned int i;
    for (i = 0; i < count; ++i) if (!machine_finite(values[i])) return 0;
    return 1;
}
static int machine_fail(RefMachine* out, unsigned int fault) {
    memset(out, 0, sizeof(*out));
    out->faults = fault;
    return 0;
}
#ifndef FZX_REF_MACHINE_FIXED
#define FZX_REF_MACHINE_FIXED 0
#endif
#if FZX_REF_MACHINE_FIXED
/* Fixed-point presentation tail (FZX_REF_MACHINE_FIXED, default 0; owner
 * decision 2026-11-17: ref_machine counts against the vehicle budget). Floats
 * are converted once (directions and matrices Q24, world units Q12); the range
 * checks are exact IEEE bit compares; reflection, the shifted projection, LOD
 * and visibility are integer. Approximate: reflection may differ by one unit
 * and LOD/visibility only at exact boundaries; shifted_distance is Q12-rounded.
 * Out-of-range values fall back to the float path below, which also decides
 * every fault. machine_q and machine_round are stand-ins pending SH2ZAM pose-1
 * (float-to-fixed conversion). */
static int machine_q(float value, int frac, int* out) {
    unsigned int bits, mag;
    int shift;
    memcpy(&bits, &value, sizeof(bits));
    if (!(bits & 0x7f800000u)) { *out = 0; return 1; }
    shift = (int)((bits >> 23) & 0xffu) - 150 + frac;
    if (shift > 7) return 0;
    mag = (bits & 0x7fffffu) | 0x800000u;
    if (shift >= 0) mag <<= shift;
    else if (shift < -25) mag = 0;
    else mag = (mag + (1u << (-shift - 1))) >> -shift;
    if (mag >= (1u << 28)) return 0;
    *out = (bits >> 31) ? -(int)mag : (int)mag;
    return 1;
}
static long long machine_round(long long value, int shift) {
    const long long half = 1LL << (shift - 1);
    return value < 0 ? -((-value + half) >> shift) : (value + half) >> shift;
}
static int machine_range_bits(const float scale[3], const float* forward) {
    unsigned int i, s, f;
    for (i = 0; i < 3; ++i) {
        memcpy(&s, &scale[i], sizeof(s));
        memcpy(&f, &forward[i], sizeof(f));
        /* 0 < scale <= 1.0f and |forward| <= 1.01f (0x3f8147ae), finite inputs. */
        if ((s >> 31) || !s || s > 0x3f800000u || (f & 0x7fffffffu) > 0x3f8147aeu) return 0;
    }
    return 1;
}
static int machine_fixed_tail(RefMachine* result, const float world_light[3], const float* forward,
                              const float* eye, const float (*v)[4], const float (*p)[4],
                              const float (*pv)[4], const float* pos, float ambient) {
    int f[3], w[3], e[3], q[3], vq[3][3], pq[3][4], p00, p11, p20, p21, p22, p32, i, j, lod = 0, visible = 0;
    long long b[3], t[4], c[4], s0, s1;
    short reflection[3];
    unsigned char level;
    for (i = 0; i < 3; ++i) {
        if (!machine_q(forward[i], 24, &f[i]) || !machine_q(world_light[i], 24, &w[i]) ||
            !machine_q(eye[i], 12, &e[i]) || !machine_q(pos[i], 12, &q[i])) return 0;
        for (j = 0; j < 3; ++j) if (!machine_q(v[i][j], 24, &vq[i][j])) return 0;
        for (j = 0; j < 4; ++j) if (!machine_q(pv[i][j], 24, &pq[i][j])) return 0;
    }
    if (!machine_q(p[0][0], 24, &p00) || !machine_q(p[1][1], 24, &p11) || !machine_q(p[2][0], 24, &p20) ||
        !machine_q(p[2][1], 24, &p21) || !machine_q(p[2][2], 24, &p22) || !machine_q(p[3][2], 12, &p32)) return 0;
    for (i = 0; i < 3; ++i) {
        const long long packed = (long long)(w[i] - f[i]) * 16383;
        if (packed < -(32767LL << 24) || packed > (32767LL << 24)) return 0;
        reflection[i] = (short)machine_round(packed, 24);
        b[i] = machine_round((long long)f[i] * 100, 12) - e[i];
    }
    t[3] = -machine_round(b[0] * vq[0][2] + b[1] * vq[1][2] + b[2] * vq[2][2], 24);
    s0 = machine_round(b[0] * vq[0][0] + b[1] * vq[1][0] + b[2] * vq[2][0], 24);
    s1 = machine_round(b[0] * vq[0][1] + b[1] * vq[1][1] + b[2] * vq[2][1], 24);
    if (t[3] < -(1LL << 30) || t[3] > (1LL << 30) || s0 < -(1LL << 30) || s0 > (1LL << 30) ||
        s1 < -(1LL << 30) || s1 > (1LL << 30)) return 0;
    t[2] = p32 - machine_round(p22 * t[3], 24);
    t[0] = machine_round(p00 * s0, 24) - machine_round(p20 * t[3], 24);
    t[1] = machine_round(p11 * s1, 24) - machine_round(p21 * t[3], 24);
    for (i = 0; i < 4; ++i) {
        c[i] = machine_round((long long)pq[0][i] * q[0] + (long long)pq[1][i] * q[1] + (long long)pq[2][i] * q[2], 24) + t[i];
        if (c[i] < -(1LL << 30) || c[i] > (1LL << 30)) return 0;
    }
    if ((result->state_flags & RACER_STATE_ACTIVE) && c[2] >= 0 && c[2] <= 2500LL * 4096) {
        const long long a3 = c[3] < 0 ? -c[3] : c[3];
        if (!a3) return 0;
        /* x = clip0/clip3 and y = clip1/clip3 in [-1, 1], without the division. */
        if ((c[0] < 0 ? -c[0] : c[0]) <= a3 && (c[1] < 0 ? -c[1] : c[1]) <= a3) {
            lod = c[2] < 230 * 4096 ? 1 : c[2] < 290 * 4096 ? 2 : c[2] < 380 * 4096 ? 3 :
                c[2] < 470 * 4096 ? 4 : c[2] < 1500 * 4096 ? 5 : 6;
            visible = !(result->state_flags & RACER_STATE_CRASHED);
        }
    }
    level = (unsigned char)ambient;
    for (i = 0; i < 3; ++i) { result->reflection[i] = reflection[i]; result->ambient[i] = level; }
    result->shifted_distance = (float)(int)c[2] * (1.0f / 4096.0f);
    result->source_lod = lod;
    result->normal_body_visible = visible;
    return 1;
}
#endif
int ref_machine(RefMachine* out, const unsigned char outside_fog[3]) {
    const Racer* r = &gRacers[0];
    const Camera* camera = &gCameras[0];
    const CourseSegment* segment = r->segmentPositionInfo.courseSegment;
    RefMachine result;
    float world_light[3], b[3], translation[4], clip[4], ambient = 100.0f;
    const float* pos = &r->segmentPositionInfo.pos.x;
    const float* forward = &camera->basis.x.x;
    const float* eye = &camera->eye.x;
    const float (*v)[4] = camera->viewMtx.m;
    const float (*p)[4] = camera->projectionMtx.m;
    const float (*pv)[4] = camera->projectionViewMtx.m;
    unsigned int i, inside, type;
    int colors[3];
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!outside_fog || !segment) return machine_fail(out, REF_MACHINE_STATE);
    if (gGameMode != GAMEMODE_TIME_ATTACK || gNumPlayers != 1 ||
        r->character != CAPTAIN_FALCON || r->customType != CUSTOM_MACHINE_DEFAULT)
        return machine_fail(out, REF_MACHINE_UNSUPPORTED);
    memset(&result, 0, sizeof(result));
    memcpy(result.model_pos, &r->modelPos, sizeof(result.model_pos));
    memcpy(result.model_basis, &r->modelBasis, sizeof(result.model_basis));
    result.model_scale[0] = D_800CE748;
    result.model_scale[1] = D_800CE74C;
    result.model_scale[2] = D_800CE750;
    if (!machine_float_array(result.model_pos, 3) ||
        !machine_float_array(&result.model_basis[0][0], 9) ||
        !machine_float_array(result.model_scale, 3) ||
        !machine_float_array(pos, 3) || !machine_float_array(forward, 3) ||
        !machine_float_array(eye, 3) || !machine_float_array(&v[0][0], 16) ||
        !machine_float_array(&p[0][0], 16) || !machine_float_array(&pv[0][0], 16))
        return machine_fail(out, REF_MACHINE_NONFINITE);
#if FZX_REF_MACHINE_FIXED
    if (!machine_range_bits(result.model_scale, forward)) return machine_fail(out, REF_MACHINE_STATE);
#else
    for (i = 0; i < 3; ++i)
        if (result.model_scale[i] <= 0.0f || result.model_scale[i] > 1.0f ||
            forward[i] < -1.01f || forward[i] > 1.01f)
            return machine_fail(out, REF_MACHINE_STATE);
#endif
    result.state_flags = (unsigned int)r->stateFlags;
    result.character = r->character;
    result.custom_type = r->customType;
    colors[0] = r->bodyR; colors[1] = r->bodyG; colors[2] = r->bodyB;
    for (i = 0; i < 3; ++i) {
        if (colors[i] < 0 || colors[i] > 255) return machine_fail(out, REF_MACHINE_STATE);
        result.body[i] = (unsigned char)colors[i];
        result.light_rgb[i] = 255;
    }
    inside = ((unsigned int)segment->trackSegmentInfo & TRACK_FLAG_INSIDE) != 0;
    if (inside) {
        float t;
        if (!machine_finite(segment->nextJoinStartTValue) ||
            !machine_finite(segment->previousJoinEndTValue) ||
            !machine_finite(r->segmentPositionInfo.segmentTValue) ||
            !machine_finite(r->segmentPositionInfo.segmentLengthProportion))
            return machine_fail(out, REF_MACHINE_NONFINITE);
        if (segment->nextJoinStartTValue < r->segmentPositionInfo.segmentTValue ||
            r->segmentPositionInfo.segmentTValue < segment->previousJoinEndTValue) {
            if (!machine_finite(segment->joinScale) || segment->joinScale <= 0.0f)
                return machine_fail(out, REF_MACHINE_LIGHT);
            if (segment->nextJoinStartTValue < r->segmentPositionInfo.segmentTValue)
                t = (1.0f - r->segmentPositionInfo.segmentLengthProportion) / segment->joinScale;
            else t = r->segmentPositionInfo.segmentLengthProportion / segment->joinScale;
            if (!machine_finite(t) || t < 0.0f || t > 2.0f)
                return machine_fail(out, REF_MACHINE_LIGHT);
            ambient = (float)(Math_Round(t * -50.0f) + 100);
        } else ambient = 50.0f;
    }
    /* Source RACER_STATE_FLAGS_80000000 is (1 << 31); spell its unsigned
     * value here so this read-only adapter does not inherit signed-shift UB. */
    if (inside && !(result.state_flags & 0x80000000u)) {
        const Vec3s* color;
        type = (unsigned int)segment->trackSegmentInfo & TRACK_TYPE_MASK;
        if (((unsigned int)segment->trackSegmentInfo & TRACK_SHAPE_MASK) == TRACK_SHAPE_PIPE) {
            if (!sPipeFogColors || type >= PIPE_MAX) return machine_fail(out, REF_MACHINE_LIGHT);
            color = &sPipeFogColors[type];
        } else {
            if (!sTunnelFogColors || type >= TUNNEL_MAX) return machine_fail(out, REF_MACHINE_LIGHT);
            color = &sTunnelFogColors[type];
        }
        memcpy(world_light, &r->segmentBasis.y, sizeof(world_light));
        if (!machine_float_array(world_light, 3)) return machine_fail(out, REF_MACHINE_NONFINITE);
        colors[0] = color->x; colors[1] = color->y; colors[2] = color->z;
        for (i = 0; i < 3; ++i) {
            if (world_light[i] < -1.01f || world_light[i] > 1.01f || colors[i] < 0 || colors[i] > 255)
                return machine_fail(out, REF_MACHINE_LIGHT);
            result.light_dir[i] = (signed char)Math_Round(world_light[i] * 120.0f);
            result.fog[i] = (unsigned char)colors[i];
        }
    } else {
        for (i = 0; i < 3; ++i) {
            world_light[i] = 0.57735f;
            result.light_dir[i] = 69;
            result.fog[i] = outside_fog[i];
        }
    }
#if FZX_REF_MACHINE_FIXED
    if (!machine_fixed_tail(&result, world_light, forward, eye, v, p, pv, pos, ambient)) {
#endif
    for (i = 0; i < 3; ++i) {
        float packed = (world_light[i] - forward[i]) * 16383.0f;
        if (!machine_finite(packed) || packed < -32767.0f || packed > 32767.0f)
            return machine_fail(out, REF_MACHINE_LIGHT);
        result.reflection[i] = (short)Math_Round(packed);
        result.ambient[i] = (unsigned char)ambient;
    }
    /* Source shifted projection; preserve operation order and use physics
     * position for LOD, while the renderer uses the model position above. */
    for (i = 0; i < 3; ++i) b[i] = forward[i] * 100.0f - eye[i];
    translation[3] = ((-b[0] * v[0][2]) - b[1] * v[1][2]) - b[2] * v[2][2];
    translation[2] = p[3][2] - p[2][2] * translation[3];
    translation[0] = p[0][0] * ((b[0] * v[0][0] + b[1] * v[1][0]) + b[2] * v[2][0]) - p[2][0] * translation[3];
    translation[1] = p[1][1] * ((b[0] * v[0][1] + b[1] * v[1][1]) + b[2] * v[2][1]) - p[2][1] * translation[3];
    for (i = 0; i < 4; ++i)
        clip[i] = ((pv[0][i] * pos[0] + pv[1][i] * pos[1]) + pv[2][i] * pos[2]) + translation[i];
    if (!machine_float_array(clip, 4)) return machine_fail(out, REF_MACHINE_PROJECTION);
    result.shifted_distance = clip[2];
    if ((result.state_flags & RACER_STATE_ACTIVE) && clip[2] >= 0.0f && clip[2] <= 2500.0f) {
        float inv, x, y;
        if (clip[3] == 0.0f) return machine_fail(out, REF_MACHINE_PROJECTION);
        inv = 1.0f / clip[3]; x = inv * clip[0]; y = inv * clip[1];
        if (!machine_finite(inv) || !machine_finite(x) || !machine_finite(y))
            return machine_fail(out, REF_MACHINE_PROJECTION);
        if (x >= -1.0f && x <= 1.0f && y >= -1.0f && y <= 1.0f) {
            result.source_lod = clip[2] < 230.0f ? 1 : clip[2] < 290.0f ? 2 :
                clip[2] < 380.0f ? 3 : clip[2] < 470.0f ? 4 : clip[2] < 1500.0f ? 5 : 6;
            result.normal_body_visible = !(result.state_flags & RACER_STATE_CRASHED);
        }
    }
#if FZX_REF_MACHINE_FIXED
    }
#endif
    *out = result;
    return 1;
}
#endif
