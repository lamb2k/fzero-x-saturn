#ifndef FZX_COURSE_MENU_H
#define FZX_COURSE_MENU_H
#include <stdbool.h>
#include <stdint.h>

#define FZX_COURSE_MENU_IMAGE_COUNT 24u
#define FZX_COURSE_MENU_DRAW_MAX 160u
#define FZX_COURSE_MENU_OBJECT_MAX 16u
typedef enum {
    FZX_COURSE_IMAGE_BACKGROUND, FZX_COURSE_IMAGE_CUP_JACK, FZX_COURSE_IMAGE_CUP_QUEEN,
    FZX_COURSE_IMAGE_CUP_KING, FZX_COURSE_IMAGE_CUP_JOKER, FZX_COURSE_IMAGE_CUP_X,
    FZX_COURSE_IMAGE_CUP_EDIT, FZX_COURSE_IMAGE_CUP_QUESTION, FZX_COURSE_IMAGE_HEADER,
    FZX_COURSE_IMAGE_RECORDS, FZX_COURSE_IMAGE_OK, FZX_COURSE_IMAGE_ARROW,
    FZX_COURSE_IMAGE_HELMET, FZX_COURSE_IMAGE_CLEARED_1, FZX_COURSE_IMAGE_CLEARED_2,
    FZX_COURSE_IMAGE_CLEARED_3, FZX_COURSE_IMAGE_CLEARED_4, FZX_COURSE_IMAGE_GHOST_MARKER,
    FZX_COURSE_IMAGE_STAFF_BEATEN, FZX_COURSE_IMAGE_WITH_GHOST, FZX_COURSE_IMAGE_WITHOUT_GHOST,
    FZX_COURSE_IMAGE_STAFF_GHOST, FZX_COURSE_IMAGE_CELEBRITY_GHOST, FZX_COURSE_IMAGE_CHAMP_GHOST
} fzx_course_menu_image_t;
enum { FZX_COURSE_DRAW_IMAGE, FZX_COURSE_DRAW_GLYPH, FZX_COURSE_DRAW_PREVIEW };
typedef enum {
    FZX_COURSE_REQUEST_NONE, FZX_COURSE_REQUEST_MAIN_MENU, FZX_COURSE_REQUEST_MACHINE,
    FZX_COURSE_REQUEST_RECORDS, FZX_COURSE_REQUEST_RECORDS_EXIT
} fzx_course_menu_request_t;
enum {
    FZX_COURSE_OBJECT_BACKGROUND, FZX_COURSE_OBJECT_MODEL, FZX_COURSE_OBJECT_CUP_0,
    FZX_COURSE_OBJECT_CUP_1, FZX_COURSE_OBJECT_CUP_2, FZX_COURSE_OBJECT_CUP_3,
    FZX_COURSE_OBJECT_CUP_4, FZX_COURSE_OBJECT_CUP_5, FZX_COURSE_OBJECT_OK,
    FZX_COURSE_OBJECT_ARROWS, FZX_COURSE_OBJECT_NAME, FZX_COURSE_OBJECT_HEADER,
    FZX_COURSE_OBJECT_GHOST_MARKER, FZX_COURSE_OBJECT_GHOST_OPTION,
    FZX_COURSE_OBJECT_FRAMEBUFFER
};
/* Ordered source 320x240 draw packets. Glyph image is the original font-table
 * index, font is 3/4, and y already includes the source baseline adjustment.
 * q8 scale 256 means 1.0. Prim color is normalized for the source combiner. */
typedef struct {
    uint16_t image;
    uint8_t kind,font;
    int16_t x,y;
    uint16_t scale_x_q8,scale_y_q8;
    uint8_t r,g,b,a,flip_x;
} fzx_course_menu_draw_t;
typedef struct {
    int32_t cmdId,state,state2,left,top,counter,counter2;
    uint8_t priority,shouldDraw;
} fzx_course_menu_object_t;
typedef struct {
    uint8_t kind,priority,visible;
    int32_t x,y,state,counter,rotation_right,rotation_delta_left,rotation_delta_right;
} fzx_course_menu_object_snapshot_t;
typedef struct {
    fzx_course_menu_draw_t draws[FZX_COURSE_MENU_DRAW_MAX];
    fzx_course_menu_object_snapshot_t objects[FZX_COURSE_MENU_OBJECT_MAX];
    uint32_t tick,frame,preview_updates,preview_loads,camera_updates,camera_orbit_ticks;
    float camera_yaw_degrees,camera_pitch_degrees;
    int32_t carousel_x;
    uint16_t count;
    uint8_t object_count,phase,cup_option,cup_type,course,track;
    uint8_t ghost_option,ghost_type,unlocked_ghosts,staff_beaten,request;
    uint8_t preview_cup,preview_visible,camera_disable_requested,records;
} fzx_course_menu_snapshot_t;
/* mode: original GP=0, TA=1, Death=2, VS=3, Practice=5. records selects
 * the original records course screen. last_course preserves source resume
 * clamping (0..53). ghost_type: none0/player1/staff2/celebrity3/champ4.
 * Time0 means unavailable; supplied nonzero times are milliseconds. These
 * are inputs, not a persistence implementation. Config is copied on reset. */
typedef struct {
    uint32_t frame;
    uint8_t mode,records,last_course,ghost_type,unlock_level,everything_unlocked;
    uint8_t player_ghost_valid,player_ghost_course,cup_cleared[4];
    int32_t staff_time[24],best_time[24];
} fzx_course_menu_config_t;
typedef struct {
    fzx_course_menu_config_t config;
    fzx_course_menu_snapshot_t snapshot;
    fzx_course_menu_object_t objects[FZX_COURSE_MENU_OBJECT_MAX];
    int32_t phase,cup_option,cup,track,course,cup_type,last_course,last_records;
    int32_t selected_ghost,ghost_type,unlocked_ghosts,staff_beaten,last_ghost_course;
    int32_t transition,change_state,menu_change,preview_cup,preview_timer,carousel_x;
    int32_t camera_state,source_name_course,ui_flag;
    uint32_t ticks,frame,same_input_ticks,preview_updates,preview_loads,camera_updates,camera_orbit_ticks;
    float camera_yaw_degrees,camera_pitch_degrees;
    uint16_t held,pressed,input,cues[8];
    uint8_t connected,request,cue_count,object_count,color[4],fault;
} fzx_course_menu_state_t;
bool fzx_course_menu_reset(fzx_course_menu_state_t *state,const fzx_course_menu_config_t *config);
bool fzx_course_menu_step(fzx_course_menu_state_t *state,uint16_t buttons,uint8_t connected,bool transition);
bool fzx_course_menu_snapshot(const fzx_course_menu_state_t *state,fzx_course_menu_snapshot_t *out);
#endif
