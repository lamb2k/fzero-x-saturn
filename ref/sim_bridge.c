/* Simulation-lane bridge (decomp headers). Built only when a sim knob is on.
 * MIT; see ../LICENSE. */
#include "global.h"
#include "fzx_camera.h"
#include "fzx_game.h"
#include "fzx_racer.h"
#include "controller.h"
#include "sim.h"
#if FZX_SIM_CAMERA
#include "ref.h"
#include "fzx_sim_camera.h"
#endif

#if FZX_DEFER_PROJECTION
/* Quick win 1 (ref/sim_overrides.h). One matrix build per read instead of one
 * per camera per tick. The camera state the build reads (eye, at, basis,
 * fov, near, far, fovScale*, frustrumCenter*, id and D_800E5E8C) is not
 * written between the tick and the read, so the result is identical. */
void (Camera_UpdateProjectionViewMtx)(GfxPool* gfxPool, Camera* camera);
extern int ref_camera_active; /* camera_bridge.c: counts unmapped viewport loads */
static GfxPool* deferred_pool[4];
static unsigned int deferred_mask;
unsigned int ref_camera_deferrals, ref_camera_deferred_builds;
#if FZX_SIM_CAMERA
static void fix_export(unsigned int index);
static unsigned int fix_valid, fix_stale;
#endif

void ref_camera_defer_projection(void* pool, void* camera) {
    Camera* c = camera;
    ++ref_camera_deferrals;
    if (c < gCameras || c >= gCameras + 4) {
        (Camera_UpdateProjectionViewMtx)(pool, c); /* untracked camera: build now */
        return;
    }
    deferred_pool[c - gCameras] = pool;
    deferred_mask |= 1u << (unsigned int)(c - gCameras);
}

void ref_camera_flush(unsigned int index) {
    if (index >= 4) return;
#if FZX_SIM_CAMERA
    if ((fix_stale >> index) & 1u) fix_export(index);
#endif
    if ((deferred_mask >> index) & 1u) {
        const int active = ref_camera_active;
        deferred_mask &= ~(1u << index);
        ++ref_camera_deferred_builds;
        ref_camera_active = 1; /* same fault accounting as inside Camera_Update */
        (Camera_UpdateProjectionViewMtx)(deferred_pool[index], &gCameras[index]);
        ref_camera_active = active;
    }
}

void ref_camera_flush_all(void) {
    unsigned int i;
    for (i = 0; i < 4; ++i) ref_camera_flush(i);
#if FZX_SIM_CAMERA
    fix_valid = 0; /* Camera_Init follows: the float state is authoritative again */
#endif
}
#endif

#if FZX_SIM_CAMERA
/* ---- Fixed-point camera glue -------------------------------------------- */
_Static_assert(CAMERA_MODE_RACE_INTRO == FZX_CAM_MODE_RACE_INTRO && CAMERA_MODE_SETUP_RACE == FZX_CAM_MODE_SETUP_RACE &&
               CAMERA_MODE_RACE == FZX_CAM_MODE_RACE && CAMERA_MODE_FALLING_OFF_TRACK == FZX_CAM_MODE_FALLING_OFF_TRACK &&
               CAMERA_MODE_FINISHED_SUCCESS == FZX_CAM_MODE_FINISHED_SUCCESS && CAMERA_MODE_ENDING == FZX_CAM_MODE_ENDING &&
               CAMERA_MODE_TITLE_DEMO == FZX_CAM_MODE_TITLE_DEMO && CAMERA_MODE_COURSE_SELECT == FZX_CAM_MODE_COURSE_SELECT &&
               CAMERA_TYPE_FOLLOW_SMOOTH == FZX_CAM_TYPE_FOLLOW_SMOOTH && CAMERA_TYPE_LOCAL_ANCHOR == FZX_CAM_TYPE_LOCAL_ANCHOR &&
               CAMERA_TYPE_ORBIT_AT == FZX_CAM_TYPE_ORBIT_AT && CAMERA_TYPE_AT_EYE == FZX_CAM_TYPE_AT_EYE &&
               BTN_CRIGHT == 0x0001 && BTN_CUP == 0x0008 && CAMERA_FOV_FLAG_1 == FZX_CAM_FOV_FLAG_1 &&
               CAMERA_FOV_FLAG_2 == FZX_CAM_FOV_FLAG_2, "fixed camera mirrors the source enums");
