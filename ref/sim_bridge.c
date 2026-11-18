/* Simulation-lane bridge (decomp headers). Built only with FZX_DEFER_PROJECTION=1.
 * MIT; see ../LICENSE. */
#include "global.h"
#include "fzx_camera.h"
#include "fzx_game.h"
#include "fzx_racer.h"
#include "controller.h"
#include "sim.h"

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
}
#endif
