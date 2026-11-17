#ifndef FZX_MACHINE_MENU_H
#define FZX_MACHINE_MENU_H
#include <stdbool.h>
#include <stdint.h>

#define FZX_MACHINE_MENU_DRAW_MAX 512u
#define FZX_MACHINE_MENU_OBJECT_MAX 16u
enum { FZX_MACHINE_SCREEN_SELECT, FZX_MACHINE_SCREEN_SETTINGS };
enum { FZX_MACHINE_DRAW_IMAGE, FZX_MACHINE_DRAW_GLYPH,
       FZX_MACHINE_DRAW_RECT, FZX_MACHINE_DRAW_MODELS };
enum { FZX_MACHINE_REQUEST_NONE, FZX_MACHINE_REQUEST_COURSE,
       FZX_MACHINE_REQUEST_SETTINGS, FZX_MACHINE_REQUEST_SELECT,
       FZX_MACHINE_REQUEST_RACE };
/* Source 320x240 coordinates. Rectangles include x2/y2. Glyph image is its
 * original font-table index. Native presentation crops eight vertical lines. */
typedef struct {
    uint16_t image;
    uint8_t kind,font;
    int16_t x,y,x2,y2;
    uint16_t scale_x_q8,scale_y_q8;
    uint8_t r,g,b,a,flip_x;
} fzx_machine_menu_draw_t;
typedef struct {
    int32_t cmdId,state,left,top,counter,counter2,cacheIndex;
    uint8_t priority,shouldDraw;
} fzx_machine_menu_object_t;
typedef struct { int32_t character,machineSkinIndex; } fzx_machine_menu_racer_t;
typedef struct {
    fzx_machine_menu_draw_t draws[FZX_MACHINE_MENU_DRAW_MAX];
    fzx_machine_menu_object_t objects[FZX_MACHINE_MENU_OBJECT_MAX];
    uint32_t tick,frame,model_ticks,camera_updates;
    float engine;
    uint16_t count;
    uint8_t object_count,screen,phase,character,skin,slot,locked,unlocked,request;
} fzx_machine_menu_snapshot_t;
/* Single-player original Time Attack slice. unlock_count is the original
 * func_8007E008 count (0..12), before its /3 *6 +6 conversion. last_engine
 * supplies save state; this API does not implement persistence. */
typedef struct {
    uint32_t frame;
    uint8_t mode,course,character,skin,unlock_count,everything_unlocked;
    float last_engine[30];
} fzx_machine_menu_config_t;
typedef struct {
    fzx_machine_menu_config_t config;
    fzx_machine_menu_snapshot_t snapshot;
    fzx_machine_menu_object_t objects[FZX_MACHINE_MENU_OBJECT_MAX];
    fzx_machine_menu_racer_t racers[4];
    int16_t indices[8];
    int8_t locks[4],changed[4];
    float engine[4],last_engine[30];
    int32_t select_phase,settings_phase,unlocked,game_mode,transition;
    uint32_t ticks,frame,same_input_ticks,model_ticks,camera_updates;
    uint16_t held,pressed,input,cues[8];
    uint16_t cache_images[16];
    int8_t stick_x;
    uint8_t screen,connected,request,cue_count,object_count,color[4],fault,cache_count;
} fzx_machine_menu_state_t;
bool fzx_machine_menu_reset(fzx_machine_menu_state_t *state,
                           const fzx_machine_menu_config_t *config,uint8_t screen);
bool fzx_machine_menu_step(fzx_machine_menu_state_t *state,uint16_t buttons,
                          int8_t stick_x,bool connected,bool transition);
bool fzx_machine_menu_snapshot(const fzx_machine_menu_state_t *state,
                              fzx_machine_menu_snapshot_t *out);
#endif