void (Camera_Update)(void);
void (Camera_Init)(void);
extern s32 sNumCameras, gGameMode, gNumPlayers, gNearestRacer;
extern s8 gGamePaused;
extern bool sAllowCameraLookBack;
extern s32 sMaxCameraLookBackRotate;
extern f32 D_800CDA90, D_800CDA94;
extern const CameraRaceSettingInfo kRegularCameraRaceSettings[], kWidescreenCameraRaceSettings[];
extern CameraSettings sCameraSettings[];
extern CameraScriptManager sCameraScriptMgrs[];
extern Racer gRacers[];
extern GfxPool* gGfxPool;
extern s32 gRandSeed1, gRandSeed2;
extern u32 gRandMask1, gRandMask2;

FzxCam fzx_sim_cam[2];
FzxCamConst fzx_sim_cam_k;
volatile uint32_t fzx_sim_camera_hash = 0x811C9DC5u, fzx_sim_camera_fixed_ticks, fzx_sim_camera_fallbacks;
static int sim_arm;
static unsigned int chaos_state;
static unsigned int k_ready;
static unsigned int last_fallback; /* cameras whose last tick ran the source (harness records) */

/* Bit-level float <-> fixed (no soft-float routine; fzx_sim_standin.c). */
static int32_t fx(const f32* f, unsigned int frac) {
    uint32_t bits;
    memcpy(&bits, f, 4);
    return fzx_f32_to_fix(bits, frac);
}
static void fl(f32* f, int32_t v, unsigned int frac) {
    const uint32_t bits = fzx_fix_to_f32(v, frac);
    memcpy(f, &bits, 4);
}
static void fx3(int32_t out[3], const Vec3f* v, unsigned int frac) {
    out[0] = fx(&v->x, frac); out[1] = fx(&v->y, frac); out[2] = fx(&v->z, frac);
}
static void fl3(Vec3f* v, const int32_t in[3], unsigned int frac) {
    fl(&v->x, in[0], frac); fl(&v->y, in[1], frac); fl(&v->z, in[2], frac);
}
static void fxb(sh2z_basis30_t* b, const Mtx3F* m) {
    fx3(b->x, &m->x, 30); fx3(b->y, &m->y, 30); fx3(b->z, &m->z, 30);
}

static void fix_constants(void) {
    const CameraRaceSettingInfo* r = gNumPlayers == 2 ? &kWidescreenCameraRaceSettings[CAMERA_RACE_SETTING_REGULAR]
                                                      : &kRegularCameraRaceSettings[CAMERA_RACE_SETTING_REGULAR];
    FzxCamConst* k = &fzx_sim_cam_k;
    k->regular_distance = fx(&r->distance, 12);
    k->regular_pitch = fx(&r->pitch, 16);
    k->fov_speed_min = fx(&D_800CDA90, 12);
    k->fov_speed_range = fx(&D_800CDA94, 12) - k->fov_speed_min;
    k->look_back_max = sMaxCameraLookBackRotate;
    k->look_back_step = sMaxCameraLookBackRotate > 0 ? (180 * 65536) / sMaxCameraLookBackRotate : 0;
    k->frustum_center_y = (gNumPlayers == 2 ? -15 : 77) * 65536;
    k_ready = 1;
}

/* Float <-> fixed field maps (table-driven: one loop instead of ~150 inline
 * conversions keeps the SH-2 image inside its 16 KiB stack margin). Each row:
 * float offset in Camera (CAM) or CameraSettings (SET), FzxCam offset, binary
 * point, number of consecutive words, and EXP when the fixed tick writes the
 * field (exported to the float mirror at flush; every row is imported). */
