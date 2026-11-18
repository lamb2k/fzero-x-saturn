#ifndef FZX_ROAD_STREAM_NATIVE_H
#define FZX_ROAD_STREAM_NATIVE_H
#include <yaul.h>
#include "fzx_road_stream_draw.h"
enum {FZX_ROAD_STREAM_MAX_DRAWS=512,FZX_ROAD_STREAM_SHADE_BYTES=512*8};
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
bool fzx_road_stream_native(vdp1_cmdt_t *,unsigned,const fzx_view_t *,const int32_t eye[3],
    const RefRoadPlan *,int32_t,int32_t,void *,void *,uint32_t *);
#endif
