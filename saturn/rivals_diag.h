/* Rivals diagnostic (claude/rivals-native, FZX_DIAG_RIVALS = N rivals, default 0).
 * POC-1 crude follower: rivals ride the baked CPU line with a pace controller, no AI or contacts.
 * Hand-off to the sprite lane: fzx_rival_poses[] is rewritten once per presented frame. */
#ifndef FZX_RIVALS_DIAG_H
#define FZX_RIVALS_DIAG_H
#include <stdint.h>

#ifndef FZX_DIAG_RIVALS
#define FZX_DIAG_RIVALS 0
#endif
#define FZX_RIVALS_MAX 29

/* One rival's draw pose, in the race's world space.
 *   pos       world position, Q4 (1/16 unit), same axes as the course spline
 *   yaw       heading around world up, 65536 per turn, 0 = +Z, rising toward +X
 *   pitch     nose up (+) from the horizontal, 65536 per turn
 *   roll      bank, 65536 per turn, + = left side higher (the track's bank at that point)
 *   id        rival index 0..N-1 (grid order); rank = place among rivals, 0 = leader
 *   valid     1 once the rival exists (from race set-up), 0 otherwise */
typedef struct {
    int32_t pos[3];
    uint16_t yaw, pitch, roll;
    uint8_t id, rank, valid, pad[3];
} fzx_rival_pose_t; /* 24 bytes */

/* Per-node baked pose (generated header). */
typedef struct {
    int32_t pos[3];
    int16_t right[3], up[3];
    uint16_t yaw, pitch, roll, pad;
} fzx_rival_node_pose_t;

extern fzx_rival_pose_t fzx_rival_poses[FZX_RIVALS_MAX];
extern volatile uint32_t fzx_rival_count;

/* Counters for the diagnostic (FRT ticks at the frame profiler's clock). */
extern volatile uint32_t fzx_rivals_prof[8];
/* 0 executed sim ticks, 1 FRT in sim steps, 2 max FRT per tick, 3 presented frames,
 * 4 FRT in pose output, 5 max FRT per pose pass, 6 race start tick seen, 7 laps completed (sum) */

void fzx_rivals_diag_reset(void);
/* Steps every rival `ticks` times (one call per executed tick batch); race_time gates the start. */
void fzx_rivals_diag_ticks(uint32_t ticks, int32_t race_time);
/* Rewrites fzx_rival_poses from the current state. */
void fzx_rivals_diag_poses(void);
#endif