typedef struct {
    uint16_t f, x;
    uint8_t frac, n, flags;
} FixMap;
enum { CAM = 0, SET = 1, EXP = 2 };
#define MC(fl, fi, q, n, e) { offsetof(Camera, fl), offsetof(FzxCam, fi), q, n, CAM | (e) }
#define MS(fl, fi, q, n, e) { offsetof(CameraSettings, fl), offsetof(FzxCam, fi), q, n, SET | (e) }
static const FixMap map_common[] = {
    MC(targetFov, target_fov, 16, 1, EXP), MC(focusPos, focus, 12, 3, EXP), MC(racerBasis.y, racer_up, 30, 3, EXP),
    MC(eye, eye, 12, 3, EXP), MC(at, at, 12, 3, EXP), MC(basis, basis, 30, 9, EXP),
    MC(xzNormalizedX, xz_x, 30, 1, EXP), MC(xzNormalizedZ, xz_z, 30, 1, EXP), MC(fov, fov, 16, 1, EXP),
    MC(frustrumCenterX, fcx, 16, 1, EXP), MC(frustrumCenterY, fcy, 16, 1, EXP),
    MS(parameters.at, p_at, 12, 3, EXP), MS(parameters.eye, p_eye, 12, 3, EXP), MS(parameters.up, up, 30, 3, EXP),
    MS(parameters.targetUp, target_up, 30, 3, EXP), MS(parameters.upLerpFactor, up_lerp, 30, 1, 0),
    MS(parameters.fov, p_fov, 16, 1, EXP), MS(parameters.targetFov, p_target_fov, 16, 1, EXP),
    MS(parameters.fovLerpFactor, fov_lerp, 30, 1, 0),
    MS(parameters.frustrumCenterX, p_fcx, 16, 1, EXP), MS(parameters.frustrumCenterY, p_fcy, 16, 1, EXP),
    MS(parameters.targetFrustrumCenterX, p_target_fcx, 16, 1, EXP),
    MS(parameters.targetFrustrumCenterY, p_target_fcy, 16, 1, EXP),
    MS(parameters.frustrumCenterXLerpFactor, fcx_lerp, 30, 1, 0),
    MS(parameters.frustrumCenterYLerpFactor, fcy_lerp, 30, 1, 0),
};
static const FixMap map_follow[] = {
    MS(followSmooth.basis, fs_basis, 30, 9, EXP), MS(followSmooth.anchorPoint, fs_anchor, 12, 3, EXP),
    MS(followSmooth.distance, fs_distance, 12, 1, EXP), MS(followSmooth.targetDistance, fs_target_distance, 12, 1, 0),
    MS(followSmooth.distanceLerpFactor, fs_distance_lerp, 30, 1, 0), MS(followSmooth.pitch, fs_pitch, 16, 1, EXP),
    MS(followSmooth.targetPitch, fs_target_pitch, 16, 1, 0), MS(followSmooth.pitchLerpFactor, fs_pitch_lerp, 30, 1, 0),
    MS(followSmooth.yaw, fs_yaw, 16, 1, EXP), MS(followSmooth.targetYaw, fs_target_yaw, 16, 1, EXP),
    MS(followSmooth.yawLerpFactor, fs_yaw_lerp, 30, 1, 0),
};
static const FixMap map_orbit[] = {
    MS(orbit.anchorPos, ob_anchor, 12, 3, EXP), MS(orbit.basis, ob_basis, 30, 9, EXP),
    MS(orbit.distance, ob_distance, 12, 1, EXP), MS(orbit.targetDistance, ob_target_distance, 12, 1, EXP),
    MS(orbit.distanceLerpFactor, ob_distance_lerp, 30, 1, 0), MS(orbit.pitch, ob_pitch, 16, 1, EXP),
    MS(orbit.targetPitch, ob_target_pitch, 16, 1, 0), MS(orbit.pitchLerpFactor, ob_pitch_lerp, 30, 1, 0),
    MS(orbit.yaw, ob_yaw, 16, 1, EXP), MS(orbit.targetYaw, ob_target_yaw, 16, 1, EXP),
    MS(orbit.yawLerpFactor, ob_yaw_lerp, 30, 1, 0),
};
static const FixMap map_at_eye[] = {
    MS(atEye.targetAt, ae_target_at, 12, 3, EXP), MS(atEye.targetEye, ae_target_eye, 12, 3, 0),
    MS(atEye.atLerpFactor, ae_at_lerp, 30, 1, 0), MS(atEye.eyeLerpFactor, ae_eye_lerp, 30, 1, 0),
};
static const FixMap map_local[] = {
    MS(localAnchor.anchorPoint, la_anchor, 12, 3, 0), MS(localAnchor.basis, la_basis, 30, 9, 0),
    MS(localAnchor.localAtCoordinates, la_at, 12, 3, EXP), MS(localAnchor.targetLocalAt, la_target_at, 12, 3, 0),
    MS(localAnchor.localEyeCoordinates, la_eye, 12, 3, EXP), MS(localAnchor.targetLocalEye, la_target_eye, 12, 3, 0),
    MS(localAnchor.localAtLerpFactor, la_at_lerp, 30, 1, 0), MS(localAnchor.localEyeLerpFactor, la_eye_lerp, 30, 1, 0),
};
#undef MC
#undef MS
_Static_assert(sizeof(Vec3f) == 12 && sizeof(Mtx3F) == 36 && sizeof(sh2z_basis30_t) == 36, "packed float/fixed rows");

