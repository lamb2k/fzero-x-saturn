/* Simulation-lane overrides, force-included after ref/overrides.h when a sim
 * knob is on (ref/sat/closure.sh, ref/sim/build.sh). Never edits the decomp.
 * MIT; see ../LICENSE.
 *
 * FZX_DEFER_PROJECTION (quick win 1): Camera_Update's per-tick
 * Camera_UpdateProjectionViewMtx(gGfxPool, camera) call becomes
 * ref_camera_defer_projection(gGfxPool, camera), which only records the
 * camera. ref/sim_bridge.c builds the matrices from the unchanged camera state
 * when they are first read (ref_camera_flush). Nothing in the tick reads
 * them, so simulation state and the published matrices stay bit-identical.
 *
 * The macro dispatches on the first token of its first argument: the
 * definition in camera.c (first token GfxPool) keeps its name, the one call
 * (gGfxPool) is redirected, and any other spelling fails to compile.
 * ref/sim_bridge.c calls the original as (Camera_UpdateProjectionViewMtx)(...),
 * which suppresses the function-like macro. */
#ifndef FZX_SIM_OVERRIDES_H
#define FZX_SIM_OVERRIDES_H

#ifndef FZX_DEFER_PROJECTION
#define FZX_DEFER_PROJECTION 0
#endif

#ifndef FZX_SIM_CAMERA
#define FZX_SIM_CAMERA 0
#endif

#if FZX_DEFER_PROJECTION
void ref_camera_defer_projection(void* pool, void* camera);
#define Camera_UpdateProjectionViewMtx(pool, camera) FZX_CUPVM_##pool, camera)
#define FZX_CUPVM_GfxPool Camera_UpdateProjectionViewMtx(GfxPool
#define FZX_CUPVM_gGfxPool ref_camera_defer_projection(gGfxPool
#endif

/* FZX_SIM_CAMERA (camera milestone): the bridge's Camera_Update() call becomes
 * ref_sim_camera_update(), which runs the fixed-point camera or the source
 * (ref/sim_bridge.c). The same first-token dispatch: the prototype and the
 * definition (argument "void") keep their name; the call (no argument) is
 * redirected. ref/sim_bridge.c calls the source as (Camera_Update)(). */
#if FZX_SIM_CAMERA
void ref_sim_camera_update(void);
#define Camera_Update(x) FZX_CU_##x
#define FZX_CU_void Camera_Update(void)
#define FZX_CU_ ref_sim_camera_update()
/* Camera_Init() (race init and menus) re-initialises the float cameras: the
 * fixed state must be re-imported, so the call is redirected the same way. */
void ref_sim_camera_init(void);
#define Camera_Init(x) FZX_CINIT_##x
#define FZX_CINIT_void Camera_Init(void)
#define FZX_CINIT_ ref_sim_camera_init()
#endif

#endif
