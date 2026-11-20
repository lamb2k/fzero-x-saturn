#ifndef FZX_ROAD_STREAM_NATIVE_H
#define FZX_ROAD_STREAM_NATIVE_H
#include <yaul.h>
#include "fzx_road_stream_draw.h"
#include "fzx_road_stream_limits.h"
extern volatile uint32_t fzx_road_stream_status,fzx_road_stream_busy;
extern volatile uint32_t fzx_road_stream_low_start,fzx_road_stream_low_end;
extern volatile uint32_t fzx_road_stream_cart_start,fzx_road_stream_cart_end;
extern volatile uint32_t fzx_road_stream_fields,fzx_road_stream_sequence;
extern const uint32_t fzx_road_renderer_id;
extern fzx_road_stream_draw_stats_t fzx_road_stream_stats;
/* Read-only capture ABI: magic/version, work/packets/count, commands/count,
 * pixels/bytes, shades/bytes, sizeof(work/packet/stats), sequence, reserved,
 * remap/count, positions/count, projected/count, images/count. */
extern volatile uint32_t fzx_road_stream_report[24];
bool fzx_road_stream_load(void *vram);
bool fzx_road_stream_guards(void);
#ifndef FZX_ROAD_STREAM_CLUT_ALIGN
#define FZX_ROAD_STREAM_CLUT_ALIGN 0
#endif
#if FZX_ROAD_STREAM_CLUT_ALIGN
/* VDP1 reads a colour lookup table from a 32-byte address: it ignores the two low bits of
 * CMDCOLR (units of 8 bytes). The road bake stores its 32-byte tables at 8-byte offsets, so
 * after fzx_road_stream_load this copies every table that is not 32-byte aligned in VDP1 VRAM
 * into area (32-byte aligned, area_bytes long, outside the texture bank) and points the
 * images at the copies. *used: the bytes taken, a multiple of 32. Run after every load.
 * fzx_road_stream_clut_report: [0] tables moved, [1] lookup images, [2] status
 * (1 done, 2 bad arguments, 3 area too small). */
extern volatile uint32_t fzx_road_stream_clut_report[3];
bool fzx_road_stream_clut_align(void *area,uint32_t area_bytes,uint32_t *used);
void fzx_road_stream_clut_touch(void);
#endif
bool fzx_road_stream_native(vdp1_cmdt_t *,unsigned,const fzx_view_t *,const int32_t eye[3],
    const RefRoadPlan *,int32_t,int32_t,void *,void *,uint32_t *);
#endif
