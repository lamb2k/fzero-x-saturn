#ifndef FZX_ROAD_TILES_H
#define FZX_ROAD_TILES_H
#include "fzx_road_pack.h"
#include "fzx_view.h"
#include "road_plan.h"
#ifndef FZX_ROAD_TILE_VERTEX_CACHE
#define FZX_ROAD_TILE_VERTEX_CACHE 0
#endif
#if FZX_ROAD_TILE_VERTEX_CACHE
typedef struct {
    uint32_t frame;int32_t view[3],xy[2];uint8_t outcode,projected;uint16_t reserved;
} fzx_road_tile_vertex_t;
typedef struct {
    uint32_t capacity,frame,hits,misses;fzx_road_tile_vertex_t vertices[];
} fzx_road_tile_cache_t;
#endif
typedef struct {
    const fzx_road_pack_t *road;const uint8_t *data;
    uint32_t bytes,textures,chunks,quads,strips,chunk_at,quad_at,strip_at;
#if FZX_ROAD_TILE_VERTEX_CACHE
    fzx_road_tile_cache_t *cache;
#endif
} fzx_road_tiles_t;
typedef struct {
    int16_t x[4],y[4];uint16_t shade[4],width,height;
    uint32_t offset;bool solid;
} fzx_road_tile_quad_t;
typedef struct {uint32_t chunks,quads,draws,culled,mean,clipped,fault,hash;} fzx_road_tile_stats_t;
typedef bool (*fzx_road_tile_emit_fn)(void *,const fzx_road_tile_quad_t *);
bool fzx_road_tiles_open(fzx_road_tiles_t *,const fzx_road_pack_t *,const void *,uint32_t,uint32_t,uint32_t);
#if FZX_ROAD_TILE_VERTEX_CACHE
/* One immutable pack and one sequential master-CPU draw per cache. The caller
 * owns aligned storage. Every draw invalidates the preceding view, including
 * generation wrap; only repeated source vertex IDs within a draw are reused. */
uint32_t fzx_road_tiles_cache_bytes(uint32_t vertices);
bool fzx_road_tiles_cache_bind(fzx_road_tiles_t *,void *,uint32_t);
#endif
/* Source group order, full geometry and original shared texture strips.
 * Native paired quads, distant longitudinal mean filtering, quantized fog and
 * guard-band remapping are presentation adaptations; no runtime texel work. */
bool fzx_road_tiles_draw(const fzx_road_tiles_t *,const fzx_view_t *,const RefRoadPlan *,
                         int32_t,int32_t,uint32_t,fzx_road_tile_emit_fn,void *,fzx_road_tile_stats_t *);
#endif
