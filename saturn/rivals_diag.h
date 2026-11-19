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

/* Hand-off to the sprite lane (agreed format for fzx_racer_sprites_draw16): one draw pose per rival.
 *   pos     world position Q16.16, same axes as RefMachine.model_pos
 *   basis   Q1.14 rows, orthonormal to about 1/1000: 0 forward (model z), 1 up (model y),
 *           2 side (model x) = the source basis z, positive LEFT (as RefMachine.model_basis)
 *   set     sprite set index (0 = the player's set for now)
 *   flags   bit0 draw shadow */
typedef struct {
    int32_t pos[3];
    int16_t basis[3][3];
    uint16_t set;
    uint16_t flags;
} fzx_racer_pose16_t; /* 34 bytes of fields, sizeof 36 on SH-2 (int32 alignment) */
_Static_assert(sizeof(fzx_racer_pose16_t) == 36, "pose16 hand-off ABI");

/* Per-node baked pose in RIVALS.PAK: position Q4, unit rows Q1.14 (forward, up, left). */
typedef struct {
    int32_t pos[3];
    int16_t fwd[3], up[3], left[3];
    uint16_t pad;
} fzx_rival_node_pose_t; /* 32 bytes */

/* Point into low WRAM once the pack is loaded (NULL before); fzx_rival_count entries each.
 * fzx_rival_ranks[i] is rival i's place among the rivals (0 = leader). */
extern fzx_racer_pose16_t *fzx_rival_poses;
extern uint8_t *fzx_rival_ranks;
extern volatile uint32_t fzx_rival_count;

/* Counters for the diagnostic (FRT ticks at the frame profiler's clock). */
extern volatile uint32_t fzx_rivals_prof[8];
/* 0 executed sim ticks, 1 FRT in sim steps, 2 max FRT per tick, 3 presented frames,
 * 4 FRT in pose output, 5 max FRT per pose pass, 6 race start tick seen, 7 laps completed (sum) */

/* Loads RIVALS.PAK into low WRAM from low_end up (once, before the frame profile takes the
 * rest); returns the new low end (unchanged on failure, see fzx_rivals_load_status). */
uint32_t fzx_rivals_diag_load(uint32_t low_end);
extern volatile uint32_t fzx_rivals_load_status;
void fzx_rivals_diag_reset(void);
/* Steps every rival `ticks` times (one call per executed tick batch); race_time gates the start. */
void fzx_rivals_diag_ticks(uint32_t ticks, int32_t race_time);
/* Rewrites fzx_rival_poses from the current state. */
void fzx_rivals_diag_poses(void);
#endif
