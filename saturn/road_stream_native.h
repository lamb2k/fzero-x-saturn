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
#if FZX_ROAD_STREAM_SPLIT
/* Two-CPU road build (ENG_ROAD_STEAL): the slave claims the plan's groups from the first, the master from the last
 * (fzx_road_stream_steal in fzx_road_stream_draw.c). The master calls, in order:
 *   arm(tag)       before the slave is started: a new nonzero tag below 2^31 for every frame;
 *   steal_run(...) when its own work is done and before it waits for the slave: builds the groups it can claim;
 *   merge_job(...) after the slave's job is finished: appends them after the slave's; false = redo the frame whole
 *                  (fzx_road_stream_native_whole) with the slave idle.
 * fzx_road_steal_report: [0] frames tried, [1] frames that stole, [2] groups stolen, [3] commands stolen, [4] faults and
 * merge refusals (frames redone whole), [5] master ticks in steal_run (sum), [6] in merge_job (sum), [7] frames the slave
 * drew again whole (over budget). */
extern volatile uint32_t fzx_road_steal_report[16];
/* [8] units left behind the slave's current one when the master arrives (sum), [9] frames with any, [10] most, [11] units (sum). */
void fzx_road_stream_split_arm(uint32_t tag);
void fzx_road_stream_steal_run(const fzx_view_t *view,const int32_t eye[3],const RefRoadPlan *plan,void *pixels,void *shades,
    vdp1_cmdt_t *commands,unsigned capacity,bool (*slave_done)(void));
bool fzx_road_stream_steal_merge_job(const RefRoadPlan *plan,vdp1_cmdt_t *commands,void *shades,uint32_t *count);
bool fzx_road_stream_native_whole(vdp1_cmdt_t *,unsigned,const fzx_view_t *,const int32_t eye[3],
    const RefRoadPlan *,int32_t,int32_t,void *,void *,uint32_t *);
#endif
#endif
