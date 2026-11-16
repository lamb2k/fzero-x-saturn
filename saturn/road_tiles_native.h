#ifndef FZX_ROAD_TILES_NATIVE_H
#define FZX_ROAD_TILES_NATIVE_H
#include <yaul.h>
#include "fzx_road_tiles.h"
enum {FZX_ROAD_TILE_MAX_DRAWS=768,FZX_ROAD_TILE_SHADE_BYTES=FZX_ROAD_TILE_MAX_DRAWS*8};
extern volatile uint32_t fzx_road_tile_status,fzx_road_tile_busy,fzx_road_tile_ticks,fzx_road_tile_fields;
extern volatile uint32_t fzx_road_tile_low_start,fzx_road_tile_low_end;
extern fzx_road_tile_stats_t fzx_road_tile_stats;
bool fzx_road_tiles_load(void *);
bool fzx_road_tiles_native(vdp1_cmdt_t *,unsigned,const fzx_view_t *,const RefRoadPlan *,int32_t,int32_t,void *,void *,uint32_t *);
#endif
