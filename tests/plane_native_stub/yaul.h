/* HOST-only command recorder for the plane compact-address gate. No SDK. */
#ifndef FZX_TEST_PLANE_YAUL_H
#define FZX_TEST_PLANE_YAUL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uintptr_t vdp1_vram_t;
typedef struct {
    unsigned end_code_disable,user_clipping_mode,color_mode,cc_mode;
} vdp1_cmdt_draw_mode_t;
typedef struct {
    int16_t cmd_xa,cmd_ya,cmd_xb,cmd_yb,cmd_xc,cmd_yc,cmd_xd,cmd_yd;
    uint16_t cmd_grda,width,height;
    uintptr_t texture;
    unsigned kind;
    vdp1_cmdt_draw_mode_t mode;
} vdp1_cmdt_t;
enum {VDP1_CMDT_CM_RGB_32768=5,VDP1_CMDT_CC_REPLACE=0,VDP1_CMDT_CC_GOURAUD=4};
static inline void vdp1_cmdt_distorted_sprite_set(vdp1_cmdt_t *c){c->kind=2;}
static inline void vdp1_cmdt_user_clip_coord_set(vdp1_cmdt_t *c){c->kind=8;}
static inline void vdp1_cmdt_draw_mode_set(vdp1_cmdt_t *c,vdp1_cmdt_draw_mode_t m){c->mode=m;}
static inline void vdp1_cmdt_char_base_set(vdp1_cmdt_t *c,vdp1_vram_t p){c->texture=p;}
static inline void vdp1_cmdt_char_size_set(vdp1_cmdt_t *c,unsigned w,unsigned h){c->width=(uint16_t)w;c->height=(uint16_t)h;}
static inline uint16_t cpu_frt_count_get(void){return 0;}
#endif
