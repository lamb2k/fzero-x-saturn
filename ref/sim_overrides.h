/* Simulation-lane overrides, force-included after ref/overrides.h with
 * FZX_DEFER_PROJECTION=1 (ref/sat/closure.sh, ref/sim_build.sh). Never edits the decomp.
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

#if FZX_DEFER_PROJECTION
void ref_camera_defer_projection(void* pool, void* camera);
#define Camera_UpdateProjectionViewMtx(pool, camera) FZX_CUPVM_##pool, camera)
#define FZX_CUPVM_GfxPool Camera_UpdateProjectionViewMtx(GfxPool
#define FZX_CUPVM_gGfxPool ref_camera_defer_projection(gGfxPool
#endif

#endif