/* to_fix: float -> fixed (import) for every row; else fixed -> float for EXP rows. */
static void fix_map(unsigned int i, const FixMap* m, unsigned int rows, int to_fix) {
    unsigned char* const base[2] = { (unsigned char*)&gCameras[i], (unsigned char*)&sCameraSettings[i] };
    unsigned char* const fixed = (unsigned char*)&fzx_sim_cam[i];
    for (; rows--; ++m) {
        unsigned char* f = base[m->flags & SET] + m->f;
        int32_t* x = (int32_t*)(fixed + m->x);
        unsigned int k;
        if (!to_fix && !(m->flags & EXP)) continue;
        for (k = 0; k < m->n; ++k, f += 4) {
            uint32_t bits;
            if (to_fix) {
                memcpy(&bits, f, 4);
                x[k] = fzx_f32_to_fix(bits, m->frac);
            } else {
                bits = fzx_fix_to_f32(x[k], m->frac);
                memcpy(f, &bits, 4);
            }
        }
    }
}
#define ROWS(a) (sizeof(a) / sizeof((a)[0]))
static void fix_map_all(unsigned int i, int to_fix) {
    fix_map(i, map_common, ROWS(map_common), to_fix);
    switch (sCameraSettings[i].type) {
        case CAMERA_TYPE_FOLLOW_SMOOTH: fix_map(i, map_follow, ROWS(map_follow), to_fix); break;
        case CAMERA_TYPE_ORBIT_AT:
        case CAMERA_TYPE_ORBIT_EYE: fix_map(i, map_orbit, ROWS(map_orbit), to_fix); break;
        case CAMERA_TYPE_AT_EYE: fix_map(i, map_at_eye, ROWS(map_at_eye), to_fix); break;
        case CAMERA_TYPE_LOCAL_ANCHOR: fix_map(i, map_local, ROWS(map_local), to_fix); break;
        default: break;
    }
}

/* Float source state -> fixed (every field the fixed tick reads). */
static void fix_import(unsigned int i) {
    const Camera* s = &gCameras[i];
    FzxCam* c = &fzx_sim_cam[i];
    c->mode = s->mode; c->race_setting = s->raceSetting; c->look_back = s->lookBackRotate;
    c->fov_flags = s->fovFlags; c->state = s->state; c->timer = s->timer;
    c->type = sCameraSettings[i].type;
    fix_map_all(i, 1);
    c->pitch_index = c->yaw_index = -99999;
    c->pan_q16 = -1;
}

/* Integer fields and camera->basis.x (read by the next Racer_Update), every tick. */
static void fix_export_tick(unsigned int i) {
    Camera* s = &gCameras[i];
    const FzxCam* c = &fzx_sim_cam[i];
    s->mode = c->mode; s->raceSetting = c->race_setting; s->lookBackRotate = c->look_back;
    s->fovFlags = c->fov_flags; s->state = c->state; s->timer = c->timer;
    fl3(&s->basis.x, c->basis.x, 30);
}

/* Fixed -> float mirror of every field the fixed tick writes; at the first read. */
static void fix_export(unsigned int i) {
    fix_stale &= ~(1u << i);
    fix_export_tick(i);
    fix_map_all(i, 0);
}

