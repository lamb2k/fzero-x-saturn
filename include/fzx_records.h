#ifndef FZX_RECORDS_H
#define FZX_RECORDS_H
#include "fzx_results.h"

#define FZX_RECORDS_KEY_COUNT 50u
#define FZX_RECORDS_EVENT_MAX 4u

enum { FZX_RECORDS_NAME_ENTRY, FZX_RECORDS_TABLE_TRANSITION,
       FZX_RECORDS_FINISH_MENU_READY };
enum { FZX_RECORDS_DRAW_NONE, FZX_RECORDS_DRAW_NAME, FZX_RECORDS_DRAW_TABLE };

/* Original Controller fields, after source input processing. Stick flags use
 * the original high nibble; repeat is provided by source, not synthesized here.
 * RNG words are the source Math_Rand1 state at this tick's post-camera boundary. */
typedef struct {
    uint32_t frame,rng_seed1,rng_mask1;
    uint16_t button_pressed,button_current;
    uint8_t stick_pressed,stick_current,retrigger,connected;
} fzx_records_input_t;

typedef struct {
    char character[2];
    int16_t rand_left,rand_top,left,top;
} fzx_records_key_t;

/* State/position packet only. Original glyph geometry, machine previews and
 * image packets belong to the presentation adapter. No asset bytes are here.
 * draw_kind records the branch entered before the source update; draw_name_state
 * preserves the source's one-frame transition-in draw on reaching the keyboard.
 * spinner_keyboard_index is the original local index, which can differ from
 * the final cursor on confirmation/third-letter ticks. */
typedef struct {
    fzx_results_config_t config;
    fzx_results_records_t records;
    fzx_records_input_t input;
    fzx_records_key_t keys[FZX_RECORDS_KEY_COUNT];
    fzx_results_event_t events[FZX_RECORDS_EVENT_MAX];
    uint32_t tick,frame,rng_seed1,rng_mask1,rng_calls,save_requests;
    uint16_t input_pressed,input_button_pressed,input_retriggered;
    uint16_t record_flags,record_increase;
    int32_t record_state,record_left_offset,record_transition_timer;
    int32_t record_left,record_top,record_height;
    int32_t max_speed_left,max_speed_top,max_speed_machine_offset;
    int16_t best_time_left,best_time_top,best_lap_left_offset,best_lap_top_offset;
    int16_t name_state,previous_name_state,draw_name_state,name_timer;
    int16_t cursor_x,cursor_y,name_length,enter_index,selected_timer[3];
    int16_t spinner_x,spinner_y,spinner_keyboard_index;
    char name[4];
    uint8_t phase,draw_kind,event_count,name_entry,spinner_valid;
    uint8_t record_position,best_lap,max_speed_record;
} fzx_records_snapshot_t;

/* Public storage for allocation only; direct state mutation is unsupported. */
typedef struct { fzx_records_snapshot_t snapshot; uint8_t initialized,fault; } fzx_records_state_t;

/* Begin only from a valid terminal FZX_RESULTS_RECORDS_REQUEST. Copies the
 * course/config/session records; it neither recomputes the finish ranking nor
 * consumes/emits the initial results save event. No RNG is consumed here. */
bool fzx_records_begin(fzx_records_state_t *state,const fzx_results_state_t *results);
/* Exactly once per source tick after Racer_Update + Camera_Update. The first
 * call belongs to the SAME frame that the results panel reaches 400. Stop at
 * FINISH_MENU_READY: retry, persistent saves and audio playback are not here.
 * After a successful step with rng_calls>0, write rng_seed1/rng_mask1 back to
 * the original RNG. Events are requests, consumed at most once per tick.
 * Invalid input/state fails unchanged. Terminal steps succeed without change. */
bool fzx_records_step(fzx_records_state_t *state,const fzx_records_input_t *input);
bool fzx_records_snapshot(const fzx_records_state_t *state,fzx_records_snapshot_t *out);
#endif
