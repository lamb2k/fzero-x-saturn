/* Engine line (docs/ENGINE_PLAN.md, E0): camera-derived scene state in fixed
 * point. Nothing here links or calls original game code. The packets it fills
 * (RefBackdrop, RefSkyline, RefRoadPlan) are the plain-data inputs of the
 * native renderers; their float fields are left zero except the road group
 * depth, which the road draw reads. Portable C: built for SH-2 and for HOST
 * tests. */
#ifndef FZX_ENGINE_SCENE_H
#define FZX_ENGINE_SCENE_H
#include <stdbool.h>
#include <stdint.h>
#include "backdrop_state.h"
#include "skyline_state.h"
#include "road_plan.h"
#include "fzx_view.h"

/* One camera sample. rotation and translation are fzx_view_t's (rows right,
 * up, forward in Q2.30; translation Q16.16 at 1/16 source scale). eye is
 * Q16.16 source units. xzn is the forward vector's normalised xz part, Q2.30.
 * tan_half is tan(fov/2), Q16.16. */
typedef struct {
    int32_t rotation[3][3], translation[3];
    int32_t eye[3];
    int32_t focal[2];
    int32_t tan_half;
    int32_t xzn[2];
} fzx_engine_cam_t;
_Static_assert(sizeof(fzx_engine_cam_t) == 80, "camera record is 80 bytes");

/* Per-course and per-venue constants, baked offline (tools/engine_path.py). */
typedef struct {
    int32_t near_plane, far_plane, center[2]; /* fzx_view_t units */
    int16_t clip[4];
    int32_t pa, pb;            /* depth = pa/2^30 * forward distance + pb/4096 */
    int32_t viewport[2];       /* viewport scale, whole pixels */
    int32_t origin_distance;   /* Q16.16: the pick's eye pull-back */
    int32_t far_distance;      /* Q16.16 */
    int32_t split_distance;    /* Q16.16 */
    int32_t fog_start;
    int32_t aspect;            /* Q16.16 vertical/horizontal backdrop range */
    int32_t lateral_sign;      /* +1: backdrop lateral axis is the view's right row */
    int32_t center_h, center_v;/* Q16.16 sky centre offsets as fractions of the fov radius */
    uint8_t fog[4];
    uint32_t anchor_shift;     /* anchors are source units >> shift */
} fzx_engine_consts_t;

/* Venue tuning (Q16.16 unless noted). Values are the source's. */
enum {
    FZX_ENGINE_BG_SCALE = 100, FZX_ENGINE_SCROLL_DEPTH = 6000, FZX_ENGINE_SKYBOX_DEPTH = 7000,
    FZX_ENGINE_BG_Y = -750, FZX_ENGINE_CLOUD_EYE_HEIGHT = 400, FZX_ENGINE_CLOUD_BG_HEIGHT = 1700,
    FZX_ENGINE_SKYLINE_RADIUS = 8000, FZX_ENGINE_SKYLINE_Y = 400, FZX_ENGINE_SKYLINE_SCALE = 50,
    FZX_ENGINE_SKYLINE_LIMIT = 23000
};

/* Backdrop for logic tick TICK (1-based: the cloud scroll has advanced
 * 2*TICK steps). Returns false and sets out->faults on an unusable camera. */
bool fzx_engine_backdrop(RefBackdrop *out, const fzx_engine_cam_t *cam,
                         const fzx_engine_consts_t *k, uint32_t tick);
/* The six skyline billboards. */
bool fzx_engine_skyline(RefSkyline *out, const fzx_engine_cam_t *cam);
/* The native view for the renderers. */
void fzx_engine_view(fzx_view_t *out, const fzx_engine_cam_t *cam, const fzx_engine_consts_t *k);

/* Clip rows and thresholds for sh2z_segmesh_pick (rows x[4], y[4], w[4], d[4];
 * thresholds far, slope, split, floor). depth_shift is K: source depth =
 * depth units >> K. Returns false when a row leaves the pick's contract. */
typedef struct {
    int32_t rows[16];
    int32_t thresholds[4];
    uint32_t depth_shift;
} fzx_engine_pick_t;
bool fzx_engine_pick_view(fzx_engine_pick_t *out, const fzx_engine_cam_t *cam,
                          const fzx_engine_consts_t *k);
#endif
