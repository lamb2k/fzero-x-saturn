#ifndef FZX_PLANES_H
#define FZX_PLANES_H
#include "fzx_view.h"
#include "backdrop_state.h"
enum {FZX_PLANE_LEVELS=4,FZX_PLANE_VARIANTS=8,FZX_PLANE_LAYER_BYTES=131072,
      FZX_PLANE_PACK_BYTES=262144,FZX_PLANE_MAX_DRAWS=640};
typedef struct {
    int16_t x[4],y[4];
    uint16_t shade[4],width,height;
    uint32_t offset;
    uint8_t layer,level,variant,pad;
} fzx_plane_quad_t;
typedef struct {uint32_t draws,tiles,split,clipped,near_adapted,fault,hash;} fzx_plane_stats_t;
typedef bool (*fzx_plane_emit_fn)(void *,const fzx_plane_quad_t *);
/* Original floor/cloud planes and repeating 64x32 tiles. Atlas levels contain
 * 1,4,16,64 subtiles; no runtime texel conversion. Native affine mapping,
 * quantized fog/coverage and bounded guard-band clipping are adaptations. */
bool fzx_planes_draw(const fzx_view_t *view,const RefBackdrop *source,
                     const uint16_t fog[3],const uint16_t mean[3],uint32_t capacity,
                     fzx_plane_emit_fn emit,void *context,fzx_plane_stats_t *stats);
#endif