static void hash_word(uint32_t w) {
    fzx_sim_camera_hash = (fzx_sim_camera_hash ^ w) * 16777619u;
}

static void fix_tick(unsigned int i) {
    Camera* s = &gCameras[i];
    FzxCam* c = &fzx_sim_cam[i];
    const Racer* r = &gRacers[s->id];
    /* Only RACE reads the pad (Camera_UpdateRace); other cameras may have no port. */
    static const Controller idle;
    const Controller* pad = s->mode == CAMERA_MODE_RACE ? &gControllers[gPlayerControlPorts[s->id]] : &idle;
    FzxCamInput in;
    uint32_t plan;
    const unsigned int bit = 1u << i;
    if (!(fix_valid & bit)) {
        fix_import(i);
        fix_valid |= bit;
        fix_stale &= ~bit;
    }
    plan = fzx_cam_plan(c, (uint32_t)r->stateFlags, pad->buttonPressed, gGamePaused != 0,
                        (r->stateFlags & RACER_STATE_FALLING_OFF_TRACK) != 0, (r->stateFlags & RACER_STATE_FINISHED) != 0,
                        (r->stateFlags & RACER_STATE_RETIRED) != 0);
    if (plan == FZX_CAM_FALLBACK) {
        /* Mode transitions and finishing/script modes: the source tick on the mirror. */
        if (fix_stale & bit) fix_export(i);
        Camera_UpdateMode(s, &sCameraSettings[i], &sCameraScriptMgrs[i]);
        fix_import(i);
        ++fzx_sim_camera_fallbacks;
        last_fallback |= bit;
        hash_word(0xFA11BACCu ^ (uint32_t)s->mode);
        return;
    }
    if (plan & (FZX_CAM_NEED_FOLLOW | FZX_CAM_NEED_INTRO)) fx3(in.focus_pos, &r->focusPos, 12);
    if (plan & FZX_CAM_NEED_FOLLOW) {
        fx3(in.focus_vel, &r->focusVelocity, 12);
        fx3(in.gravity_up, &r->gravityUp, 30);
        fx3(in.true_basis.y, &r->trueBasis.y, 30);
    }
    if (plan & FZX_CAM_NEED_INTRO) fxb(&in.true_basis, &r->trueBasis);
    if (plan & FZX_CAM_NEED_SEG_POS) fx3(in.seg_pos, &r->segmentPositionInfo.pos, 12);
    if (plan & FZX_CAM_NEED_SPEED) { in.unk5c_x = fx(&r->unk_5C.x, 12); in.unk5c_z = fx(&r->unk_5C.z, 12); }
    in.state_flags = (uint32_t)r->stateFlags;
    in.button_pressed = pad->buttonPressed;
    in.button_current = pad->buttonCurrent;
    in.retired = (r->stateFlags & RACER_STATE_RETIRED) != 0;
    in.allow_look_back = sAllowCameraLookBack != 0;
    in.engine_pan = gGameMode == GAMEMODE_GP_RACE || gGameMode == GAMEMODE_PRACTICE ||
                    gGameMode == GAMEMODE_TIME_ATTACK || gGameMode == GAMEMODE_DEATH_RACE;
    if (in.engine_pan) fx3(in.nearest_pos, &gRacers[gNearestRacer].segmentPositionInfo.pos, 12);
    fzx_cam_tick(c, &fzx_sim_cam_k, &in);
    last_fallback &= ~bit;
    fix_export_tick(i);
    fix_stale |= bit;
    deferred_pool[i] = gGfxPool;      /* Camera_UpdateProjectionViewMtx(gGfxPool, camera), deferred */
    deferred_mask |= bit;
    ++ref_camera_deferrals;
    if (c->pan_q16 >= 0) Audio_SetEnemyEnginePan((u8)(c->pan_q16 >> 16)); /* (u8) of the float: truncation */
    ++fzx_sim_camera_fixed_ticks;
    hash_word((uint32_t)i); hash_word((uint32_t)c->mode); hash_word((uint32_t)c->timer);
    for (unsigned int j = 0; j < 3; ++j) { hash_word((uint32_t)c->eye[j]); hash_word((uint32_t)c->at[j]); }
    for (unsigned int j = 0; j < 3; ++j) {
        hash_word((uint32_t)c->basis.x[j]); hash_word((uint32_t)c->basis.y[j]); hash_word((uint32_t)c->basis.z[j]);
    }
    hash_word((uint32_t)c->fov); hash_word((uint32_t)c->fcy); hash_word((uint32_t)c->pan_q16);
}

