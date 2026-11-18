/* Simulation-lane interface (plain C types; both the host driver and the
 * decomp-header bridge compile it). MIT; see ../LICENSE. */
#ifndef FZX_SIM_H
#define FZX_SIM_H

#include "fzx_sim_types.h"

#ifndef FZX_DEFER_PROJECTION
#define FZX_DEFER_PROJECTION 0
#endif
#ifndef FZX_SIM_CAMERA
#define FZX_SIM_CAMERA 0
#endif
#if FZX_SIM_CAMERA && !FZX_DEFER_PROJECTION
#error "FZX_SIM_CAMERA builds the float camera matrices only at publication: it needs FZX_DEFER_PROJECTION"
#endif

#if FZX_DEFER_PROJECTION
/* Make camera INDEX's float state and projection/view matrices current: with
 * FZX_SIM_CAMERA, export the fixed camera's state to gCameras/sCameraSettings;
 * then build the deferred matrices. A no-op when both are current. Every
 * reader of a camera's float fields or matrices calls it first:
 * ref_camera_read/ref_camera (via the image's publication), ref_machine, the
 * road plan, sky, skyline and the records matrix bridge.
 * ref_camera_flush_all also runs before a restart (Camera_Init) and hands
 * camera ownership back to the float state. */
void ref_camera_flush(unsigned int index);
void ref_camera_flush_all(void);
/* Calls of the deferred function, and matrix builds actually performed. */
extern unsigned int ref_camera_deferrals, ref_camera_deferred_builds;
#endif

#if FZX_SIM_CAMERA
/* Which camera runs: 0 the source float Camera_Update (REF), 1 the fixed-point
 * camera (FIX), 2 the source camera with a deterministic +-1 ulp nudge of every
 * float camera-state field after each tick (the chaos floor). The image sets 1
 * once at start-up; the HOST harness chooses per run. */
void ref_sim_set_arm(int arm, unsigned int seed);
/* Negative control: drop this many low fraction bits of the fixed state per tick. */
void ref_sim_set_drop_bits(unsigned int bits);
/* Word-wise FNV-1a over the fixed camera state of every fixed tick, fixed
 * ticks and source fallbacks: the host-versus-SH-2 exactness witness. */
extern volatile uint32_t fzx_sim_camera_hash, fzx_sim_camera_fixed_ticks, fzx_sim_camera_fallbacks;

/* One camera as the harness records it after a tick. Floats are the source
 * fields (for FIX: the exported mirror); raw holds the FIX words. */
typedef struct {
    int32_t mode, setting, timer, look_back, fov_flags, state, type, pitch_index, yaw_index, fixed;
    float eye[3], at[3], basis[9], fov, fcx, fcy, xz[2];
    float p_eye[3], p_at[3], up[3];
    float s_distance, s_pitch, s_yaw, s_target_yaw, s_anchor[3];
    float near_plane, far_plane, fov_scale_x, fov_scale_y;
    int32_t raw[24]; /* eye q12[3], at q12[3], basis u30[9], fov q16, fcx q16, fcy q16,
                        distance q12, pitch q16, yaw q16, target_yaw q16, pan q16, valid */
} RefSimCamera;
typedef struct {
    int32_t rng[4];            /* gRandSeed1, gRandMask1, gRandSeed2, gRandMask2 */
    int32_t camera_count;
    uint32_t hash, fixed_ticks, fallbacks;
    RefSimCamera cam[2];
} RefSimTick;
/* Fills the snapshot after flushing (exporting) both cameras. HOST only. */
void ref_sim_snapshot(RefSimTick* out);
#endif

#endif
