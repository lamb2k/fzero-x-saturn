#ifndef FZX_PLANES_NATIVE_H
#define FZX_PLANES_NATIVE_H
#include <yaul.h>
#include "fzx_planes.h"
enum {FZX_PLANE_SHADE_BYTES=FZX_PLANE_MAX_DRAWS*8*2};
extern fzx_plane_stats_t fzx_plane_stats;
extern volatile uint32_t fzx_plane_status,fzx_plane_busy,fzx_plane_ticks;
extern volatile uint32_t fzx_plane_fields;
bool fzx_planes_load(void *vram);
bool fzx_planes_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,void *vram,void *shades,uint32_t *count);
#endif
