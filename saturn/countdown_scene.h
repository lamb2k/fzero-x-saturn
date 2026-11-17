#ifndef FZX_COUNTDOWN_SCENE_H
#define FZX_COUNTDOWN_SCENE_H
#include "countdown_draw_native.h"

enum {
    FZX_COUNTDOWN_SCENE_UNLOADED=0,FZX_COUNTDOWN_SCENE_READY=1,
    FZX_COUNTDOWN_SCENE_RANGE,FZX_COUNTDOWN_SCENE_METADATA,
    FZX_COUNTDOWN_SCENE_PACK,FZX_COUNTDOWN_SCENE_TEXTURE,
    FZX_COUNTDOWN_SCENE_PREPARE,FZX_COUNTDOWN_SCENE_GUARD,
    FZX_COUNTDOWN_SCENE_PUBLISH,FZX_COUNTDOWN_SCENE_DRAW,
    FZX_COUNTDOWN_SCENE_ARGUMENT,
    FZX_COUNTDOWN_SCENE_GUARD_WORD=0x5ac81e39
};
extern volatile uint32_t fzx_countdown_status,fzx_countdown_busy;
extern volatile uint32_t fzx_countdown_ticks,fzx_countdown_fields;
extern volatile uint32_t fzx_countdown_low_start,fzx_countdown_low_end;
extern volatile uint32_t fzx_countdown_texture_start,fzx_countdown_texture_bytes;
extern volatile uint32_t fzx_countdown_command_start;
extern RefCountdown fzx_source_countdown;
extern fzx_countdown_native_stats_t fzx_countdown_stats;

/* Boot-only. low_end is the preceding owner's exclusive data end (its trailing
 * guard may occupy the next 32 bytes). Metadata begins at (low_end+95)&~31,
 * then a low-RAM pack/work context. Eight guard words precede low_start and
 * follow low_end. Scratch is align32(sat_stub_data_end)..fzx_input_log-32.
 * The caller reserves this full low-RAM extent and the supplied VDP1 bank.
 * Both complete files are identity-checked before READY is published. */
bool fzx_countdown_load(uint32_t low_end,void *vram,uint32_t reserved_bytes);

/* Read the source's completed tick after camera publication. Does not advance
 * or rebuild the source matrix. Native draw requires a successful publication. */
bool fzx_countdown_publish(void);

/* Append after the machine and before the HUD, matching source draw order.
 * Capacity includes the adapter's user clip; append END/submit in the caller.
 * Count is zero on failure; discard the whole submitted list on any failure.
 * ticks covers command construction/checks only; caller records fields.
 * command_start is zero for an empty sign; stats.commands is the emitted span. */
bool fzx_countdown_native(vdp1_cmdt_t *,unsigned capacity,const fzx_view_t *,uint32_t *count);
bool fzx_countdown_guards(void);
#endif
