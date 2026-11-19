#ifndef FZX_RACER_SPRITE_NATIVE_H
#define FZX_RACER_SPRITE_NATIVE_H
/* FZX_RACER_SPRITES: extra racers drawn with the player's rotation sprite set
 * (until per-machine sets exist). Each body uses the source draw-state
 * convention of RefMachine: model_pos and model_basis (rows), the player's
 * model scale. Bodies are posed, culled, sorted far to near and emitted as one
 * VDP1 distorted sprite each. Returns the number of commands written. */
#include <stdint.h>
#include <yaul.h>
#include "fzx_view.h"

#define FZX_RACER_SPRITE_MAX_EXTRA 31u

typedef struct {
    float pos[3];
    float basis[3][3];
} fzx_racer_sprite_body_t;

unsigned fzx_racer_sprites_draw(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                                const fzx_racer_sprite_body_t *bodies,unsigned n);

/* Fixed-point racer pose (agreed with the rivals lane through the coordinator).
 * pos: world position, Q16.16 source units (RefMachine.model_pos axes), within
 * about 32,768 units of the camera. basis: RefMachine.model_basis rows, Q1.14:
 * row 0 forward (model +z), row 1 up (model +y), row 2 side (model +x),
 * orthonormal to about 1/1000. Row 2 is the source basis z, positive LEFT,
 * exactly the axis the source maps model vertex x onto, so cell selection and
 * mirroring use it as model +x. set: sprite set (0 = the player's set until the
 * roster bake exists). flags bit 0: may have a blob shadow, drawn when the racer
 * is one of the nearest FZX_RACER_SPRITE_SHADOW_NEAR (8) to the camera; bit 1:
 * always has its shadow (the player). The poses must stay valid until the draw
 * call returns. */
typedef struct {
    int32_t pos[3];
    int16_t basis[3][3];
    uint16_t set,flags;
} fzx_racer_pose16_t;
/* 34 bytes of fields; 36 with the int32 alignment (same ABI as the rivals lane's copy). */
_Static_assert(sizeof(fzx_racer_pose16_t)==36,"pose16 hand-off ABI");

/* No float maths per racer: SH2ZAM rotation-sprites-1 fast path (hardware divider). */
unsigned fzx_racer_sprites_draw16(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                                  const fzx_racer_pose16_t *racers,unsigned n);
#endif
