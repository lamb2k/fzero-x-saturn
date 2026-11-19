/* FZX_SLAVE_ROAD=1: the slave SH-2 builds the C3 road while the master builds
 * the rest of the scene (generated/SLAVE-ROAD-PLAN.md). Default 0. */
#ifndef FZX_SLAVE_ROAD_H
#define FZX_SLAVE_ROAD_H
#include <yaul.h>
#include "road_stream_native.h"

/* Command slots reserved ahead of the road block for the master's prelude
 * (sky lines, cloud bands, skyline, floor): measured maximum 30. */
#define FZX_SLAVE_ROAD_RESERVE 128u

typedef struct {
    vdp1_cmdt_t *commands;unsigned capacity;const fzx_view_t *view;
    int32_t eye[3];const RefRoadPlan *plan;int32_t pa,pb;void *pixels,*shades;
} fzx_slave_road_args_t;

/* Counters, read by captures: jobs, late jobs (over 4 fields), master wait
 * ticks (sum, max), slave road ticks (sum, max), slave stack bytes never used
 * (minimum over the race), status. */
extern volatile uint32_t fzx_slave_road_jobs,fzx_slave_road_late;
extern volatile uint32_t fzx_slave_road_wait_sum,fzx_slave_road_wait_max;
extern volatile uint32_t fzx_slave_road_busy_sum,fzx_slave_road_busy_max;
extern volatile uint32_t fzx_slave_road_stack_unused,fzx_slave_road_status;
/* Slave stack guard (0x06002000): trips, job of the first trip, lowest slave
 * stack address seen. Status 7 = guard tripped (fail-stop), 8 = stack not linked
 * at the reservation, 9 = slave timeout, 4 = road failed. */
extern volatile uint32_t fzx_slave_road_guard_trips,fzx_slave_road_guard_first_job,fzx_slave_road_sp;

/* Boot, once, after the road renderer has loaded. */
void fzx_slave_road_init(void);
/* Master: hand this frame's road to the slave (VDP1 must be idle). */
void fzx_slave_road_start(const fzx_slave_road_args_t *args);
/* Master: bounded wait; returns the slave's result and road count. The
 * slave's own road ticks go to fzx_slave_road_busy_*. video_fields is the
 * master's field counter. */
bool fzx_slave_road_finish(const volatile uint32_t *video_fields,uint32_t *count);
#endif
