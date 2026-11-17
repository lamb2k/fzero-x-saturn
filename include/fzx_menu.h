#ifndef FZX_MENU_H
#define FZX_MENU_H
#include <stdbool.h>
#include <stdint.h>

#define FZX_MENU_IMAGE_COUNT 19u
#define FZX_MENU_DRAW_MAX 32u
typedef enum {
    FZX_MENU_BACKGROUND, FZX_MENU_GP, FZX_MENU_TIME_ATTACK, FZX_MENU_DEATH_RACE,
    FZX_MENU_VS, FZX_MENU_PRACTICE, FZX_MENU_OPTIONS, FZX_MENU_HEADER,
    FZX_MENU_FLAMES, FZX_MENU_NOVICE, FZX_MENU_STANDARD, FZX_MENU_EXPERT,
    FZX_MENU_MASTER, FZX_MENU_2PLAYERS, FZX_MENU_3PLAYERS, FZX_MENU_4PLAYERS,
    FZX_MENU_TIME_ATTACK_LABEL, FZX_MENU_RECORDS_LABEL, FZX_MENU_OK
} fzx_menu_image_t;
typedef enum {
    FZX_MENU_REQUEST_NONE, FZX_MENU_REQUEST_TITLE, FZX_MENU_REQUEST_COURSE,
    FZX_MENU_REQUEST_MACHINE, FZX_MENU_REQUEST_OPTIONS, FZX_MENU_REQUEST_RECORDS
} fzx_menu_request_t;
/* Source 320x240 positions, ordered back to front. RGBA is normalized for
 * the source texture combiner: mode-0 RGBA images have white modulation. */
typedef struct {
    uint16_t image;
    int16_t x,y;
    uint8_t r,g,b,a;
} fzx_menu_draw_t;
typedef struct {
    fzx_menu_draw_t draws[FZX_MENU_DRAW_MAX];
    uint32_t tick;
    uint16_t count;
    uint8_t phase,selected,suboption,request,players,difficulty;
} fzx_menu_snapshot_t;
typedef struct { int32_t cmdId,left,top,counter,state; } fzx_menu_object_t;
typedef struct {
    fzx_menu_snapshot_t snapshot;
    fzx_menu_object_t objects[15];
    int32_t phase,selected,suboptions[8],players,difficulty,course;
    int32_t unlock_level,everything_unlocked,transition,change_state,menu_change;
    uint32_t ticks,frame,unused_random_calls,same_input_ticks;
    uint16_t held,pressed,input,cue,cues[4];
    uint8_t connected,request,stop_music,player_audio_mode,course_prepare;
    uint8_t color[4],fault,cue_count;
} fzx_menu_state_t;
/* First US cartridge main menu, background 0. Cheat wallpaper transitions,
 * persistence and audio playback are outside this closure. Default selection
 * is stock GP; callers may choose another ordinary cartridge mode explicitly. */
bool fzx_menu_reset(fzx_menu_state_t *state,uint8_t selected,uint8_t unlock_level,
                    bool everything_unlocked,uint32_t frame);
bool fzx_menu_step(fzx_menu_state_t *state,uint16_t buttons,uint8_t connected,bool transition);
bool fzx_menu_snapshot(const fzx_menu_state_t *state,fzx_menu_snapshot_t *out);
#endif
