/* Source-camera bridge. Source owns camera state; no reimplementation here. */
#include "global.h"
#include "fzx_camera.h"
#include "ref.h"
#include "viewports.h"

_Static_assert(sizeof(RefCamera) == 252, "camera trace ABI");
static unsigned int camera_faults;
int ref_camera_active;

/* Only exported viewport objects are mapped. Other presentation loads retain
 * the original reference harness's null mapping; no N64 segment interpreter. */
uintptr_t Segment_SegmentedToVirtual(uintptr_t address) {
    unsigned int i;
    for (i = 0; i < 13; ++i) {
        if (address == (uintptr_t)ref_viewports[i]) return address;
    }
    if (ref_camera_active) ++camera_faults;
    return 0;
}

void ref_camera_init(int cold) {
    camera_faults = 0;
    ref_camera_active = 1;
    if (cold) Camera_StartInit();
    Camera_Init();
    ref_camera_active = 0;
}

void ref_camera_tick(void) {
    ref_camera_active = 1;
    Camera_Update();
    ref_camera_active = 0;
}

unsigned int ref_camera_count(void) {
    extern s32 sNumCameras;
    return (unsigned int)sNumCameras;
}

void ref_camera(RefCamera* out) { (void)ref_camera_read(0, out); }

int ref_camera_read(unsigned int index, RefCamera* out) {
    const Camera* c;
    if (!out || index >= ref_camera_count()) return 0;
    c = &gCameras[index];
    memset(out, 0, sizeof(*out));
    out->mode = c->mode;
    out->setting = c->raceSetting;
    out->timer = c->timer;
    out->look_back = c->lookBackRotate;
    memcpy(out->eye, &c->eye, sizeof(out->eye));
    memcpy(out->at, &c->at, sizeof(out->at));
    memcpy(out->basis, &c->basis, sizeof(out->basis));
    out->fov = c->fov;
    out->near_plane = c->near;
    out->far_plane = c->far;
    out->viewport[0] = c->currentVpScaleX;
    out->viewport[1] = c->currentVpScaleY;
    out->viewport[2] = c->currentVpTransX;
    out->viewport[3] = c->currentVpTransY;
    out->scissor[0] = c->currentScissorLeft;
    out->scissor[1] = c->currentScissorTop;
    out->scissor[2] = c->currentScissorRight;
    out->scissor[3] = c->currentScissorBottom;
    memcpy(out->view, &c->viewMtx, sizeof(out->view));
    memcpy(out->projection, &c->projectionMtx, sizeof(out->projection));
    out->faults = camera_faults;
    return 1;
}