#ifndef __sh__ /* the chaos arm and the snapshot are HOST harness only */
/* Chaos floor: +-1 ulp on every float field the camera tick writes. */
static void nudge(f32* f) {
    uint32_t bits;
    memcpy(&bits, f, 4);
    chaos_state = chaos_state * 1103515245u + 12345u;
    if ((bits & 0x7FFFFFFFu) == 0 || (bits & 0x7F800000u) == 0x7F800000u) return;
    bits += (chaos_state >> 16) & 1u ? 1u : 0xFFFFFFFFu;
    memcpy(f, &bits, 4);
}
static void nudge3(Vec3f* v) { nudge(&v->x); nudge(&v->y); nudge(&v->z); }
static void chaos(unsigned int i) {
    Camera* s = &gCameras[i];
    CameraSettings* t = &sCameraSettings[i];
    nudge3(&s->eye); nudge3(&s->at); nudge3(&s->basis.x); nudge3(&s->basis.y); nudge3(&s->basis.z);
    nudge3(&s->focusPos); nudge3(&s->racerBasis.y); nudge(&s->fov); nudge(&s->frustrumCenterY);
    nudge3(&t->parameters.at); nudge3(&t->parameters.eye); nudge3(&t->parameters.up); nudge(&t->parameters.fov);
    if (t->type == CAMERA_TYPE_FOLLOW_SMOOTH) {
        nudge3(&t->followSmooth.anchorPoint); nudge(&t->followSmooth.distance); nudge(&t->followSmooth.pitch);
        nudge3(&t->followSmooth.basis.x); nudge3(&t->followSmooth.basis.y); nudge3(&t->followSmooth.basis.z);
    } else if (t->type == CAMERA_TYPE_ORBIT_AT || t->type == CAMERA_TYPE_ORBIT_EYE) {
        nudge(&t->orbit.distance); nudge(&t->orbit.pitch); nudge(&t->orbit.yaw);
    }
}

#endif

void ref_sim_set_arm(int arm, unsigned int seed) {
    sim_arm = arm;
    chaos_state = seed;
    fix_valid = fix_stale = 0;
}

void ref_sim_set_drop_bits(unsigned int bits) { fzx_cam_drop_bits = bits; }

void ref_sim_camera_init(void) {
    (Camera_Init)();
    fix_valid = fix_stale = last_fallback = 0; /* the float state is authoritative again */
}

void ref_sim_camera_update(void) {
    unsigned int i;
    /* Race ticks only: menu cameras (machine select, course preview) can tick
     * while gGameMode already reads Time Attack. */
    const s32 mode = gCameras[0].mode;
    if (sim_arm == 1 && gGameMode == GAMEMODE_TIME_ATTACK && gNumPlayers == 1 && sNumCameras <= 2 &&
        mode >= CAMERA_MODE_RACE_INTRO && mode <= CAMERA_MODE_FINISHED_LOSER) {
        if (!k_ready) fix_constants();
        for (i = 0; i < (unsigned int)sNumCameras; ++i) fix_tick(i);
        return;
    }
    if (fix_valid) {               /* leaving the fixed path: the float state takes over */
        for (i = 0; i < 2; ++i) if (fix_stale & (1u << i)) fix_export(i);
        fix_valid = 0;
    }
    (Camera_Update)();
#ifndef __sh__
    if (sim_arm == 2) for (i = 0; i < (unsigned int)sNumCameras && i < 2; ++i) chaos(i);
#endif
}

#ifndef __sh__

static float angle_float(f32 deg) { return (float)Math_Round(DEG_TO_FZXANG2(deg)); }

