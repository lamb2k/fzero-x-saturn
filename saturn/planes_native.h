#ifndef FZX_PLANES_NATIVE_H
#define FZX_PLANES_NATIVE_H
#include <yaul.h>
#include "fzx_planes.h"
#ifndef FZX_PLANE_CLOUD_ONLY
#define FZX_PLANE_CLOUD_ONLY 0
#endif
#if FZX_PLANE_CLOUD_ONLY != 0 && FZX_PLANE_CLOUD_ONLY != 1
#error "FZX_PLANE_CLOUD_ONLY must be 0 or 1"
#endif
#if FZX_PLANE_CLOUD_ONLY && (!defined(FZX_SOURCE_FLOOR_RBG) || !FZX_SOURCE_FLOOR_RBG || \
                            !defined(FZX_SOURCE_PLANES) || !FZX_SOURCE_PLANES)
#error "Cloud-only residency requires source planes and the RBG0 floor"
#endif
enum {FZX_PLANE_RESIDENT_BYTES=FZX_PLANE_CLOUD_ONLY?FZX_PLANE_LAYER_BYTES:FZX_PLANE_PACK_BYTES};
enum {FZX_PLANE_SHADE_BYTES=FZX_PLANE_MAX_DRAWS*8*2};
extern fzx_plane_stats_t fzx_plane_stats;
extern volatile uint32_t fzx_plane_status,fzx_plane_busy,fzx_plane_ticks;
extern volatile uint32_t fzx_plane_fields;
bool fzx_planes_load(void *vram);
bool fzx_planes_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,void *vram,void *shades,uint32_t *count);
#endif
