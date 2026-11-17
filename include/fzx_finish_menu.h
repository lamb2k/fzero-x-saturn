#ifndef FZX_FINISH_MENU_H
#define FZX_FINISH_MENU_H
#include "fzx_records.h"

#define FZX_FINISH_MENU_EVENT_MAX 4u
enum { FZX_FINISH_MENU_NONE, FZX_FINISH_MENU_RETRY, FZX_FINISH_MENU_SETTINGS,
       FZX_FINISH_MENU_MACHINE, FZX_FINISH_MENU_COURSE, FZX_FINISH_MENU_QUIT };
enum { FZX_FINISH_MENU_SOUND, FZX_FINISH_MENU_LEVEL_FADE, FZX_FINISH_MENU_AUDIO_STOP };
typedef struct {
    uint32_t frame,seed1,mask1,seed2,mask2;
    uint16_t pressed;
    int16_t stick_y;
    uint8_t connected;
} fzx_finish_menu_input_t;
typedef struct { uint16_t id; uint8_t kind; } fzx_finish_menu_event_t;
typedef struct {
    fzx_finish_menu_input_t input;
    fzx_finish_menu_event_t events[FZX_FINISH_MENU_EVENT_MAX];
    uint32_t tick,seed1,mask1,seed2,mask2,rng1_calls,rng2_calls;
    int32_t selection,move_timer,reveal,draw_selection,draw_reveal;
    int32_t ghost_options_reveal,ghost_reveal,ghost_option_state,ghost_option,overwrite_option;
    int32_t ghost_open,ghost_timer,ghost_state,busy,state_flags;
    uint8_t action,event_count;
} fzx_finish_menu_snapshot_t;
typedef struct { fzx_finish_menu_snapshot_t snapshot; uint8_t initialized,fault; } fzx_finish_menu_state_t;

/* This first closure supports the original no-recorded-ghost path only. The
 * caller must establish that absence from the recording subsystem. It does
 * not disable a real ghost or implement saving, reload, audio or presentation.
 * Begin at the source records terminal; action is a request, never a reset. */
bool fzx_finish_menu_begin(fzx_finish_menu_state_t *,const fzx_records_snapshot_t *,bool no_recorded_ghost);
/* The FIRST step is on the SAME frame that records becomes ready, using the
 * same controller input retained from that source tick. Do not repoll input.
 * Advance once at the original finish-menu draw boundary. Both RNG streams
 * are supplied after simulation and must be committed together on success.
 * Draw selection/reveal describe the state before this tick's navigation.
 * Action requests freeze subsequent steps. Invalid input fails unchanged. */
bool fzx_finish_menu_step(fzx_finish_menu_state_t *,const fzx_finish_menu_input_t *);
bool fzx_finish_menu_snapshot(const fzx_finish_menu_state_t *,fzx_finish_menu_snapshot_t *);
#endif
