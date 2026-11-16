#ifndef FZX_ROAD_NATIVE_H
#define FZX_ROAD_NATIVE_H
#include "fzx_road_pack.h"
#include "road_plan.h"
/* Boot only. No disc access or allocation occurs in the race loop. */
bool fzx_road_load(const void *course,uint32_t course_bytes);
extern fzx_road_pack_t fzx_native_road;
typedef struct {
    uint32_t status,cart_id,bytes,hash,sectors,vertices,triangles,chunks;
    int32_t io_result;
    uint32_t volume_word;
} fzx_road_load_status_t;
extern volatile fzx_road_load_status_t fzx_road_load_status;
/* Presentation clock: phase zero at the first completed race tick, unchanged
 * by an ordinary retry. Debugger replays may explicitly set next_frame. */
void fzx_road_note_tick(void);
void fzx_road_restart(void);
bool fzx_road_publish(void);
extern volatile uint32_t fzx_road_next_frame, fzx_road_frame;
extern volatile uint32_t fzx_road_plan_status, fzx_road_plan_sequence;
extern RefRoadPlan fzx_source_road_plan;
#endif
