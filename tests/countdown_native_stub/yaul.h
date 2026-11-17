/* HOST-only API recorder. This is not the SDK command layout or an emulator. */
#ifndef FZX_COUNTDOWN_TEST_YAUL_H
#define FZX_COUNTDOWN_TEST_YAUL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uintptr_t vdp1_vram_t;
typedef struct {uint16_t raw;} rgb1555_t;
typedef struct {
    unsigned end_code_disable,trans_pixel_disable,user_clipping_mode,color_mode,cc_mode;
} vdp1_cmdt_draw_mode_t;
typedef struct {
    int16_t cmd_xa,cmd_ya,cmd_xb,cmd_yb,cmd_xc,cmd_yc,cmd_xd,cmd_yd;
    uintptr_t texture;
    uint16_t width,height,cmd_colr,cmd_grda;
    unsigned kind;
    vdp1_cmdt_draw_mode_t mode;
} vdp1_cmdt_t;
enum {VDP1_CMDT_CM_RGB_32768=5,VDP1_CMDT_CC_REPLACE=0};
static inline void vdp1_cmdt_distorted_sprite_set(vdp1_cmdt_t *c){c->kind=2;}
static inline void vdp1_cmdt_polygon_set(vdp1_cmdt_t *c){c->kind=4;}
static inline void vdp1_cmdt_user_clip_coord_set(vdp1_cmdt_t *c){c->kind=8;}
static inline void vdp1_cmdt_draw_mode_set(vdp1_cmdt_t *c,vdp1_cmdt_draw_mode_t m){c->mode=m;}
static inline void vdp1_cmdt_char_base_set(vdp1_cmdt_t *c,vdp1_vram_t p){c->texture=p;}
static inline void vdp1_cmdt_char_size_set(vdp1_cmdt_t *c,unsigned w,unsigned h){c->width=(uint16_t)w;c->height=(uint16_t)h;}
static inline void vdp1_cmdt_color_set(vdp1_cmdt_t *c,rgb1555_t color){c->cmd_colr=color.raw;}
#endif
