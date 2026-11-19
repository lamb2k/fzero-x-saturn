#ifndef FZX_ROAD_STREAM_LIMITS_H
#define FZX_ROAD_STREAM_LIMITS_H
/* Road stream sizes shared by the Saturn wrapper (saturn/road_stream_native.h)
 * and HOST tools (tools/e0_road_host.c). Constants only. */
enum {FZX_ROAD_STREAM_MAX_DRAWS=512,FZX_ROAD_STREAM_SHADE_BYTES=512*8};
/* Owner road safety budget: road commands per frame, user clip included. */
enum {FZX_ROAD_STREAM_BUDGET_COMMANDS=420};
#endif
