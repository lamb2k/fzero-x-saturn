/* Bounded source-road presentation experiment: masked screen-aligned VDP1
 * charts, one per clipped source triangle. No display-list or depth emulation. */
#ifndef FZX_ROAD_DRAW_H
#define FZX_ROAD_DRAW_H
#include "fzx_road_material.h"
#include "fzx_view.h"
#include "road_plan.h"
typedef struct {
    int16_t x,y;
    uint16_t width,height,triangle,material;
    uint32_t offset;
} fzx_road_chart_t;
typedef struct {
    uint32_t chunks,triangles,culled,charts,bytes,tested_pixels,shaded_pixels;
    uint32_t fault;
} fzx_road_draw_stats_t;
typedef bool (*fzx_road_chart_fn)(void *context,const fzx_road_chart_t *chart);
/* Projection depth coefficients: -P[2][2] in Q2.30 and P[3][2]/16 in Q16.16.
 * The atlas must be idle until this call completes. A failure leaves a partial
 * atlas; the caller must NOT submit it as a frame or hide it with placeholders. */
bool fzx_road_draw(const fzx_road_pack_t *pack,const fzx_view_t *view,
                   const RefRoadPlan *plan,int32_t projection_a,int32_t projection_b,
                   uint16_t *atlas,uint32_t atlas_bytes,uint32_t chart_capacity,
                   fzx_road_chart_fn emit,void *context,fzx_road_draw_stats_t *stats);
#endif
