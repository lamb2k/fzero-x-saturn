#ifndef FZX_RESULTS_SCENE_H
#define FZX_RESULTS_SCENE_H
#include "results_native.h"
#ifndef FZX_SOURCE_RECORDS
#define FZX_SOURCE_RECORDS 0
#endif
#if FZX_SOURCE_RECORDS
#include "records_scene.h"
#endif

enum { FZX_RESULTS_UNLOADED,FZX_RESULTS_READY,FZX_RESULTS_RANGE,
       FZX_RESULTS_GUARD,FZX_RESULTS_ASSET,FZX_RESULTS_STATE,FZX_RESULTS_DRAW };
extern volatile uint32_t fzx_results_status,fzx_results_busy;
extern volatile uint32_t fzx_results_low_start,fzx_results_low_end;
extern volatile uint32_t fzx_results_cart_start,fzx_results_cart_end;
extern volatile uint32_t fzx_results_command_start,fzx_results_command_count;
extern volatile uint32_t fzx_results_vram_start,fzx_results_vram_bytes;
extern volatile uint32_t fzx_results_tint_level,fzx_results_texture_borrowed;
extern fzx_results_snapshot_t *fzx_source_results;
extern fzx_results_input_t fzx_source_results_input;

/* Load after countdown and race workspace. cart_end is the preceding end guard;
 * the scene inserts a separate guard. Assets stay resident in the 4 MiB cart.
 * Countdown VRAM is borrowed only after its timer is zero and the prior VDP1
 * drawing has completed. No streaming reads occur during results animation. */
bool fzx_results_load(uint32_t low_end,uint32_t cart_end,void *vram,unsigned bytes);
bool fzx_results_guards(void);
bool fzx_results_tick(bool connected);
/* Caller waits for VDP1 before drawing or reset. Reset restores countdown VRAM
 * from CD only if borrowed; scene records survive this in-memory restart. */
bool fzx_results_restart(void);
bool fzx_results_native(vdp1_cmdt_t *,unsigned capacity,unsigned *count);
#define FZX_RESULTS_COMMANDS (FZX_RESULTS_UI_COMMANDS+FZX_FINISH_COMMANDS)
#if FZX_SIM_PIPELINE
bool fzx_results_draw_pending(void);
#endif
#endif
