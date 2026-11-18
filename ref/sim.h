/* Simulation-lane interface (plain C types; both the host driver and the
 * decomp-header bridge compile it). MIT; see ../LICENSE. */
#ifndef FZX_SIM_H
#define FZX_SIM_H

#ifndef FZX_DEFER_PROJECTION
#define FZX_DEFER_PROJECTION 0
#endif
#if defined(FZX_SIM_CAMERA) && FZX_SIM_CAMERA
#error "the fixed-point camera (FZX_SIM_CAMERA) is not integrated; it stays on claude/sim-native"
#endif

#if FZX_DEFER_PROJECTION
/* Make camera INDEX's projection/view matrices current: build the deferred
 * matrices. A no-op when they are current. Every reader of a camera's
 * matrices calls it first: the image's publication (camera 0, before
 * RefCamera, skyline, sky, road plan and machine) and the records matrix
 * bridge (camera 1). ref_camera_flush_all also runs before a restart
 * (ref_race_restart / Camera_Init). New readers must flush before they read. */
void ref_camera_flush(unsigned int index);
void ref_camera_flush_all(void);
/* Calls of the deferred function, and matrix builds actually performed. */
extern unsigned int ref_camera_deferrals, ref_camera_deferred_builds;
#endif

#endif
