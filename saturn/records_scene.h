#ifndef FZX_RECORDS_SCENE_H
#define FZX_RECORDS_SCENE_H
#include "records_native.h"
enum {FZX_RECORDS_UNLOADED,FZX_RECORDS_READY,FZX_RECORDS_RANGE,FZX_RECORDS_GUARD,
      FZX_RECORDS_ASSET,FZX_RECORDS_STATE,FZX_RECORDS_DRAW};
extern volatile uint32_t fzx_records_status,fzx_records_busy,fzx_records_active;
extern volatile uint32_t fzx_records_low_start,fzx_records_low_end;
extern volatile uint32_t fzx_records_cart_start,fzx_records_cart_end;
extern volatile uint32_t fzx_records_command_start,fzx_records_command_count;
extern volatile uint32_t fzx_records_vram_start,fzx_records_vram_bytes;
extern fzx_records_snapshot_t *fzx_source_records;
extern fzx_records_draw_snapshot_t *fzx_source_records_draw;
extern fzx_records_input_t fzx_source_records_input;
bool fzx_records_load(uint32_t low_end,uint32_t cart_end,void *vram,unsigned bytes,const void *tints);
bool fzx_records_guards(void);
bool fzx_records_tick(fzx_results_state_t *results,uint32_t frame,bool connected);
bool fzx_records_restart(void);
/* Caller has waited for previous VDP1 submission before borrowing memory. */
bool fzx_records_scene_draw(vdp1_cmdt_t *,unsigned,unsigned *count);
#endif
