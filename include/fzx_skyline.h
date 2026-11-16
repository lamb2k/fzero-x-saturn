#ifndef FZX_SKYLINE_H
#define FZX_SKYLINE_H
#include "fzx_view.h"
#include "skyline_state.h"
typedef struct {int16_t x[4],y[4];uint32_t texture;} fzx_skyline_quad_t;
typedef struct {uint32_t polygons,clipped,fault;} fzx_skyline_stats_t;
typedef bool (*fzx_skyline_emit_fn)(void *,const fzx_skyline_quad_t *);
/* Original billboard matrices/vertices. VDP1 clips side-plane crossings;
 * source-visible near-plane or coordinate overflow is an explicit failure. */
bool fzx_skyline_draw(const fzx_view_t *view,const RefSkyline *scene,uint32_t capacity,
                     fzx_skyline_emit_fn emit,void *context,fzx_skyline_stats_t *stats);
#endif