void ref_sim_snapshot(RefSimTick* out) {
    unsigned int i;
    static const RefSimTick zero;
    *out = zero;
    out->rng[0] = gRandSeed1; out->rng[1] = (int32_t)gRandMask1;
    out->rng[2] = gRandSeed2; out->rng[3] = (int32_t)gRandMask2;
    out->camera_count = sNumCameras;
    out->hash = fzx_sim_camera_hash; out->fixed_ticks = fzx_sim_camera_fixed_ticks;
    out->fallbacks = fzx_sim_camera_fallbacks;
    for (i = 0; i < 2 && i < (unsigned int)sNumCameras; ++i) {
        const Camera* s = &gCameras[i];
        const CameraSettings* t = &sCameraSettings[i];
        RefSimCamera* o = &out->cam[i];
        ref_camera_flush(i);
        o->mode = s->mode; o->setting = s->raceSetting; o->timer = s->timer; o->look_back = s->lookBackRotate;
        o->fov_flags = s->fovFlags; o->state = s->state; o->type = t->type;
        memcpy(o->eye, &s->eye, 12); memcpy(o->at, &s->at, 12); memcpy(o->basis, &s->basis, 36);
        o->fov = s->fov; o->fcx = s->frustrumCenterX; o->fcy = s->frustrumCenterY;
        o->xz[0] = s->xzNormalizedX; o->xz[1] = s->xzNormalizedZ;
        memcpy(o->p_eye, &t->parameters.eye, 12); memcpy(o->p_at, &t->parameters.at, 12);
        memcpy(o->up, &t->parameters.up, 12);
        o->near_plane = s->near; o->far_plane = s->far; o->fov_scale_x = s->fovScaleX; o->fov_scale_y = s->fovScaleY;
        o->pitch_index = o->yaw_index = -99999;
        if (t->type == CAMERA_TYPE_FOLLOW_SMOOTH) {
            o->s_distance = t->followSmooth.distance; o->s_pitch = t->followSmooth.pitch;
            o->s_yaw = t->followSmooth.yaw; o->s_target_yaw = t->followSmooth.targetYaw;
            memcpy(o->s_anchor, &t->followSmooth.anchorPoint, 12);
            o->pitch_index = (int32_t)angle_float(t->followSmooth.pitch);
            o->yaw_index = (int32_t)angle_float(t->followSmooth.targetYaw);
        } else if (t->type == CAMERA_TYPE_ORBIT_AT || t->type == CAMERA_TYPE_ORBIT_EYE) {
            o->s_distance = t->orbit.distance; o->s_pitch = t->orbit.pitch;
            o->s_yaw = t->orbit.yaw; o->s_target_yaw = t->orbit.targetYaw;
            memcpy(o->s_anchor, &t->orbit.anchorPos, 12);
            o->pitch_index = (int32_t)angle_float(t->orbit.pitch);
            o->yaw_index = (int32_t)angle_float(t->orbit.yaw);
        }
        if ((fix_valid >> i) & 1u) {
            const FzxCam* c = &fzx_sim_cam[i];
            o->fixed = 1;
            if (c->pitch_index != -99999) { o->pitch_index = c->pitch_index; o->yaw_index = c->yaw_index; }
            memcpy(&o->raw[0], c->eye, 12); memcpy(&o->raw[3], c->at, 12);
            memcpy(&o->raw[6], &c->basis, 36);
            o->raw[15] = c->fov; o->raw[16] = c->fcx; o->raw[17] = c->fcy;
            o->raw[18] = c->type == CAMERA_TYPE_FOLLOW_SMOOTH ? c->fs_distance : c->ob_distance;
            o->raw[19] = c->type == CAMERA_TYPE_FOLLOW_SMOOTH ? c->fs_pitch : c->ob_pitch;
            o->raw[20] = c->type == CAMERA_TYPE_FOLLOW_SMOOTH ? c->fs_yaw : c->ob_yaw;
            o->raw[21] = c->type == CAMERA_TYPE_FOLLOW_SMOOTH ? c->fs_target_yaw : c->ob_target_yaw;
            o->raw[22] = c->pan_q16;
            o->raw[23] = (last_fallback >> i) & 1u ? 2 : 1; /* 1 fixed tick, 2 source fallback tick */
        }
    }
}
#endif
#endif
