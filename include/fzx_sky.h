/* Native sky: source triangles split at the authored 1D gradient samples.
 * No framebuffer shading and no source geometry or texture stored here. */
#ifndef FZX_SKY_H
#define FZX_SKY_H
#include "fzx_view.h"
#include "backdrop_state.h"

typedef struct { int16_t x[4],y[4]; uint16_t color[4]; } fzx_sky_polygon_t;
typedef bool (*fzx_sky_emit_fn)(void *,const fzx_sky_polygon_t *);
typedef struct { uint32_t polygons,bands,clipped,fault; } fzx_sky_stats_t;

/* 64 opaque Saturn RGB1555 samples, source sky vertices 8..11 in their
 * original order, and the already-published source camera view. Native
 * Gouraud interpolation/5-bit rounding is an explicit visual adaptation.
 * A failure may have emitted a partial list: never submit it as a frame. */
bool fzx_sky_draw(const fzx_view_t *view,const RefBackdropVertex vertices[4],
                  const uint16_t colors[64],uint32_t capacity,
                  fzx_sky_emit_fn emit,void *context,fzx_sky_stats_t *stats);
/* Optional offline-selected sample boundaries, strictly increasing 0..63.
 * NULL/0 retains all original sample intervals. Source geometry is unchanged. */
bool fzx_sky_draw_bands(const fzx_view_t *view,const RefBackdropVertex vertices[4],
                       const uint16_t colors[64],const uint8_t *breaks,unsigned break_count,
                       uint32_t capacity,fzx_sky_emit_fn emit,void *context,fzx_sky_stats_t *stats);
#endif
