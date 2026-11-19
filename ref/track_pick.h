/* FZX_TRACK_BAKED (default 0): plain-C boundary between the road planner's
 * translation unit and the Saturn-native track visibility pick (SH2ZAM
 * segmesh-1, saturn/track_pick.c). The planner unit builds integer anchors once
 * per course and fixed-point clip rows per frame; the Saturn side runs the pick
 * and writes the plan's groups. No N64 types cross this boundary. */
#ifndef FZX_REF_TRACK_PICK_H
#define FZX_REF_TRACK_PICK_H
#include "road_plan.h"

/* Largest ring the native pick stores; longer courses use the source loop. */
#define FZX_TRACK_MAX_SEGMENTS 512u

/* Rows: x[4], y[4], w[4], d[4] (columns 0..2 then translation), as in
 * sh2z_segmesh_view_t. Thresholds: far, slope, split, floor (depth units). */
typedef struct {
    int rows[16];
    int thresholds[4];
    float depth_unit;  /* source depth = depth units * depth_unit */
} FzxTrackPickView;

/* Bumped by the port wherever the course's chunk ring is built or replaced
 * (ref_race_init after Course_Init); the planner rebuilds its anchors when it
 * changes. Defined in the planner unit. */
extern unsigned ref_track_ring_generation;

/* Once per course: n anchors, then each anchor. begin returns 0 when n is
 * unsupported (the caller keeps the source path). */
int fzx_track_anchors_begin(unsigned n);
void fzx_track_anchor_set(unsigned i, short x, short y, short z);

/* Per frame. Returns 1 when out's groups and visible_chunks are written; -1 when
 * nothing is visible (out untouched, the planner returns); -2 when the run pool
 * overflowed (out->faults = 2); 0 when the view is outside the pick's contract
 * (out untouched; the caller runs the source loop). */
int fzx_track_pick(RefRoadPlan *out, const FzxTrackPickView *view);
#endif
