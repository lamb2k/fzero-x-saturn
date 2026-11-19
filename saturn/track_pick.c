/* FZX_TRACK_BAKED (default 0): Saturn-native track visibility pick. Runs SH2ZAM's
 * segmesh-1 pick over the course's integer anchors and writes the road plan's
 * far-to-near chunk groups. The anchors and the per-frame scratch are mutable
 * state in high WRAM (small); see generated/TRACK-BAKE-PLAN.md for the baked
 * mesh data that later steps place on the cartridge. */
#include <stdint.h>
#include <stddef.h>
#include "track_pick.h"
#include "sh2zam/segmesh_experimental.h"

_Static_assert(FZX_TRACK_MAX_SEGMENTS <= SH2Z_SEGMESH_MAX_SEGMENTS, "track ring bound");
_Static_assert(sizeof(int) == sizeof(int32_t), "SH-2 int is 32 bits");

static sh2z_segmesh_anchor_t anchors[FZX_TRACK_MAX_SEGMENTS];
static int32_t pick_depth[FZX_TRACK_MAX_SEGMENTS + 1u];
static int8_t pick_state[FZX_TRACK_MAX_SEGMENTS + 1u];
static sh2z_segmesh_run_t pick_runs[REF_ROAD_GROUPS];
static unsigned anchor_count;
/* Last pick result (sh2z code) and the number of runs, for capture tools. */
volatile int32_t fzx_track_pick_result;
volatile uint32_t fzx_track_pick_runs, fzx_track_pick_marked;

int fzx_track_anchors_begin(unsigned n) {
    if (n < 3u || n > FZX_TRACK_MAX_SEGMENTS) { anchor_count = 0; return 0; }
    anchor_count = n;
    return 1;
}

void fzx_track_anchor_set(unsigned i, short x, short y, short z) {
    if (i < anchor_count) {
        anchors[i].x = x; anchors[i].y = y; anchors[i].z = z;
    }
}

int fzx_track_pick(RefRoadPlan *out, const FzxTrackPickView *v) {
    if (!out || !v || anchor_count == 0u) return 0;
    sh2z_segmesh_view_t sv;
    for (unsigned i = 0; i < 4u; ++i) {
        sv.x[i] = v->rows[i];
        sv.y[i] = v->rows[4u + i];
        sv.w[i] = v->rows[8u + i];
        sv.d[i] = v->rows[12u + i];
    }
    sv.far = v->thresholds[0];
    sv.slope = v->thresholds[1];
    sv.split = v->thresholds[2];
    sv.floor = v->thresholds[3];
    const sh2z_segmesh_scratch_t scratch = { pick_depth, pick_state };
    uint32_t runs = 0, marked = 0;
    const int r = sh2z_segmesh_pick(anchors, anchor_count, &sv, &scratch, pick_runs, REF_ROAD_GROUPS, &runs, &marked);
    fzx_track_pick_result = r;
    fzx_track_pick_runs = runs;
    fzx_track_pick_marked = marked;
    if (r == SH2Z_SEGMESH_BAD_ARGS) return 0;
    if (r == SH2Z_SEGMESH_TOO_MANY) { out->faults = 2; return -2; }
    out->visible_chunks = marked;
    if (marked == 0u) return -1;
    out->count = runs;
    for (uint32_t i = 0; i < runs; ++i) {
        RefRoadGroup *g = &out->groups[i];
        g->start = pick_runs[i].start;
        g->end = pick_runs[i].end;
        g->direction = pick_runs[i].direction;
        g->average_depth = (float)pick_runs[i].average * v->depth_unit;
    }
    return 1;
}
